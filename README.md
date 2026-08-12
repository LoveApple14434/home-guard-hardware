# ESP32-S3 接入 Home Assistant（ESPHome）

基于开源项目 [ESPHome](https://esphome.io) 将 ESP32-S3 接入 Home Assistant，实现：
- 🌡️ 温湿度等传感器数据上报
- � OV7670 摄像头实时画面 / 快照（浏览器 + HA 均可查看）
- �💡 开关 / 继电器 / 灯控制
- 📊 HA 仪表盘展示
- ⚡ 与 HA 自动化联动

---

## 1. 项目结构

```
esp_mqtt_ha/
├── esp32-s3-ha.yaml   # ESPHome 主配置(传感器/开关/网络等)
├── secrets.yaml       # 敏感信息(WiFi密码/API密钥), 勿提交到 git
├── .venv/             # Python 虚拟环境(已装好 esphome)
└── README.md          # 本文件
```

## 2. 准备工作

### 2.1 安装 ESPHome CLI（二选一）

**方式 A：VS Code 扩展（推荐）**
安装 `ESPHome` 扩展后，打开 `esp32-s3-ha.yaml`，点击编辑器右上角的
“安装 / 编译 / 上传”按钮即可。已包含在下方步骤。

> ⚠️ **本项目的关键环境配置**：`esp32-s3-ha.yaml` 中已设置
> `toolchain: platformio`。这是因为 ESPHome 2026.x 默认使用 ESP-IDF
> 原生工具链（会调用系统的 `idf.py`），而本机 `D:\Program\ESP\v5.5` 的
> ESP-IDF 安装不完整（缺 `python_env`/工具链），会导致编译报错
> `No module named 'esp_idf_monitor'`。强制 `platformio` 后走经典构建
> 流程，自动下载 arduino-esp32 工具链，无需 ESP-IDF。若以后想用
> ESP-IDF 工具链，需先完整安装 ESP-IDF 再把该行改成 `toolchain: esp-idf`。

**方式 B：命令行安装**

本工作区已在 `.venv` 中装好 esphome（2026.7.3），直接使用即可：

```bash
# 进入工作区目录后激活虚拟环境
cd d:\personal_programming\MCUs\ESP32\esp_mqtt_ha
.venv\Scripts\activate

esphome version
```

> 如果没有 `.venv`，也可自己安装：`pip install esphome`（Windows 需 Python 3.9+）
> 如果你的 HA 是 Home Assistant OS 或 Supervised 安装，
> 也可以直接在 HA 中安装 **ESPHome 官方加载项（Add-on）**，
> 然后把本项目目录挂载进去编译。二者等价。

### 2.2 填写配置

1. 编辑 `secrets.yaml`，填入你的 **WiFi 名称/密码** 和 **OTA 密码**。
   - `api_key` 已生成好（`JdwZ2/rD2wnesJrvU86Mt9ROyo4Q0x3LhfN9/Exu2pI=`），**记下它**，第 4 步要用。
2. 编辑 `esp32-s3-ha.yaml`，按你的实际接线修改：
   - `board:` 改成你的开发板型号
   - 各传感器 / 开关的 `pin:` 改为实际 GPIO
   - 不需要的组件直接注释掉

### 2.3 接线参考（默认配置）

| 组件     | GPIO | 说明                        |
|----------|------|-----------------------------|
| DHT22    | GPIO4| 数据脚（VCC→3.3V, GND→GND） |
| 烟雾传感器| GPIO1| AO 模拟输出（MQ-2 等；若 AO 为 0~5V 需电阻分压） |
| 按钮门锁 | GPIO38/39/40 + GPIO41/42/47 | 三个按钮模拟门锁 + 三色 LED 指示（见 2.4 节） |
| 板载LED  | GPIO48| 状态指示灯（S3 DevKitC）    |

### 2.4 智能门锁模拟（三个独立按钮）

用**三个独立按钮**模拟智能门锁的三种状态（按钮是**瞬时触发**，按下瞬间生效，松开保持当前状态），无需真实锁体即可在 HA 中测试门锁卡片与自动化：

| 按钮 | GPIO | 门锁状态 | lock 实体状态 | text_sensor 显示 |
|------|------|----------|---------------|------------------|
| 按钮 1（锁定）  | GPIO38 | 锁定     | `locked`      | 锁定 |
| 按钮 2（解锁成功）| GPIO39 | 解锁成功 | `unlocked`    | 解锁成功 |
| 按钮 3（解锁失败）| GPIO40 | 解锁失败 | `jammed`（卡住）| 解锁失败 |

**接线**：

1. 每个按钮**一端接 3.3V**，另一端分别接 **GPIO38 / GPIO39 / GPIO40**
2. 引脚已启用内部下拉：按下时读到高电平即触发对应状态
3. 无需外接电阻（内部下拉即可）

> ⚠️ **不要用 GPIO35/36/37**：这些引脚被 Octal PSRAM 接口占用（你的板子带 8MB Octal
> PSRAM，摄像头帧缓冲必需），使用它们会报 `GPIOxx is used by the PSRAM interface in
> octal mode` 警告。

**三色 LED 状态指示（可选）**：

三个输出引脚各接一个 LED（**高电平点亮**），与当前门锁状态同步显示：

| LED | GPIO | 点亮时机 |
|-----|------|----------|
| LED1（如绿色） | GPIO41 | 锁定 |
| LED2（如蓝色） | GPIO42 | 解锁成功 |
| LED3（如红色） | GPIO47 | 解锁失败 |

接线：`GPIO → 220Ω 限流电阻 → LED 阳极`，`LED 阴极 → GND`。
同一时刻只有一个 LED 点亮（对应的那一盏）。

**自动锁定（Auto-lock）**：解锁成功 / 解锁失败状态**保持 `auto_lock_timeout` 无操作后
自动回到锁定**（模拟真实门锁的自动上锁）；此时 LED 与 lock 实体同步切回"锁定"。
再按一次按钮 2/3 即可重新触发。默认 5s，可在配置里改 `auto_lock_timeout`。

**说明**：HA 前端操作（lock/unlock）同样生效（分别对应锁定/解锁成功）；
"解锁失败"用 lock 的 `jammed`（卡住）状态表示，可在 HA 中用它触发报警等自动化。
对应实体：`lock.front_door_lock`、`text_sensor.front_door_lock_status`。

> ⚠️ 原示例中的继电器（GPIO2/GPIO15）和按钮（GPIO5）引脚已被摄像头占用，
> 因此 `switch:` / `binary_sensor:` 段保持注释状态。若你仍要接继电器/按钮，
> 请先把摄像头对应引脚（GPIO2/GPIO15/GPIO5）换到其它空闲引脚。

### 2.5 摄像头接线（OV7670）

> **前置要求**：OV7670 帧缓冲放在 PSRAM，**必须使用带 PSRAM 的 ESP32-S3 模块**
> （如 `ESP32-S3-WROOM-1-N8R8` / `N16R8`，8MB Octal PSRAM），否则无法工作。
> 供电请用 **3.3V**（OV7670 是 3.3V 器件，切勿接 5V）。

| OV7670 模块引脚 | ESP32-S3 GPIO | 说明 |
|------------------|---------------|------|
| VCC / DOVDD     | 3.3V          | 供电（3.3V，勿接 5V） |
| GND / DGND      | GND           | 地 |
| SIOC            | GPIO14        | SCCB 时钟（I2C SCL） |
| SIOD            | GPIO21        | SCCB 数据（I2C SDA） |
| XCLK            | GPIO15        | 外部时钟输入（20MHz） |
| PCLK            | GPIO13        | 像素时钟输出 |
| VSYNC           | GPIO6         | 帧同步 |
| HREF            | GPIO7         | 行参考 |
| D0~D7           | GPIO11,12,9,8,10,16,17,18 | 8 位并行数据线 |
| PWDN            | GPIO2         | 掉电控制（低电平=正常工作） |
| RESET           | GPIO5         | 复位（高电平=释放复位） |

> 若你的 OV7670 模块没有引出 PWDN/RESET 引脚，把配置里对应的 `power_down_pin` /
> `reset_pin` 两行删掉即可（默认不驱动）。

#### 摄像头使用

- **浏览器直接看**：`http://<设备IP>:8080/`（MJPEG 实时流）、`http://<设备IP>:8080/snapshot`（单张快照）
- **HA 中查看**：接入后会出现 `camera.room_camera` 实体，可加到仪表盘；
  也可在自动化里用 `camera.snapshot` / `camera.turn_off` 等服务。
- **性能说明**：OV7670 没有硬件 JPEG 编码，由 ESP32 软件转码（`pixel_format: RGB565`），
  帧率偏低属正常。默认 `QVGA(320x240)`；想更清晰可改 `VGA(640x480)`，但会更卡。

## 3. 编译并烧录

**方式 A（VS Code 扩展）**：打开 `esp32-s3-ha.yaml`，点击右上角 ▶ 按钮，
选择串口后即可自动编译 + 烧录。

**方式 B（命令行）**：

```bash
# 先激活 .venv(见 2.1 方式B)
cd d:\personal_programming\MCUs\ESP32\esp_mqtt_ha
.venv\Scripts\activate

# 编译
esphome compile esp32-s3-ha.yaml

# 插上 USB 后烧录(会自动选择串口)
esphome run esp32-s3-ha.yaml
```

烧录成功后打开串口日志确认：

```bash
esphome logs esp32-s3-ha.yaml
```

看到 `[INFO] ESPHome` 和 IP 地址即代表联网成功。

> 首次烧录后，以后都可以走无线 OTA 升级，无需再插线：
> ```bash
> esphome upload esp32-s3-ha.yaml
> ```

## 4. 接入 Home Assistant

1. 打开 HA 后台：`http://192.168.100.1:8123/`
2. 进入 **设置 → 设备与服务 → 添加集成 → ESPHome**
3. 输入设备 IP（路由器后台或串口日志中可查），点击提交
4. 输入你在 `secrets.yaml` 里配置的 `api_key`（上面的密钥）
5. 添加成功后，设备会自动出现在 **设置 → 设备与服务 → ESP32 S3 HA**

## 5. 添加到仪表盘

### 5.1 添加卡片

1. 打开 HA 的 **概览（Overview）**
2. 点击右上角 ✏️ 编辑 → 添加卡片
3. 选择实体，即可把以下实体拖到仪表盘：
   - `sensor.living_room_temperature`（温度）
   - `sensor.living_room_humidity`（湿度）
   - `sensor.smoke_sensor_voltage`（烟雾传感器电压）
   - `sensor.smoke_level`（烟雾等级 0~100%）
   - `camera.room_camera`（摄像头画面）
   - `switch.living_room_light`（客厅灯）
   - `switch.outlet_switch`（插座开关）
   - `binary_sensor.front_door_button`（门口按钮）
   - `lock.front_door_lock`（智能门锁——SP3T 开关模拟）
   - `text_sensor.front_door_lock_status`（门锁状态文本：锁定/解锁成功/解锁失败）

### 5.2 改成中文显示名（可选）

> 为什么实体名是英文？新版 ESPHome 会对实体名做 ASCII 化唯一性校验，
> 中文名（如“客厅温度”“客厅湿度”）会全部转成下划线导致冲突而无法编译，
> 这是官方限制。所以配置里用英文名，中文显示在 HA 侧设置即可，效果完全一样。

在 HA 中把实体重命名为中文（一次性，随设备保留）：

1. 进入 **设置 → 设备与服务 → ESP32 S3 HA → 实体**
2. 点击实体右侧的齿轮图标，把“名称”改成中文，例如：
   - `sensor.living_room_temperature` → `客厅温度`
   - `sensor.living_room_humidity` → `客厅湿度`
   - `switch.living_room_light` → `客厅灯`
3. 保存后，仪表盘和自动化里显示的就是中文了

> 提示：也可以写 `customize.yaml` 批量定义中文名，适合实体较多时使用。

## 6. 创建自动化（示例）

在 HA 的 **设置 → 自动化** 中新建：

```yaml
# 示例: 温度超过 30°C 时打开插座
alias: 高温自动降温
trigger:
  - platform: numeric_state
    entity_id: sensor.living_room_temperature
    above: 30
condition: []
action:
  - service: switch.turn_on
    target:
      entity_id: switch.outlet_switch
mode: single
```

## 7. 常用维护命令

```bash
# 查看日志
esphome logs esp32-s3-ha.yaml

# 无线升级固件
esphome upload esp32-s3-ha.yaml

# 清理编译缓存
esphome clean esp32-s3-ha.yaml

# 校验配置(不编译)
esphome config esp32-s3-ha.yaml
```

## 8. 常见问题

| 问题 | 解决方法 |
|------|----------|
| 连不上 WiFi | 检查 `secrets.yaml` 密码；WiFi 仅支持 2.4GHz |
| 烧录失败 | 确认已装 USB 驱动（CP210x/CH340），换 USB 线（部分线只充电） |
| 找不到设备 | 确认 HA 与 ESP32 在同一局域网/网段 |
| API 密钥不匹配 | 删除 HA 中旧集成重新添加，密钥保持一致 |
| DHT 读取失败 | 检查接线与引脚号，DHT 数据线建议加 4.7k~10kΩ 上拉 |
| 摄像头黑屏/无画面 | ① 确认板子带 PSRAM（帧缓冲必需）；② 核对 OV7670 接线（重点：XCLK/PCLK/VSYNC/HREF 与 8 根数据线）；③ 查看串口日志确认 `esp_camera_init` 是否成功 |
| 摄像头帧率低/卡顿 | OV7670 无硬件 JPEG，软件转码属正常；把 `resolution` 降到 QVGA 或调低 `jpeg_quality` |
| 编译报“Duplicate entity” | 实体名用了中文，改成英文名并在 HA 中重命名（见 5.2 节） |

---

## 参考链接

- [ESPHome 官方文档](https://esphome.io)
- [Home Assistant ESPHome 集成](https://www.home-assistant.io/integrations/esphome/)
- [ESP32-S3 板级配置](https://esphome.io/components/esp32/)
