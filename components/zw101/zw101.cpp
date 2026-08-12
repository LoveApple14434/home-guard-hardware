#include "zw101.h"
#include "esphome/core/log.h"
#include <string.h>

namespace esphome {
namespace zw101 {

static const char *const TAG = "zw101";

// 前向声明（定义在文件后部）
static const char *ack_code_to_str(uint8_t code);
static const char *auto_step_str(uint8_t p1);

void ZW101Component::setup() {
  ESP_LOGI(TAG, "===== ZW101 SETUP START =====");
  
  // 初始化搜索状态
  search_last_action_ = millis();
  
  // 握手测试
  ESP_LOGI(TAG, "Attempting handshake...");
  if (!handshake()) {
    ESP_LOGW(TAG, "Initial handshake failed, will retry in loop");
  } else {
    ESP_LOGI(TAG, "✓ Setup complete - handshake OK");
  }
  ESP_LOGI(TAG, "===== ZW101 SETUP END =====");
}

void ZW101Component::loop() {
  uint32_t now = millis();

  // 读取模组信息（首次运行时）
  if (!info_read_) {
    info_read_ = true;
    read_fp_info();
  }

  // 自动注册/匹配模式：监听模块主动上报，不跑手动搜索
  if (auto_mode_active_) {
    process_auto_enroll();
    return;
  }

  // 手动注册流程（兼容保留，当前入口已走自动注册）
  if (enroll_state_ != ENROLL_IDLE) {
    process_enrollment();
    return;
  }

  // 自动搜索：状态机推进
  if (search_state_ != SEARCH_IDLE) {
    process_search();
    return;
  }

  // 空闲时每秒触发一次搜索
  if (now - search_last_action_ > 1000) {
    search_state_ = SEARCH_GET_IMAGE;
    search_last_action_ = now;
    process_search();
  }

  // 处理匹配成功后的清除状态
  if (match_found_ && now >= match_clear_time_) {
    match_found_ = false;
    if (fingerprint_sensor_)
      fingerprint_sensor_->publish_state(false);
  }
}

// 非阻塞式注册流程处理（单次注册：29H 采图 → 02H 特征 → 06H 直接存储，不合并）
void ZW101Component::process_enrollment() {
  uint32_t now = millis();

  switch (enroll_state_) {
    case ENROLL_WAIT_FINGER:
      if (now - enroll_last_action_ > 500) {
        // 注册流程必须用 29H（注册用获取图像），不能用 01H（验证用）
        send_cmd(CMD_GET_IMAGE_ENROLL);
        enroll_state_ = ENROLL_CAPTURING;
        enroll_last_action_ = now;
      }
      break;

    case ENROLL_CAPTURING:
      if (receive_response()) {
        // 采图成功 → 生成特征到 Buffer 1
        if (status_sensor_)
          status_sensor_->publish_state("Captured, generating feature...");
        send_cmd2(CMD_GEN_CHAR, 1);
        enroll_state_ = ENROLL_GEN_CHAR;
        enroll_last_action_ = now;
      } else {
        // 无手指(0x02) 或超时 → 回到等待，稍后重试，避免卡死
        enroll_state_ = ENROLL_WAIT_FINGER;
        enroll_last_action_ = now;
      }
      break;

    case ENROLL_GEN_CHAR:
      if (receive_response()) {
        // 特征生成成功 → 直接存储 Buffer 1 到 next_id（不合并）
        if (status_sensor_)
          status_sensor_->publish_state("Storing template...");
        send_store_cmd(1, next_fingerprint_id_);
        enroll_state_ = ENROLL_STORING;
        enroll_last_action_ = now;
      } else {
        // 特征生成失败（手指质量差）→ 重新采图
        ESP_LOGW(TAG, "Gen char failed, retry capture");
        if (status_sensor_)
          status_sensor_->publish_state("Retry: place finger again");
        enroll_state_ = ENROLL_WAIT_FINGER;
        enroll_last_action_ = now;
      }
      break;

    case ENROLL_STORING:
      // 存储模板，用 3000ms 超时
      if (receive_response(3000)) {
        ESP_LOGI(TAG, "✓ Fingerprint enrolled successfully as ID %d", next_fingerprint_id_);
        if (status_sensor_) {
          char buf[64];
          snprintf(buf, sizeof(buf), "Enroll Success (ID: %d)", next_fingerprint_id_);
          status_sensor_->publish_state(buf);
        }
        next_fingerprint_id_++;
        enroll_state_ = ENROLL_IDLE;
      } else if (now - enroll_last_action_ > 8000) {
        // 存储超时 → 放弃，回到 IDLE
        ESP_LOGW(TAG, "Enrollment store timeout, aborting");
        if (status_sensor_)
          status_sensor_->publish_state("Enroll Failed");
        enroll_state_ = ENROLL_IDLE;
      }
      break;

    default:
      break;
  }
}

// 非阻塞式搜索流程处理
void ZW101Component::process_search() {
  uint32_t now = millis();

  switch (search_state_) {
    case SEARCH_GET_IMAGE:
      // 发送前清空接收缓冲，避免上次残留数据被误读
      flush_rx();
      send_cmd(CMD_GET_IMAGE);
      search_state_ = SEARCH_GEN_CHAR;
      search_last_action_ = now;
      break;

    case SEARCH_GEN_CHAR:
      // 等待 GET_IMAGE 的响应（无手指时模块会快速回复确认码 0x02）
      if (receive_response()) {
        // 采集到有效指纹图像 → 生成特征
        flush_rx();
        send_cmd2(CMD_GEN_CHAR, 1);
        search_state_ = SEARCH_DO_SEARCH;
        search_last_action_ = now;
      } else {
        // 无手指(0x02) 或超时 → 立即回到 IDLE，等下一次扫描，避免空等
        search_state_ = SEARCH_IDLE;
      }
      break;

    case SEARCH_DO_SEARCH:
      // 等待 GEN_CHAR 的响应
      if (receive_response()) {
        // 特征生成成功 → 搜索指纹库
        flush_rx();
        send_search_cmd(1, 0, library_capacity_);
        search_state_ = SEARCH_WAIT_RESULT;
        search_last_action_ = now;
      } else {
        // 特征生成失败（手指移动等）→ 回到 IDLE
        search_state_ = SEARCH_IDLE;
      }
      break;

    case SEARCH_WAIT_RESULT:
      {
        // 等待 SEARCH 命令的结果（模块响应很快）
        uint8_t response[50];
        uint8_t length = wait_for_response(response, 50, 300);

        if (length >= 12 && response[9] == 0x00) {
          // 匹配成功
          uint16_t match_id = (response[10] << 8) | response[11];
          uint16_t match_score = (response[12] << 8) | response[13];

          ESP_LOGI(TAG, "Match found! ID: %d, Score: %d", match_id, match_score);

          if (fingerprint_sensor_) {
            fingerprint_sensor_->publish_state(true);
            match_found_ = true;
            match_clear_time_ = now + 1000;  // 1秒后清除状态
          }

          if (match_id_sensor_)
            match_id_sensor_->publish_state(match_id);

          if (match_score_sensor_)
            match_score_sensor_->publish_state(match_score);

          if (status_sensor_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Match: ID %d (Score: %d)", match_id, match_score);
            status_sensor_->publish_state(buf);
          }
        } else if (length >= 12) {
          // 有响应但未匹配成功（确认码 0x09 = 未搜索到指纹）
          ESP_LOGI(TAG, "Search finished (no match): ACK=0x%02X", response[9]);
        }
        search_state_ = SEARCH_IDLE;
      }
      break;

    default:
      search_state_ = SEARCH_IDLE;
      break;
  }
}

// 注册指纹 - 单次注册模式（29H 采图 + 02H 特征 + 06H 直接存储，不合并）
// 每次触发按压一次手指即可注册一个指纹（ID 自动递增）
// 注：05H 合并在本模块固件上返回 0x02 失败，故采用单次注册
bool ZW101Component::register_fingerprint() {
  if (enroll_state_ != ENROLL_IDLE || auto_mode_active_) {
    ESP_LOGW(TAG, "Enrollment already in progress");
    return false;
  }

  if (status_sensor_)
    status_sensor_->publish_state("Place finger once to enroll");
  ESP_LOGI(TAG, "Starting single fingerprint enrollment (ID=%d)", next_fingerprint_id_);

  enroll_state_ = ENROLL_WAIT_FINGER;
  enroll_sample_count_ = 0;
  enroll_last_action_ = millis();

  return true;
}

// 清空指纹库
bool ZW101Component::clear_fingerprint_library() {
  ESP_LOGI(TAG, "Clearing fingerprint library");
  if (status_sensor_)
    status_sensor_->publish_state("Clearing Library...");

  send_cmd(CMD_CLEAR_LIB);
  if (receive_response()) {
    if (status_sensor_)
      status_sensor_->publish_state("Library Cleared");
    ESP_LOGI(TAG, "Library cleared successfully");
    next_fingerprint_id_ = 0;
    return true;
  }

  if (status_sensor_)
    status_sensor_->publish_state("Clear Failed");
  return false;
}

// 读取模组信息
void ZW101Component::read_fp_info() {
  uint8_t response[32];

  // 首先读取系统参数获取指纹库容量
  send_cmd(CMD_READ_SYSPARA);
  uint8_t length = wait_for_response(response, 32, 2000);

  if (length >= 28 && response[9] == 0x00) {
    uint16_t fp_lib_size = (response[14] << 8) | response[15];
    library_capacity_ = fp_lib_size;
    ESP_LOGI(TAG, "Library capacity: %d", fp_lib_size);
  }

  // 握手测试
  if (handshake()) {
    if (status_sensor_) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Ready");
      status_sensor_->publish_state(buf);
    }
  }
}

// 握手测试
bool ZW101Component::handshake() {
  uint8_t packet[12];
  uint16_t length = 3;
  uint16_t checksum = 1 + length + CMD_HANDSHAKE;

  build_packet_header(packet, length);
  packet[9] = CMD_HANDSHAKE;
  packet[10] = (checksum >> 8) & 0xFF;
  packet[11] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "=== HANDSHAKE START ===");
  ESP_LOGI(TAG, "TX HANDSHAKE (CMD=0x%02X)", CMD_HANDSHAKE);
  ESP_LOG_BUFFER_HEX(TAG, packet, 12);
  
