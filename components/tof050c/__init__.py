"""TOF050C 激光测距模块组件 (ST VL6180X, I2C)

TOF050C 是基于 ST VL6180X (FlightSense 直接 ToF) 的 I2C 激光测距模块:
  - 量程: 0~50cm (默认 1x 缩放 ~20cm 最准; 2x/3x 扩展到 ~40/60cm)
  - 接口: I2C, 7-bit 地址 0x29 (16 位寄存器地址)
  - 供电: 3.3V~5V, 典型 ~40mA; 引脚 VIN/GND/SDA/SCL/INT/SHUT
  - SHUT 需拉高使能 (低电平关闭)

本文件仅作包入口; 平台配置见同目录下的 sensor.py。
"""
