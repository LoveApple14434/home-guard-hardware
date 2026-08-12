# ZW101 指纹识别模块集成指南

## 1. 硬件接线

### UART 接线表

| 信号 | ZW101 | ESP32-S3 |
|------|--------|----------|
| VCC  | VCC    | 5V / 3.3V (参考模块规格) |
| GND  | GND    | GND      |
| TX   | RX     | GPIO19 (UART1 RX) |
| RX   | TX     | GPIO20 (UART1 TX) |

> ⚠️ **重要**: ZW101 模块供电请参考模块说明书。多数模块可用 3.3V 或 5V 供电。

### 引脚说明

- **GPIO19 (RX)**：接收来自 ZW101 的数据
- **GPIO20 (TX)**：发送数据给 ZW101
- 波特率：**57600 bps**（标准配置，部分模块支持 9600）

## 2. 软件配置

主配置文件 `esp32-s3-ha.yaml` 已包含：

```yaml
# UART 配置
uart:
  - id: zw101_uart
    tx_pin: GPIO44
    rx_pin: GPIO43
    baud_rate: 57600

# ZW101 组件
zw101:
  id: zw101_reader
  uart_id: zw101_uart
```

### 修改接线的步骤

若你的接线与上述不同，请修改 `esp32-s3-ha.yaml` 中的 `tx_pin` 和 `rx_pin`：

```yaml
uart:
  - id: zw101_uart
    tx_pin: GPIO20        # 改成你的 TX 引脚
    rx_pin: GPIO19        # 改成你的 RX 引脚
    baud_rate: 57600
```

**常见空闲引脚（未被摄像头/DHT/烟雾传感器占用）**：
- GPIO3, GPIO19, GPIO20, GPIO22, GPIO25, GPIO26, GPIO27, GPIO28, GPIO29, GPIO30
- GPIO39, GPIO40, GPIO41, GPIO42, GPIO45, GPIO46, GPIO47

## 3. Home Assistant 集成

编译上传后，ZW101 将在 HA 中自动发现为以下实体：

### Binary Sensor（二进制传感器）
- **entity_id**: `binary_sensor.fingerprint_matched`
- **状态**: `on` = 指纹匹配成功，`off` = 无匹配
- **自动清除**: 匹配成功 1 秒后自动清除状态

### Sensor（传感器）
- **Match Score**: `sensor.fingerprint_match_score` - 匹配得分（0~255）
- **Match ID**: `sensor.fingerprint_match_id` - 匹配的指纹 ID（0~999）

### Text Sensor（文本传感器）
- **Status**: `text_sensor.fingerprint_reader_status` - 实时状态信息
  - `"Module Online"` - 模块在线
  - `"Enrolling..."` - 正在注册指纹
  - `"Place finger (sample 1/5)"` - 待用户按压手指
  - `"Enroll Success (ID: X)"` - 注册成功
  - `"Ready"` - 就绪状态
  - `"Match: ID X (Score: Y)"` - 匹配成功

### Switch（开关）
- **Enroll**: `switch.enroll_new_fingerprint` - 注册新指纹
  - 打开开关 → 进入注册模式 → 按照提示按压手指 5 次
  - 自动完成后关闭
  
- **Clear**: `switch.clear_fingerprint_library` - 清空指纹库
  - 打开开关 → 清空所有已注册指纹
  - 自动完成后关闭

## 4. 使用流程

### 4.1 注册指纹

1. 在 HA 中打开 **"Enroll New Fingerprint"** 开关
2. 观察 **"Fingerprint Reader Status"** 文本传感器的提示
3. 按照提示依次将同一根手指按压在 ZW101 模块上 **5 次**
4. 每次按压后需移开手指，等待下一步提示
5. 自动完成注册后，开关会关闭，状态显示 `"Enroll Success (ID: X)"`