  write_array(packet, 12);
  flush();

  uint8_t response[32];
  uint8_t resp_len = wait_for_response(response, 32, 500);

  if (resp_len >= 12 && response[9] == 0x00) {
    ESP_LOGI(TAG, "✓ Handshake successful");
    return true;
  }

  ESP_LOGW(TAG, "✗ Handshake failed - resp_len=%d", resp_len);
  if (resp_len > 0) {
    ESP_LOG_BUFFER_HEX(TAG, response, resp_len);
  }
  return false;
}

// 删除指定指纹
bool ZW101Component::delete_fingerprint(uint16_t id) {
  uint8_t packet[16];
  uint16_t length = 7;
  uint16_t delete_count = 1;
  uint16_t checksum = 1 + length + CMD_DEL_CHAR +
                      (id >> 8) + (id & 0xFF) +
                      (delete_count >> 8) + (delete_count & 0xFF);

  build_packet_header(packet, length);
  packet[9] = CMD_DEL_CHAR;
  packet[10] = (id >> 8) & 0xFF;
  packet[11] = id & 0xFF;
  packet[12] = (delete_count >> 8) & 0xFF;
  packet[13] = delete_count & 0xFF;
  packet[14] = (checksum >> 8) & 0xFF;
  packet[15] = checksum & 0xFF;

  write_array(packet, 16);
  flush();

  uint8_t response[32];
  uint8_t resp_len = wait_for_response(response, 32, 1000);

  if (resp_len >= 12 && response[9] == 0x00) {
    ESP_LOGI(TAG, "Fingerprint ID %d deleted successfully", id);
    if (status_sensor_) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Deleted ID: %d", id);
      status_sensor_->publish_state(buf);
    }
    return true;
  }

  ESP_LOGW(TAG, "Failed to delete fingerprint ID %d", id);
  return false;
}

