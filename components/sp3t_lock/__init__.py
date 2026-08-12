"""SP3T 开关模拟智能门锁组件

用单刀三掷(SP3T)开关的三个位置模拟智能门锁的三种状态:
  - 位置1 → 锁定        (LOCK_STATE_LOCKED)
  - 位置2 → 解锁成功    (LOCK_STATE_UNLOCKED)
  - 位置3 → 解锁失败    (LOCK_STATE_JAMMED)

以原生 lock 实体暴露给 Home Assistant, 并可选附带 text_sensor
显示中文状态(锁定 / 解锁成功 / 解锁失败)。
本文件仅定义命名空间与组件类; 平台配置见同目录下的 lock.py。
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.const import CONF_ID

sp3t_lock_ns = cg.esphome_ns.namespace("sp3t_lock")
SP3TLock = sp3t_lock_ns.class_("SP3TLock", lock.Lock, cg.Component)

CONF_LOCKED_BUTTON = "locked_button"
CONF_UNLOCKED_BUTTON = "unlocked_button"
CONF_FAILED_BUTTON = "failed_button"
CONF_LED_LOCKED_PIN = "led_locked_pin"
CONF_LED_UNLOCKED_PIN = "led_unlocked_pin"
CONF_LED_FAILED_PIN = "led_failed_pin"
CONF_AUTO_LOCK_TIMEOUT = "auto_lock_timeout"
CONF_STATUS = "status"