**日志输出示例**：
```
[I][zw101:xxx] Starting fingerprint enrollment
[I][zw101:xxx] Place finger (sample 1/5)
[I][zw101:xxx] Sample 1 captured
...
[I][zw101:xxx] Fingerprint enrolled successfully as ID 0
```

### 4.2 验证指纹

1. 将已注册的手指按在 ZW101 模块上
2. 系统自动进行识别（每秒扫描一次）
3. 识别成功时：
   - **Binary Sensor** `fingerprint_matched` → `on`
   - **Match ID** 显示指纹 ID
   - **Match Score** 显示匹配得分
   - **Status** 显示 `"Match: ID X (Score: Y)"`
4. 1 秒后状态自动清除

### 4.3 清空指纹库

1. 在 HA 中打开 **"Clear Fingerprint Library"** 开关
2. 等待完成（通常 < 1 秒）
3. 所有已注册指纹被删除
4. 开关自动关闭，状态显示 `"Library Cleared"`

## 5. Home Assistant 自动化示例

### 5.1 指纹识别 → 打开灯光

```yaml
automation:
  - alias: "Fingerprint Open Light"
    trigger:
      - platform: state
        entity_id: binary_sensor.fingerprint_matched
        to: "on"
    action:
      - service: light.turn_on
        target:
          entity_id: light.living_room
        data:
          brightness: 255
```

### 5.2 根据不同指纹 ID 识别用户

```yaml
automation:
  - alias: "Recognize Family Members"
    trigger:
      - platform: state
        entity_id: sensor.fingerprint_match_id
    action:
      - choose:
          - conditions:
              - condition: template
                value_template: "{{ trigger.to_state.state == '0' }}"
            sequence:
              - service: notify.all
                data:
                  message: "爸爸回家了"
          - conditions:
              - condition: template
                value_template: "{{ trigger.to_state.state == '1' }}"
            sequence:
              - service: notify.all
                data:
                  message: "妈妈回家了"
          - conditions:
              - condition: template
                value_template: "{{ trigger.to_state.state == '2' }}"
            sequence:
              - service: notify.all
                data:
                  message: "孩子回家了"
```

### 5.3 指纹开锁

```yaml
automation:
  - alias: "Fingerprint Door Unlock"
    trigger:
      - platform: state
        entity_id: binary_sensor.fingerprint_matched
        to: "on"
    condition:
      - condition: template
        value_template: "{{ state_attr('sensor.fingerprint_match_id', 'state') | int < 5 }}"
    action:
      - service: lock.unlock
        target:
          entity_id: lock.front_door
      - service: notify.mobile_app
        data:
          message: "门禁已打开 (指纹 ID: {{ states('sensor.fingerprint_match_id') }})"
```

### 5.4 定时清空指纹库（可选）

```yaml
automation:
  - alias: "Weekly Clear Fingerprint Library"
    trigger:
      - platform: time
        at: "01:00:00"  # 每天 01:00
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.clear_fingerprint_library
```

## 6. 故障排除

### 问题 1：编译报错 `Component 'zw101' not found`

**原因**：External Component 路径配置错误

**解决方案**：
1. 检查 `esp32-s3-ha.yaml` 中 `external_components` 的 `path` 是否正确
2. 确保 `components/zw101/` 目录存在
3. 确保 `components/zw101/__init__.py` 文件存在
4. 清理缓存：`.venv\Scripts\python.exe -m esphome clean esp32-s3-ha.yaml`

### 问题 2：UART 通信失败 / 握手错误

**现象**：日志显示 `Handshake failed` 或无法连接模块

**原因**：
- UART 波特率不匹配
- 硬件接线错误（TX/RX 接反）
- ZW101 模块未上电或损坏

**解决方案**：
1. 检查接线：确认 ZW101 TX ↔ ESP32-S3 RX (GPIO43)，ZW101 RX ↔ ESP32-S3 TX (GPIO44)
2. 验证波特率：ZW101 默认 57600（部分旧版本 9600），查看模块手册
3. 检查供电：ZW101 VCC 是否接到正确的电压（3.3V 或 5V，参考模块说明）
4. 用串口调试工具测试模块是否正常（如 USB-TTL 转接 + 串口助手）