// RGB LED 控制
void ZW101Component::set_rgb_led(uint8_t mode, uint8_t color, uint8_t brightness) {
  uint8_t packet[16];
  uint16_t length = 7;
  uint16_t checksum = 1 + length + CMD_RGB_CTRL + mode + color + brightness;

  build_packet_header(packet, length);
  packet[9] = CMD_RGB_CTRL;
  packet[10] = mode;
  packet[11] = color;
  packet[12] = brightness;
  packet[13] = (checksum >> 8) & 0xFF;
  packet[14] = checksum & 0xFF;

  write_array(packet, 15);
  flush();
}

// ============================================================
// 自动注册模式（官方 31H 协议）
// 指令包: EF 01 FF FF FF FF 01 00 08 31 [ID高][ID低][次数][参高][参低][校验和2B]
// 应答包: EF 01 FF FF FF FF 07 00 05 [确认码][参数1][参数2][校验和2B]
//   模块按参数 bit2=0 要求返回关键步骤:
//     00 01 0n 采图 → 00 02 0n 生成特征 → 00 03 0n 手指离开(一次完成)
//     → 00 04 F0 合并模板 → 00 05 F1 重复检查 → 00 06 F2 存储成功=注册完成!
// ============================================================
bool ZW101Component::auto_enroll_mode() {
  if (auto_mode_active_) {
    ESP_LOGW(TAG, "Auto enroll already active");
    return false;
  }

  uint8_t packet[17];
  uint16_t length = 8;                 // 指令1 + ID2 + 次数1 + 参数2 + 校验和2
  uint8_t enroll_times = 4;            // 官方示例：录入 4 次
  uint16_t params = 0x0000;            // bit0=0 LED长亮, bit2=0 要求返回关键步骤,
                                       // bit3=0 不允许覆盖ID, bit4=0 允许重复注册, bit5=0 要求手指离开
  uint16_t id = next_fingerprint_id_;

  uint16_t checksum = 1 + length + CMD_AUTO_ENROLL +
                      (id >> 8) + (id & 0xFF) +
                      enroll_times +
                      (params >> 8) + (params & 0xFF);

  build_packet_header(packet, length);
  packet[9] = CMD_AUTO_ENROLL;
  packet[10] = (id >> 8) & 0xFF;
  packet[11] = id & 0xFF;
  packet[12] = enroll_times;
  packet[13] = (params >> 8) & 0xFF;
  packet[14] = params & 0xFF;
  packet[15] = (checksum >> 8) & 0xFF;
  packet[16] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX AUTO_ENROLL ID=%d times=%d params=0x%04X", id, enroll_times, params);
  ESP_LOG_BUFFER_HEX(TAG, packet, 17);
  write_array(packet, 17);
  flush();

  auto_mode_active_ = true;
  auto_mode_timeout_ = millis() + 90000;   // ESP32 侧 90 秒总超时
  auto_rx_idx_ = 0;

  if (status_sensor_)
    status_sensor_->publish_state("Auto Enroll: place finger 4x");
  ESP_LOGI(TAG, "Auto enroll started, ID=%d, place finger %d times", id, enroll_times);
  return true;
}

