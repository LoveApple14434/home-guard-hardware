#include "sp3t_lock.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sp3t_lock {

static const char *const TAG = "sp3t_lock";

void SP3TLock::setup() {
  ESP_LOGCONFIG(TAG, "Setting up SP3T Lock...");
  this->locked_pin_->setup();
  this->unlocked_pin_->setup();
  this->failed_pin_->setup();

  // 初始化状态指示灯输出, 上电全部熄灭
  GPIOPin *led_pins[] = {this->led_locked_pin_, this->led_unlocked_pin_, this->led_failed_pin_};
  for (GPIOPin *pin : led_pins) {
    if (pin != nullptr) {
      pin->setup();
      pin->digital_write(false);
    }
  }

  this->last_pos_ = this->read_position();
  if (this->last_pos_ == POS_UNKNOWN)
    this->last_pos_ = POS_LOCKED;  // 上电/中间态: 安全默认锁定
  this->apply_position(this->last_pos_);
}

void SP3TLock::loop() {
  uint32_t now = millis();
  SwitchPos pos = this->read_position();
  if (pos == POS_UNKNOWN)
    return;  // 开关在切换中的中间态: 保持上次状态, 避免误报

  if (pos != this->last_pos_) {
    if (this->debounce_until_ == 0)
      this->debounce_until_ = now + 20;  // 20ms 机械去抖
    if (now >= this->debounce_until_) {
      this->last_pos_ = pos;
      this->apply_position(pos);
      this->debounce_until_ = 0;
    }
  } else {
    this->debounce_until_ = 0;
  }
}

SwitchPos SP3TLock::read_position() {
  // 公共端接 3.3V + 内部下拉: 拨到哪个位置, 对应引脚读到 HIGH
  if (this->locked_pin_->digital_read())
    return POS_LOCKED;
  if (this->unlocked_pin_->digital_read())
    return POS_UNLOCKED;
  if (this->failed_pin_->digital_read())
    return POS_FAILED;
  return POS_UNKNOWN;
}

void SP3TLock::apply_position(SwitchPos pos) {
  switch (pos) {
    case POS_LOCKED:
      this->publish_state(lock::LOCK_STATE_LOCKED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("锁定");
      ESP_LOGI(TAG, "Lock state -> LOCKED (switch position 1)");
      break;
    case POS_UNLOCKED:
      this->publish_state(lock::LOCK_STATE_UNLOCKED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("解锁成功");
      ESP_LOGI(TAG, "Lock state -> UNLOCKED (switch position 2)");
      break;
    case POS_FAILED:
      this->publish_state(lock::LOCK_STATE_JAMMED);
      if (this->status_sensor_ != nullptr)
        this->status_sensor_->publish_state("解锁失败");
      ESP_LOGI(TAG, "Lock state -> JAMMED (switch position 3, unlock failed)");
      break;
    default:
      break;
  }
  this->update_leds(pos);
}

// 点亮当前状态对应的 LED, 熄灭其余两个
void SP3TLock::update_leds(SwitchPos pos) {
  if (this->led_locked_pin_ != nullptr)
    this->led_locked_pin_->digital_write(pos == POS_LOCKED);
  if (this->led_unlocked_pin_ != nullptr)
    this->led_unlocked_pin_->digital_write(pos == POS_UNLOCKED);
  if (this->led_failed_pin_ != nullptr)
    this->led_failed_pin_->digital_write(pos == POS_FAILED);
}

void SP3TLock::control(const lock::LockCall &call) {
  // 物理 SP3T 开关决定状态; HA 前端操作不改变硬件, 回读开关位置
  ESP_LOGI(TAG, "Control called from frontend, re-reading switch position");
  this->apply_position(this->read_position());
}

}  // namespace sp3t_lock
}  // namespace esphome
