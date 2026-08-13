"""TOF050C 激光测距模块 —— sensor 平台

用法(在 esp32-s3-ha.yaml 中):
    i2c:
      - id: tof_i2c
        sda: GPIO8
        scl: GPIO9
        scan: true

    sensor:
      - platform: tof050c
        i2c_id: tof_i2c
        name: "ToF Distance"
        id: tof_distance
        unit_of_measurement: "mm"
        update_interval: 200ms
        range_scaler: 1          # 1x / 2x / 3x, 默认 1x
        # enable_pin: ...        # 可选: SHUT 使能脚(GPIO output), 默认接 3.3V 拉高
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c, sensor
from esphome.const import (
    ICON_ARROW_EXPAND_VERTICAL,
    STATE_CLASS_MEASUREMENT,
    UNIT_MILLIMETER,
)

DEPENDENCIES = ["i2c"]

tof050c_ns = cg.esphome_ns.namespace("tof050c")
TOF050CSensor = tof050c_ns.class_(
    "TOF050CSensor", sensor.Sensor, cg.PollingComponent, i2c.I2CDevice
)

CONF_RANGE_SCALER = "range_scaler"
CONF_ENABLE_PIN = "enable_pin"

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        TOF050CSensor,
        unit_of_measurement=UNIT_MILLIMETER,
        icon=ICON_ARROW_EXPAND_VERTICAL,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            # 量程缩放: 1x(默认, ~20cm 最准) / 2x(~40cm) / 3x(~60cm, 精度下降)
            cv.Optional(CONF_RANGE_SCALER, default=1): cv.one_of(1, 2, 3, int=True),
            # 可选 SHUT 使能引脚(输出高电平使能; 不配则默认模块上电即启用, 需硬件把 SHUT 接 3.3V)
            cv.Optional(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.polling_component_schema("200ms"))
    .extend(i2c.i2c_device_schema(0x29))
)


async def to_code(config):
    """生成 sensor 平台代码"""
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    cg.add(var.set_range_scaler(config[CONF_RANGE_SCALER]))
    if CONF_ENABLE_PIN in config:
        enable = await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])
        cg.add(var.set_enable_pin(enable))

    await i2c.register_i2c_device(var, config)