// 取消自动模式（发送 30H）
void ZW101Component::cancel_auto_mode() {
  uint8_t packet[12];
  uint16_t length = 3;
  uint16_t checksum = 1 + length + CMD_AUTO_CANCEL;

  build_packet_header(packet, length);
  packet[9] = CMD_AUTO_CANCEL;
  packet[10] = (checksum >> 8) & 0xFF;
  packet[11] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX AUTO_CANCEL (0x30)");
  write_array(packet, 12);
  flush();

  auto_mode_active_ = false;
  auto_mode_timeout_ = 0;
  auto_rx_idx_ = 0;
  ESP_LOGI(TAG, "Auto mode cancelled");
}

// 自动模式响应步骤名称
static const char *auto_step_str(uint8_t p1) {
  switch (p1) {
    case 0x00: return "legality check";
    case 0x01: return "capture image (place finger)";
    case 0x02: return "generate feature";
    case 0x03: return "finger removed (sample done)";
    case 0x04: return "merge template";
    case 0x05: return "duplicate check";
    case 0x06: return "store template";
    default: return nullptr;
  }
}

// 自动注册响应处理（在 loop 中调用）
void ZW101Component::process_auto_enroll() {
  uint32_t now = millis();

  // 读取模块主动上报的所有字节
  while (available()) {
    if (auto_rx_idx_ < sizeof(auto_rx_buf_)) {
      auto_rx_buf_[auto_rx_idx_++] = read();
    } else {
      read();  // 丢弃溢出
    }

    // 尝试解析完整应答包
    if (auto_rx_idx_ >= 2 && auto_rx_buf_[0] == 0xEF && auto_rx_buf_[1] == 0x01) {
      if (auto_rx_idx_ >= 9) {
        uint16_t pkt_len = (auto_rx_buf_[7] << 8) | auto_rx_buf_[8];
        uint16_t total = pkt_len + 9;   // 应答包 pkt_len=5 → total=14
        if (auto_rx_idx_ >= total && total >= 12) {
          handle_auto_enroll_response(auto_rx_buf_, total);
          auto_rx_idx_ = 0;
        }
      }
    } else if (auto_rx_idx_ > 0) {
      // 包头不对，丢弃一个字节重新对齐
      memmove(auto_rx_buf_, auto_rx_buf_ + 1, auto_rx_idx_ - 1);
      auto_rx_idx_--;
    }
  }

  // 总超时保护
  if (auto_mode_timeout_ > 0 && now >= auto_mode_timeout_) {
    ESP_LOGW(TAG, "Auto enroll timeout, cancelling");
    if (status_sensor_)
      status_sensor_->publish_state("Enroll Timeout");
    cancel_auto_mode();
  }
}

