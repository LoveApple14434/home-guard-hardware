#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace tof050c {

/// TOF050C 激光测距模块 (ST VL6180X, I2C 地址 0x29, 16 位寄存器地址)
///
/// 每次 update() 执行一次单次测距:
///   写 0x018=0x01 启动 → 轮询 0x04F 等待完成 → 读 0x062 距离(mm) → 写 0x015=0x07 清中断
class TOF050CSensor : public sensor::Sensor, public PollingComponent, public i2c::I2CDevice {
 public:
  // 量程缩放: 1 / 2 / 3 (对应 RANGE_SCALER 寄存器 0x0000 / 0x00FD / 0x007F)
  void set_range_scaler(uint8_t scaler) { this->range_scaler_ = scaler; }
  // 可选 SHUT 使能引脚(输出高电平使能传感器)
  void set_enable_pin(GPIOPin *pin) { this->enable_pin_ = pin; }

  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // AN4545 "SR03 settings": 首次上电必须写入的强制私有寄存器
  void apply_sr03_settings_();
  // AN4545 推荐的公共寄存器默认值
  void apply_public_settings_();
  // 应用量程缩放(RANGE_SCALER + 校准参数缩放)
  void apply_scaling_();

  uint8_t range_scaler_{1};
  GPIOPin *enable_pin_{nullptr};
};

}  // namespace tof050c
}  // namespace esphome