### 问题 3：指纹识别失败 / 匹配得分为 0

**原因**：
- 手指按压不完整或力度不够
- ZW101 镜片脏污
- 指纹库中无相应指纹

**解决方案**：
1. 重新注册指纹，每次按压时确保手指完全覆盖传感器
2. 用干布擦拭 ZW101 镜片（勿用水）
3. 检查 `sensor.fingerprint_match_id` 是否为 0（无有效匹配）

### 问题 4：日志无输出或设备不响应

**现象**：ZW101 无任何反应，日志无关键信息

**原因**：
- UART 引脚被其他组件占用
- GPIO43/GPIO44 与摄像头或其他模块冲突

**解决方案**：
1. 在 `esp32-s3-ha.yaml` 中改用其他空闲引脚（见「硬件接线」部分）
2. 检查 GPIO 占用情况（参考 README 摄像头接线表）
3. 临时禁用其他 UART 设备测试

## 7. 技术信息

### ZW101 UART 协议

数据包格式：
```
[0xEF 0x01] [地址4字节] [包标识] [长度2字节] [指令码] [参数...] [校验和2字节]
```

主要指令码：
| 指令 | 代码 | 用途 |
|------|------|------|
| CMD_GET_IMAGE | 0x01 | 获取指纹图像 |
| CMD_GEN_CHAR | 0x02 | 生成特征文件 |
| CMD_MATCH | 0x03 | 精确比对指纹 |
| CMD_SEARCH | 0x04 | 搜索指纹库 |
| CMD_REG_MODEL | 0x05 | 合并特征 |
| CMD_STORE_CHAR | 0x06 | 存储模板 |
| CMD_CLEAR_LIB | 0x0D | 清空指纹库 |
| CMD_READ_SYSPARA | 0x0F | 读系统参数 |
| CMD_HANDSHAKE | 0x35 | 握手 |
| CMD_RGB_CTRL | 0x3C | RGB 灯控制 |

### 指纹 ID 范围

- 有效 ID：0 ~ 999（共 1000 个）
- 自动递增注册（从 0 开始）
- 清空库后重置为 0

### 匹配得分

- **范围**：0 ~ 255
- **阈值**：通常 > 50 表示有效匹配
- **精度**：得分越高，匹配度越高

## 8. 相关文件

- **主配置**：`esp32-s3-ha.yaml` - ESPHome 配置文件
- **组件代码**：
  - `components/zw101/__init__.py` - 主组件（Python）
  - `components/zw101/binary_sensor.py` - 二进制传感器平台
  - `components/zw101/sensor.py` - 传感器平台
  - `components/zw101/text_sensor.py` - 文本传感器平台
  - `components/zw101/switch.py` - 开关平台
  - `components/zw101/zw101.h` - C++ 头文件
  - `components/zw101/zw101.cpp` - C++ 实现

- **参考项目**：https://github.com/14790897/ZW101_ESPHOME_FINGERPRINT

## 9. 常见问题 FAQ

**Q: 可以同时连接多个 ZW101 模块吗？**  
A: 可以。在 YAML 中创建多个 UART 和 ZW101 组件实例即可。

**Q: ZW101 模块支持多少枚指纹？**  
A: 标准支持 1000 枚指纹（ID: 0-999）。

**Q: 如何重置 ZW101 模块？**  
A: 通过「清空指纹库」开关清除所有指纹。模块自身无硬复位脚。

**Q: 手指识别被拒是什么原因？**  
A: 通常因为手指按压不完整、镜片脏污或指纹不清晰。重新注册后可改善。

---

**更新日期**：2026-08-10  
**支持版本**：ESPHome 2026.7.3+ / ESP32-S3