// 解析自动注册应答包
void ZW101Component::handle_auto_enroll_response(uint8_t *buf, uint8_t len) {
  if (buf[6] != 0x07) {  // 非应答包，打印原始数据
    ESP_LOGI(TAG, "RX non-ack packet:");
    ESP_LOG_BUFFER_HEX(TAG, buf, len);
    return;
  }

  uint8_t code = buf[9];   // 确认码
  uint8_t p1 = buf[10];    // 参数1（步骤）
  uint8_t p2 = buf[11];    // 参数2（n / F0 / F1 / F2）

  ESP_LOGI(TAG, "AutoEnroll: code=0x%02X (%s) p1=0x%02X p2=0x%02X",
           code, ack_code_to_str(code), p1, p2);
  ESP_LOG_BUFFER_HEX(TAG, buf, len);

  const char *step = auto_step_str(p1);
  if (step != nullptr) {
    if (p2 == 0xF0) {
      ESP_LOGI(TAG, "  [%s]", step);
    } else if (p2 == 0xF1 || p2 == 0xF2) {
      ESP_LOGI(TAG, "  [%s]", step);
    } else {
      ESP_LOGI(TAG, "  [%s #%d]", step, p2);
    }
  }

  // 注册完成：确认码 00 + 存储模板 06 F2
  if (code == 0x00 && p1 == 0x06 && p2 == 0xF2) {
    ESP_LOGI(TAG, "✓ Fingerprint enrolled successfully as ID %d", next_fingerprint_id_);
    if (status_sensor_) {
      char s[64];
      snprintf(s, sizeof(s), "Enroll Success (ID: %d)", next_fingerprint_id_);
      status_sensor_->publish_state(s);
    }
    next_fingerprint_id_++;
    cancel_auto_mode();
    return;
  }

  // 错误处理：确认码非 00
  if (code != 0x00) {
    // 07 02 0n 生成特征失败 → 模块会自动重新采图，不取消
    if (code == 0x07 && p1 == 0x02) {
      ESP_LOGW(TAG, "  gen feature failed, module will retry capture...");
      return;
    }
    ESP_LOGW(TAG, "✗ AutoEnroll error: %s", ack_code_to_str(code));
    if (status_sensor_) {
      char s[64];
      snprintf(s, sizeof(s), "Enroll Failed: 0x%02X", code);
      status_sensor_->publish_state(s);
    }
    cancel_auto_mode();
    return;
  }
}

