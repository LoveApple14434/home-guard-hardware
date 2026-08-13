#include "tof050c_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tof050c {

static const char *const TAG = "tof050c";

// ---- VL6180X 寄存器 (16 位地址) ----
static const uint16_t REG_IDENTIFICATION_MODEL_ID = 0x0000;  // 读回 0xB4 = VL6180X
static const uint16_t REG_SYSTEM_INTERRUPT_CLEAR = 0x0015;   // 写 0x07 清除所有中断
static const uint16_t REG_SYSTEM_FRESH_OUT_OF_RESET = 0x0016;  // 上电置位; 首次使用前必须写 0 清除
static const uint16_t REG_SYSRANGE_START = 0x0018;           // 写 0x01 启动单次测距
static const uint16_t REG_SYSRANGE_PART_TO_PART_RANGE_OFFSET = 0x0024;  // 出厂校准偏移(需写回生效)
static const uint16_t REG_RESULT_INTERRUPT_STATUS_GPIO = 0x004F;  // 轮询, 低3位==0x04 表示完成
static const uint16_t REG_RESULT_RANGE_STATUS = 0x004D;          // range 状态(高 4 位为错误码)
static const uint16_t REG_RESULT_RANGE_VAL = 0x0062;              // 测距结果(1 字节, 单位 mm)
static const uint16_t REG_RANGE_SCALER = 0x0096;                  // 16bit 量程缩放

// 期望的芯片型号 ID
static const uint8_t VL6180X_EXPECTED_MODEL_ID = 0xB4;
// RANGE_SCALER 取值 (1x / 2x / 3x)
static const uint16_t VL6180X_SCALER_VALUES[] = {0x0000, 0x00FD, 0x007F};

void TOF050CSensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TOF050C (VL6180X)...");

  // 可选 SHUT 引脚: 输出高电平使能传感器(若硬件已把 SHUT 接 3.3V 则可省略)
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    this->enable_pin_->digital_write(true);
    delay(5);  // 等待模块上电启动
  }

  // 等待 FW boot 完成: 重试读取型号 ID 直到 0xB4 (最长 ~200ms)
  uint8_t model_id = 0;
  for (uint8_t i = 0; i < 40; i++) {
    model_id = this->reg16(REG_IDENTIFICATION_MODEL_ID).get();
    if (model_id == VL6180X_EXPECTED_MODEL_ID)
      break;
    delay(5);
  }
  if (model_id != VL6180X_EXPECTED_MODEL_ID) {
    ESP_LOGE(TAG, "Unexpected model ID 0x%02X (expected 0x%02X); is this a VL6180X?", model_id,
             VL6180X_EXPECTED_MODEL_ID);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "VL6180X detected (model ID 0x%02X)", model_id);

  // 若刚从复位启动(FRESH_OUT_OF_RESET 置位):
  // 必须按 AN4545 "SR03 settings" 写入强制私有寄存器后再清除标志,
  // 否则 VL6180X 不会正常完成测距(中断状态一直 0x00 → 测距超时)。
  if ((this->reg16(REG_SYSTEM_FRESH_OUT_OF_RESET).get() & 0x01) != 0) {
    this->apply_sr03_settings_();
    this->reg16(REG_SYSTEM_FRESH_OUT_OF_RESET) = 0x00;
    ESP_LOGI(TAG, "Applied AN4545 SR03 mandatory settings and cleared fresh-out-of-reset");
  }

  // AN4545 推荐的公共寄存器默认设置
  this->apply_public_settings_();

  // 应用量程缩放(RANGE_SCALER + 校准参数调整)
  this->apply_scaling_();
}

void TOF050CSensor::dump_config() {
  LOG_SENSOR("", "TOF050C", this);
  LOG_UPDATE_INTERVAL(this);
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Range scaler: %ux", this->range_scaler_);
}

void TOF050CSensor::update() {
  // 清除所有挂起中断, 确保干净状态后再启动测量
  this->reg16(REG_SYSTEM_INTERRUPT_CLEAR) = 0x07;

  // 1. 启动单次测距
  this->reg16(REG_SYSRANGE_START) = 0x01;

  // 2. 轮询等待测距完成 (低 3 位 == 0x04), 带超时保护
  uint8_t status = 0;
  uint8_t retries = 0;
  do {
    delay(5);
    status = this->reg16(REG_RESULT_INTERRUPT_STATUS_GPIO).get();
    retries++;
  } while ((status & 0x07) != 0x04 && retries < 50);  // 最长 ~250ms

  // 3. 读取距离值 (原始值, 单位 mm, 需乘以缩放系数)
  uint8_t range_raw = this->reg16(REG_RESULT_RANGE_VAL).get();

  // 4. 清除中断, 准备下一次测量
  this->reg16(REG_SYSTEM_INTERRUPT_CLEAR) = 0x07;

  if ((status & 0x07) != 0x04) {
    // 诊断: 读取 range 状态错误码(高 4 位)帮助定位硬件/软件问题
    uint8_t range_status = this->reg16(REG_RESULT_RANGE_STATUS).get() >> 4;
    ESP_LOGW(TAG, "Measurement timeout or no result (int_status=0x%02X, range_status=%u)", status, range_status);
    this->publish_state(NAN);
    this->status_set_warning();
    return;
  }

  float distance_mm = static_cast<float>(range_raw) * this->range_scaler_;
  ESP_LOGD(TAG, "Distance: %.0f mm (raw=%u, scaler=%ux)", distance_mm, range_raw, this->range_scaler_);
  this->publish_state(distance_mm);
  this->status_clear_warning();
}

