#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/lock/lock.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace sp3t_lock {

// 单刀三掷(SP3T)开关的三个位置
enum SwitchPos : uint8_t {
  POS_UNKNOWN = 0,   // 中间态(无任何位置导通)
  POS_LOCKED = 1,    // 位置1: 锁定
  POS_UNLOCKED = 2,  // 位置2: 解锁成功
  POS_FAILED = 3,    // 位置3: 解锁失败
};

/// 用 SP3T 开关模拟智能门锁: 三个位置分别对应
/// 锁定 / 解锁成功 / 解锁失败(JAMMED), 以原生 lock 实体形式暴露给 HA。
class SP3TLock : public lock::Lock, public Component {
 public:
  void set_locked_pin(GPIOPin *pin) { this->locked_pin_ = pin; }
  void set_unlocked_pin(GPIOPin *pin) { this->unlocked_pin_ = pin; }
  void set_failed_pin(GPIOPin *pin) { this->failed_pin_ = pin; }
  void set_led_locked_pin(GPIOPin *pin) { this->led_locked_pin_ = pin; }
  void set_led_unlocked_pin(GPIOPin *pin) { this->led_unlocked_pin_ = pin; }
  void set_led_failed_pin(GPIOPin *pin) { this->led_failed_pin_ = pin; }
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(const lock::LockCall &call) override;

  SwitchPos read_position();
  void apply_position(SwitchPos pos);
  void update_leds(SwitchPos pos);

  // SP3T 开关输入引脚
  GPIOPin *locked_pin_{nullptr};
  GPIOPin *unlocked_pin_{nullptr};
  GPIOPin *failed_pin_{nullptr};
  // 三色状态指示灯输出引脚(锁定/解锁成功/解锁失败), 高电平点亮
  GPIOPin *led_locked_pin_{nullptr};
  GPIOPin *led_unlocked_pin_{nullptr};
  GPIOPin *led_failed_pin_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};

  SwitchPos last_pos_{POS_UNKNOWN};
  uint32_t debounce_until_{0};
};

}  // namespace sp3t_lock
}  // namespace esphome
