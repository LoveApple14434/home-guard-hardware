"""SP3T 开关模拟智能门锁 —— lock 平台

用法(在 esp32-s3-ha.yaml 中):
    lock:
      - platform: sp3t_lock
        id: door_lock
        name: "Front Door Lock"
        locked_pin:   {number: GPIO35, mode: {input: true, pulldown: true}}
        unlocked_pin: {number: GPIO36, mode: {input: true, pulldown: true}}
        failed_pin:   {number: GPIO37, mode: {input: true, pulldown: true}}
        status:
          name: "Front Door Lock Status"
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import lock, text_sensor
from esphome.const import CONF_ID

from . import (
    CONF_FAILED_BUTTON,
    CONF_LED_FAILED_PIN,
    CONF_LED_LOCKED_PIN,
    CONF_LED_UNLOCKED_PIN,
    CONF_LOCKED_BUTTON,
    CONF_STATUS,
    CONF_UNLOCKED_BUTTON,
    SP3TLock,
)

AUTO_LOAD = ["text_sensor"]

CONFIG_SCHEMA = (
    lock.lock_schema(SP3TLock)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        {
            # 三个独立按钮(按下时读到高电平, 瞬时触发)
            cv.Required(CONF_LOCKED_BUTTON): pins.gpio_input_pin_schema,
            cv.Required(CONF_UNLOCKED_BUTTON): pins.gpio_input_pin_schema,
            cv.Required(CONF_FAILED_BUTTON): pins.gpio_input_pin_schema,
            cv.Optional(CONF_LED_LOCKED_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_LED_UNLOCKED_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_LED_FAILED_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
        }
    )
)


async def to_code(config):
    """生成 lock 平台代码"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)

    for key, setter in (
        (CONF_LOCKED_BUTTON, var.set_locked_button),
        (CONF_UNLOCKED_BUTTON, var.set_unlocked_button),
        (CONF_FAILED_BUTTON, var.set_failed_button),
        (CONF_LED_LOCKED_PIN, var.set_led_locked_pin),
        (CONF_LED_UNLOCKED_PIN, var.set_led_unlocked_pin),
        (CONF_LED_FAILED_PIN, var.set_led_failed_pin),
    ):
        if key in config:
            pin = await cg.gpio_pin_expression(config[key])
            cg.add(setter(pin))

    if CONF_STATUS in config:
        ts = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(var.set_status_sensor(ts))