// AN4545 "SR03 settings - Mandatory: private registers" (per Pololu VL6180X lib)
// 首次上电(FRESH_OUT_OF_RESET 置位)时必须写入, 否则测距功能无法正常工作
void TOF050CSensor::apply_sr03_settings_() {
  const struct {
    uint16_t reg;
    uint8_t val;
  } settings[] = {
      {0x0207, 0x01}, {0x0208, 0x01}, {0x00E3, 0x01}, {0x00E4, 0x03}, {0x00E5, 0x02},
      {0x00E6, 0x01}, {0x00E7, 0x03}, {0x00F5, 0x02}, {0x00D9, 0x05}, {0x00DB, 0xCE},
      {0x00DC, 0x03}, {0x00DD, 0xF8}, {0x009F, 0x00}, {0x00A3, 0x3C}, {0x00B7, 0x00},
      {0x00BB, 0x3C}, {0x00B2, 0x09}, {0x00CA, 0x09}, {0x0198, 0x01}, {0x01B0, 0x17},
      {0x01AD, 0x00}, {0x00FF, 0x05}, {0x0100, 0x05}, {0x0199, 0x05}, {0x01A6, 0x1B},
      {0x01AC, 0x3E}, {0x01A7, 0x1F}, {0x0030, 0x00},
  };
  for (const auto &s : settings) {
    this->reg16(s.reg) = s.val;
  }
}

// AN4545 推荐的公共寄存器默认值 (per Pololu VL6180X configureDefault)
void TOF050CSensor::apply_public_settings_() {
  this->reg16(0x010A) = 0x30;  // READOUT__AVERAGING_SAMPLE_PERIOD
  this->reg16(0x003F) = 0x46;  // SYSALS__ANALOGUE_GAIN
  this->reg16(0x0031) = 0xFF;  // SYSRANGE__VHV_REPEAT_RATE
  uint8_t als_period[2] = {0x00, 0x63};
  this->write_register16(0x0040, als_period, 2);  // SYSALS__INTEGRATION_PERIOD = 99
  this->reg16(0x002E) = 0x01;  // SYSRANGE__VHV_RECALIBRATE
  this->reg16(0x001B) = 0x09;  // SYSRANGE__INTERMEASUREMENT_PERIOD
  this->reg16(0x003E) = 0x31;  // SYSALS__INTERMEASUREMENT_PERIOD
  this->reg16(0x0014) = 0x24;  // SYSTEM__INTERRUPT_CONFIG_GPIO (range 中断使能)
  this->reg16(0x001C) = 0x31;  // SYSRANGE__MAX_CONVERGENCE_TIME (49ms)
  this->reg16(0x2A3) = 0x00;   // INTERLEAVED_MODE__ENABLE
}

// 应用量程缩放: RANGE_SCALER + 校准参数缩放 (per Pololu setScaling)
void TOF050CSensor::apply_scaling_() {
  uint8_t scaling = (this->range_scaler_ >= 1 && this->range_scaler_ <= 3) ? this->range_scaler_ : 1;

  // RANGE_SCALER (16bit, 大端)
  uint16_t scaler = VL6180X_SCALER_VALUES[scaling - 1];
  uint8_t scaler_buf[2] = {static_cast<uint8_t>(scaler >> 8), static_cast<uint8_t>(scaler & 0xFF)};
  this->write_register16(REG_RANGE_SCALER, scaler_buf, 2);

  // 缩放会影响校准参数: 偏移和 CrosstalkValidHeight 需除以缩放
  int8_t ptp_offset = this->reg16(REG_SYSRANGE_PART_TO_PART_RANGE_OFFSET).get();
  this->reg16(REG_SYSRANGE_PART_TO_PART_RANGE_OFFSET) = static_cast<uint8_t>(ptp_offset / scaling);
  this->reg16(0x0021) = static_cast<uint8_t>(20 / scaling);  // SYSRANGE__CROSSTALK_VALID_HEIGHT

  // 仅 1x 缩放使能 early convergence estimate (RANGE_CHECK_ENABLES bit0)
  uint8_t rce = this->reg16(0x002D).get();  // SYSRANGE__RANGE_CHECK_ENABLES
  this->reg16(0x002D) = (rce & 0xFE) | (scaling == 1 ? 0x01 : 0x00);
}

}  // namespace tof050c
}  // namespace esphome
