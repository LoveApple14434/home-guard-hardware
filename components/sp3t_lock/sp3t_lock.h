#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/lock/lock.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace sp3t_lock {

// 三种门锁状态
enum LockPos : uint8_t {
  POS_LOCKED = 1,    // 锁定
  POS_UNLOCKED = 2,  // 解锁成功
  POS_FAILED = 3,    // 解锁失败
};

/// 用三个独立按钮模拟智能门锁:
///   按钮1 → 锁定 / 按钮2 → 解锁成功 / 按钮3 → 解锁失败(按下触发, 瞬时)
/// 以原生 lock 实体形式暴露给 HA, HA 前端操作(lock/unlock)同样生效。
class SP3TLock : public lock::Lock, public Component {
 public:
  void set_locked_button(GPIOPin *pin) { this->locked_button_ = pin; }
  void set_unlocked_button(GPIOPin *pin) { this->unlocked_button_ = pin; }
  void set_failed_button(GPIOPin *pin) { this->failed_button_ = pin; }
  void set_led_locked_pin(GPIOPin *pin) { this->led_locked_pin_ = pin; }
  void set_led_unlocked_pin(GPIOPin *pin) { this->led_unlocked_pin_ = pin; }
  void set_led_failed_pin(GPIOPin *pin) { this->led_failed_pin_ = pin; }
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  // 自动锁定延时(毫秒): 解锁成功/解锁失败状态保持该时长无操作后自动回到锁定
  void set_auto_lock_timeout(uint32_t timeout_ms) { this->auto_lock_timeout_ = timeout_ms; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(const lock::LockCall &call) override;

  void handle_button(GPIOPin *pin, bool &last_state, uint32_t &debounce_until, LockPos pos, uint32_t now);
  void apply_position(LockPos pos, uint32_t now);
  void update_leds(LockPos pos);
  void start_auto_lock(uint32_t now);

  // 三个独立按钮输入(按下时读到高电平)
  GPIOPin *locked_button_{nullptr};
  GPIOPin *unlocked_button_{nullptr};
  GPIOPin *failed_button_{nullptr};
  bool locked_last_{false};
  bool unlocked_last_{false};
  bool failed_last_{false};
  uint32_t locked_debounce_{0};
  uint32_t unlocked_debounce_{0};
  uint32_t failed_debounce_{0};

  // 三色状态指示灯输出引脚(锁定/解锁成功/解锁失败), 高电平点亮
  GPIOPin *led_locked_pin_{nullptr};
  GPIOPin *led_unlocked_pin_{nullptr};
  GPIOPin *led_failed_pin_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};

  LockPos logic_pos_{POS_LOCKED};  // 当前逻辑门锁状态(上电默认锁定)

  // 自动锁定状态
  bool auto_lock_active_{false};       // 是否正在自动锁定倒计时
  uint32_t auto_lock_start_{0};        // 倒计时起始时刻(ms)
  uint32_t auto_lock_timeout_{5000};   // 自动锁定延时, 默认 5s
};

}  // namespace sp3t_lock
}  // namespace esphome