// ==================== 私有方法 ====================

// 发送简单命令
void ZW101Component::send_cmd(uint8_t cmd) {
  uint8_t packet[12];
  uint16_t length = 3;
  uint16_t checksum = 1 + length + cmd;

  build_packet_header(packet, length);
  packet[9] = cmd;
  packet[10] = (checksum >> 8) & 0xFF;
  packet[11] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX CMD 0x%02X (length=%d)", cmd, 12);
  ESP_LOG_BUFFER_HEX(TAG, packet, 12);
  
  write_array(packet, 12);
  flush();
}

// 发送带一个参数的命令
void ZW101Component::send_cmd2(uint8_t cmd, uint8_t param1) {
  uint8_t packet[13];
  uint16_t length = 4;
  uint16_t checksum = 1 + length + cmd + param1;

  build_packet_header(packet, length);
  packet[9] = cmd;
  packet[10] = param1;
  packet[11] = (checksum >> 8) & 0xFF;
  packet[12] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX CMD2 0x%02X param=0x%02X (length=%d)", cmd, param1, 13);
  ESP_LOG_BUFFER_HEX(TAG, packet, 13);
  
  write_array(packet, 13);
  flush();
}

// 发送存储命令
void ZW101Component::send_store_cmd(uint8_t buffer_id, uint16_t template_id) {
  uint8_t packet[15];
  uint16_t length = 6;
  uint16_t checksum = 1 + length + CMD_STORE_CHAR + buffer_id +
                      (template_id >> 8) + (template_id & 0xFF);

  build_packet_header(packet, length);
  packet[9] = CMD_STORE_CHAR;
  packet[10] = buffer_id;
  packet[11] = (template_id >> 8) & 0xFF;
  packet[12] = template_id & 0xFF;
  packet[13] = (checksum >> 8) & 0xFF;
  packet[14] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX STORE buffer=%d, template_id=%d (length=%d)", buffer_id, template_id, 15);
  ESP_LOG_BUFFER_HEX(TAG, packet, 15);
  
  write_array(packet, 15);
  flush();
}

// 发送搜索命令
void ZW101Component::send_search_cmd(uint8_t buffer_id, uint16_t start_page, uint16_t page_num) {
  uint8_t packet[17];
  uint16_t length = 8;
  uint16_t checksum = 1 + length + CMD_SEARCH + buffer_id +
                      (start_page >> 8) + (start_page & 0xFF) +
                      (page_num >> 8) + (page_num & 0xFF);

  build_packet_header(packet, length);
  packet[9] = CMD_SEARCH;
  packet[10] = buffer_id;
  packet[11] = (start_page >> 8) & 0xFF;
  packet[12] = start_page & 0xFF;
  packet[13] = (page_num >> 8) & 0xFF;
  packet[14] = page_num & 0xFF;
  packet[15] = (checksum >> 8) & 0xFF;
  packet[16] = checksum & 0xFF;

  flush_rx();
  ESP_LOGI(TAG, "TX SEARCH page=%d..%d (length=%d)", start_page, page_num, 17);
  ESP_LOG_BUFFER_HEX(TAG, packet, 17);
  
  write_array(packet, 17);
  flush();
}

