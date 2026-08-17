#include "sp3t_lock.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sp3t_lock {

static const char *const TAG = "sp3t_lock";

void SP3TLock::setup() {
  ESP_LOGCONFIG(TAG, "Setting up button lock...");
  this->locked_button_->setup();
  this->unlocked_button_->setup();
  this->failed_button_->setup();

  // 初始化状态指示灯输出, 上电全部熄灭
  GPIOPin *led_pins[] = {this->led_locked_pin_, this->led_unlocked_pin_, this->led_failed_pin_};
  for (GPIOPin *pin : led_pins) {
    if (pin != nullptr) {
      pin->setup();
      pin->digital_write(false);
    }
  }

  // 上电默认锁定(安全默认)
  this->apply_position(POS_LOCKED);
}

void SP3TLock::loop() {
  uint32_t now = millis();

  // 三个独立按钮: 检测按下边沿(低→高)并触发对应状态
  this->handle_button(this->locked_button_, this->locked_last_, this->locked_debounce_, POS_LOCKED, now);
  this->handle_button(this->unlocked_button_, this->unlocked_last_, this->unlocked_debounce_, POS_UNLOCKED, now);
  this->handle_button(this->failed_button_, this->failed_last_, this->failed_debounce_, POS_FAILED, now);
}

// 检测按钮按下(上升沿)并触发对应状态, 带 30ms 去抖防机械抖动
void SP3TLock::handle_button(GPIOPin *pin, bool &last_state, uint32_t &debounce_until, LockPos pos, uint32_t now) {
  if (pin == nullptr)
    return;
  bool pressed = pin->digital_read();
  if (pressed && !last_state && now >= debounce_until) {
    this->apply_position(pos);
    debounce_until = now + 30;  // 30ms 去抖窗口, 防止一次按下重复触发
  }
  last_state = pressed;
}

void SP3TLock::apply_position(LockPos pos) {
  this->logic_pos_ = pos;
  switch (pos) {
    case POS_LOCKED:
      this->publish_state(lock::LOCK_STATE_LOCKED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("锁定");
      ESP_LOGI(TAG, "Lock state -> LOCKED (button 1)");
      break;
    case POS_UNLOCKED:
      this->publish_state(lock::LOCK_STATE_UNLOCKED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("解锁成功");
      ESP_LOGI(TAG, "Lock state -> UNLOCKED (button 2)");
      break;
    case POS_FAILED:
      this->publish_state(lock::LOCK_STATE_JAMMED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("解锁失败");
      ESP_LOGI(TAG, "Lock state -> JAMMED (button 3, unlock failed)");
      break;
    default:
      break;
  }
  this->update_leds(this->logic_pos_);
}

// 点亮当前状态对应的 LED, 熄灭其余两个
void SP3TLock::update_leds(LockPos pos) {
  if (this->led_locked_pin_ != nullptr)
    this->led_locked_pin_->digital_write(pos == POS_LOCKED);
  if (this->led_unlocked_pin_ != nullptr)
    this->led_unlocked_pin_->digital_write(pos == POS_UNLOCKED);
  if (this->led_failed_pin_ != nullptr)
    this->led_failed_pin_->digital_write(pos == POS_FAILED);
}

void SP3TLock::control(const lock::LockCall &call) {
  // HA 前端操作: 点 lock → 锁定; 点 unlock → 解锁成功
  auto state = call.get_state();
  if (!state.has_value())
    return;
  switch (*state) {
    case lock::LOCK_STATE_LOCKED:
      ESP_LOGI(TAG, "Control from frontend: LOCK");
      this->apply_position(POS_LOCKED);
      break;
    case lock::LOCK_STATE_UNLOCKED:
      ESP_LOGI(TAG, "Control from frontend: UNLOCK");
      this->apply_position(POS_UNLOCKED);
      break;
    default:
      ESP_LOGI(TAG, "Control: unsupported lock state %d, ignored", *state);
      break;
  }
}

}  // namespace sp3t_lock
}  // namespace esphome