// 构建数据包头
void ZW101Component::build_packet_header(uint8_t *packet, uint16_t length) {
  packet[0] = HEADER_HIGH;
  packet[1] = HEADER_LOW;
  packet[2] = (DEVICE_ADDRESS >> 24) & 0xFF;
  packet[3] = (DEVICE_ADDRESS >> 16) & 0xFF;
  packet[4] = (DEVICE_ADDRESS >> 8) & 0xFF;
  packet[5] = DEVICE_ADDRESS & 0xFF;
  packet[6] = 0x01;  // 包标识（命令包）! 必须与校验和计算中的 PID(=1) 一致
  packet[7] = (length >> 8) & 0xFF;
  packet[8] = length & 0xFF;
}

// 确认码含义解析
static const char *ack_code_to_str(uint8_t code) {
  switch (code) {
    case 0x00: return "OK";
    case 0x01: return "Packet error";
    case 0x02: return "No finger on sensor";
    case 0x03: return "Image capture fail";
    case 0x04: return "Finger too dry";
    case 0x05: return "Finger too wet";
    case 0x06: return "Image messy";
    case 0x07: return "Too few features";
    case 0x08: return "Fingerprint mismatch";
    case 0x09: return "No match found in lib";
    case 0x0A: return "Merge/combine fail";
    case 0x0B: return "Address code error";
    case 0x0C: return "Delete fail";
    case 0x0D: return "Clear library fail";
    case 0x0E: return "Library full";
    case 0x0F: return "ID not found";
    default: return "Unknown";
  }
}

// 接收响应
bool ZW101Component::receive_response(uint16_t timeout_ms) {
  uint8_t buffer[50];
  uint8_t len = wait_for_response(buffer, 50, timeout_ms);

  if (len >= 12) {
    // 打印确认码及含义，方便诊断
    uint8_t ack = buffer[9];
    ESP_LOGI(TAG, "RX ACK=0x%02X (%s)", ack, ack_code_to_str(ack));
    ESP_LOG_BUFFER_HEX(TAG, buffer, len);
    return ack == 0x00;
  }

  if (len > 0) {
    ESP_LOGW(TAG, "RX incomplete (%d bytes) - ignored", len);
    ESP_LOG_BUFFER_HEX(TAG, buffer, len);
  }
  return false;
}

// 清空接收缓冲区（发送新命令前调用，防止残留数据被误读）
void ZW101Component::flush_rx() {
  while (available()) {
    read();
  }
}

// 等待响应（静默计时模式）
// 收到任何字节都会重置"静默计时"，这样即使模块分段发送也能收全整个包。
// 完整包 + 静默 50ms → 返回；总超时兜底。
uint8_t ZW101Component::wait_for_response(uint8_t *buffer, uint8_t max_length, uint32_t timeout_ms) {
  uint32_t start_time = millis();
  uint32_t last_byte_time = 0;
  uint8_t idx = 0;
  uint16_t total_length = 0;
  bool header_valid = false;

  while (millis() - start_time < timeout_ms) {
    if (available()) {
      uint8_t byte = read();
      last_byte_time = millis();
      if (idx < max_length) {
        buffer[idx++] = byte;
      }

      // 已有完整包头（EF 01 + 地址4B + PID + 长度2B = 前9字节）时计算完整包长
      if (idx >= 9 && buffer[0] == HEADER_HIGH && buffer[1] == HEADER_LOW) {
        uint16_t pkt_length = (buffer[7] << 8) | buffer[8];
        total_length = pkt_length + 9;  // 头部(6) + PID(1) + 长度(2) + 数据 + 校验(2)
        header_valid = true;
      }
    } else {
      // 没有新字节到达
      if (header_valid && idx >= total_length) {
        // 完整包已收到，且已静默 50ms 确认无后续数据
        if (last_byte_time != 0 && millis() - last_byte_time > 50) {
          return idx;
        }
      }
    }
    delay(1);
  }

  if (idx > 0) {
    ESP_LOGW(TAG, "RX TIMEOUT - received %d bytes (incomplete)", idx);
    ESP_LOG_BUFFER_HEX(TAG, buffer, idx);
  }
  
  return idx;  // 返回收到的字节数
}

}  // namespace zw101
}  // namespace esphome
