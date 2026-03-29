# 项目长期记忆

## 项目概述
- **项目**：LoRa Meshtastic 固件开发
- **主代码目录**：`code/firmware-2.7.15.567b8ea`（基于 Meshtastic 官方固件 v2.7.15）
- **构建系统**：PlatformIO + ESP-IDF (Arduino framework for ESP32)

## 硬件平台 & 自定义板卡
项目有多个自研板卡（均为 ESP32-C3 + LoRa 模块组合）：

### esp32c3_moonshine（基础版）
- MCU：ESP32-C3（esp32-c3-devkitm-1）
- LoRa：E220-400M30S（LLCC68/SX1262/SX1268 三选一探测）
- 无屏幕、无 GPS
- SPI: SCK=10, MISO=6, MOSI=7, CS=8, RST=5, DIO1=3, BUSY=4
- LED_POWER=12，DIO3 TCXO 1.8V，TCXO_OPTIONAL
- Flash: 4MB, dio 模式, 80MHz

### esp32c3_moonshine（firmware 版本，variants 中）
- 同上基础版，增加：
  - 支持 SX126X_RXEN=2（RX 使能 GPIO）
  - 支持 E22_400M33S（SX126X_MAX_POWER=22, TX_GAIN=0）
  - BATTERY_PIN=1, ADC_MULTIPLIER=2.0f
  - USB CDC 启动：ARDUINO_USB_MODE=1, ARDUINO_USB_CDC_ON_BOOT=1

### esp32c3_moonshine_mv（带屏幕+GPS+多按键版）
- MCU：ESP32-C3
- LoRa：RA-01SC-P（SETTING_MAX_POWER=29, TX_GAIN=26, SX126X_MAX_POWER=3）
- 屏幕：SSD1306（I2C: SDA=0, SCL=1）
- GPS：GPS_RX=21, GPS_TX=20, GPS_POWER_TOGGLE, PIN_GPS_EN=12
- 多按键：PCF8574 IO 扩展器（地址 0x27，INT=9），映射 SELECT/UP/DOWN/LEFT/RIGHT/CANCEL
- NeoPixel：GPIO13, 1颗 NEO_GRB
- BATTERY_PIN=2

### esp32c3_moonshine_travelers（旅行者版）
- MCU：ESP32-C3
- LoRa：RA-01SC-P（SETTING_MAX_POWER=3, TX_GAIN=0, SX126X_MAX_POWER=3）
- 屏幕：SH1106（I2C: SDA=0, SCL=1）
- GPS：GPS_RX=21, GPS_TX=20, GPS_RST=P1.6（TCA9535）, GPS_EN=P1.7（TCA9535）
  - GPS EN 通过 `GpioTca9535GpsEnPin` + `GpioUnaryTransformer` 桥接 `gps->enablePin`
  - GPS RST 由 `tca9535GpsReset()` 控制，init 中释放（高电平）
- BATTERY_PIN=2
- **GPIO9**：短按=SELECT，长按=无功能（`BUTTON_DISABLE_LONG_PRESS`）
- **TCA9535PWR 4×4 矩阵键盘**：与屏幕共用 I2C，A0=A1=A2=0，地址 0x20
  - 驱动：`src/input/TCA9535ButtonThread.h/.cpp`
  - 矩阵接法：P0.0~P0.3 行输出（ROW0~ROW3），P0.4~P0.7 列输入（COL0~COL3）
  - 扫描方式：逐行拉低输出，读列检测低电平，50µs 行间延时
  - 中断引脚：TCA9535_INT_PIN=5（GPIO5，低电平有效，下降沿触发）
  - 映射（variant.h）：key0-2=索引1-3, key4-6=索引4-6, key8-10=索引7-9, key12=索引10(*), key13=索引11(0), key14=索引12(#)；key3=UP, key7=DOWN, key11=LEFT, key15=RIGHT
  - **kbchar 字段语义**：
    - RAK14004 传 1-based 索引（kbchar=1..16），走 canned message 选择路径
    - TCA9535 传 ASCII 字符（'0'-'9','*','#'），走文本输入路径（INACTIVE→FREETEXT）
    - `CannedMessageModule` 的 MATRIXKEY 拦截：可打印 ASCII 不拦截（fall through），索引才拦截
    - 不能传 ASCII 给索引路径！ASCII '0'=48 → index=47 → 数组越界 → 崩溃
  - SELECT 由 GPIO9 短按处理，CANCEL 由 POWER_BOOT 短按处理
  - **P1.2 = POWER_EN**：高电平有效，驱动 MOS 管维持供电
    - `tca9535PowerEn(bool on)` 静态函数，read-modify-write P1.2
  - **P1.3 = POWER_BOOT**：输入，低电平有效（按键按下接地）
    - 开机：物理按键 → MOS → ESP32 得电 → **main.cpp Wire.begin() 后立即调用 `tca9535PowerEn(true)` 锁住供电**
    - ⚠️ 曾经的设计错误：在 `tca9535ButtonThread::init()` 里等 P1.3 按住 2s 再拉高 POWER_EN，但 setup() 需要数秒初始化，用户早已松手导致 MOS 断电。已修复为在 Wire.begin() 后立即上锁。
    - 短按：派发 INPUT_BROKER_CANCEL
    - 长按 2s：派发 INPUT_BROKER_SHUTDOWN（走系统关机流程 → Power::shutdown() → POWER_EN 拉低）
    - `tca9535ReadPowerBoot()` 静态函数读取 P1.3 状态
  - **P1.4 = LoRa RST**：通过 TCA9535GpioHal 自定义 HAL 拦截虚拟引脚 200 转发到 I²C
  - `tca9535LoraReset(bool high)` 静态函数，read-modify-write P1.4
  - `LORA_RESET = TCA9535_LORA_RST_VIRTUAL_PIN(200)`，RadioLib 的 `findChip()`/`reset()` 全链路走 I²C
  - **P1.5 = 状态指示灯**：输出，低电平点亮
    - `tca9535StatusLed(bool on)` 静态函数，read-modify-write P1.5
    - init() 中配置为输出，默认熄灭（高电平）
    - 在 TCA9535ButtonThread::runOnce() 中独立驱动，500ms 亮 + 500ms 灭 = 1 秒闪烁
    - 不再跟随 LED_PIN（已从 GpioSplitter 中移除）
  - **P1.0 = 键盘背光**：输出，高电平点亮
    - `TCA9535_BIT_P10` 宏 = `(1u << 0)`
    - `tca9535Backlight(bool on)` 静态函数，read-modify-write P1.0
    - init() 中默认熄灭（低电平）
    - runOnce() 中驱动：按键按下时点亮，刷新 5 秒计时；5 秒无操作自动熄灭
  - **P1.1 = CHARGE_DET**：输入，高电平=正在充电
    - `TCA9535_CHARGE_DET_PIN` 宏 = `(1u << 1)`，在 variant.h 中定义
    - `tca9535ReadChargeDet()` 静态内联函数，读 P1.1 输入寄存器
    - `tca9535IsCharging` 全局 volatile bool，由 runOnce() 每 2 秒轮询更新
    - `Power.cpp` 的 `AnalogBatteryLevel::isCharging()` 和 `isVbusIn()` 均读取此变量
    - P1 config = 0x0A（P1.1=输入 CHARGE_DET, P1.3=输入 POWER_BOOT, 其余输出）
    - ⚠️ 踩坑1：加 CHARGE_DET 后曾将 config 从 0x8B 改为 0x8D，导致 P1.2(POWER_EN) 被误配成输入（高阻），MOS 失控断电。正确值 0x0A
    - ⚠️ 踩坑2：TP4057 充电芯片电压反串导致 P1.1 在未充电时仍读高电平，充电检测误报。需硬件修改解决。
- **关机路径**：所有关机触发最终走 Power::shutdown() → tca9535PowerEn(false) → doDeepSleep()

## 代码架构要点
- **踩坑记录**：TCA9535 矩阵扫描 cols 计算中 `~` 运算符对 uint8_t 会整数提升为 int，导致高 4 位被污染。修复：`((~(p0In & 0xF0)) >> 4) & 0x0F`。见 scanMatrix()。
- 路由层：FloodingRouter → ReliableRouter → NextHopRouter
- 无线接口：RadioLib 抽象层（SX126x/SX128x/LR11x0/RF95）
- 模块系统：`src/modules/` 下各功能模块（TextMessage, Position, Telemetry, etc.）
- Protobuf 消息定义：`src/mesh/generated/` + `protobufs/`
- 关键全局变量：`router`, `service`, `screen`, `gps`, `rIf`

## 自定义修改记录（readme.md）
- 增加 CN 频段定义：`RDEF(CN, 470.0f, 510.0f, 100, 0, SETTING_MAX_POWER, true, false, false)`
- SETTING_MAX_POWER 使用宏保护：`#ifndef SETTING_MAX_POWER / #define SETTING_MAX_POWER 3`

## 构建注意事项
- `platformio.ini` 中 lib_deps 均已固定 commit hash，符合生产规范
- 排除了大量 RadioLib 不用的协议（AX25/LoRaWAN/APRS 等）以节省 flash
- MAX_THREADS=40
- 默认 env：tbeam（需要切换到 esp32c3_moonshine 系列时要指定 env）
- **Flash 占用**（esp32c3_moonshine_travelers）：text≈1.92MB, data≈0.63MB, 总≈2.57MB / 4MB
- **分区表**（partition-table.csv）：app=0x2C0000(2.75MB), OTA=0x030000(192KB), spiffs=0x100000(1MB)
- **中文 12×12 字库**：`src/graphics/fonts/ChineseFont12x12.h`，21075 字形，535KB flash
  - 生成工具：`tools/gen_chinese_font.mjs`（需 npm install @napi-rs/canvas）
  - 覆盖：U+4E00-U+9FFF（CJK 统一汉字全集，含 GB2312 6763 字）
  - 像素判定：透明背景 + 黑色前景，alpha > 128 阈值（去掉抗锯齿毛边，字形锐利）
  - ⚠️ 阈值 40 太低→字体膨胀变粗；128 合适→清晰锐利
  - ⚠️ `imageSmoothingEnabled` 对 canvas fillText 无效
  - 渲染函数：`cfont12_find()` / `cfont12_draw()` / `cfont12_utf8()` / `cfont12_drawStr()`
  - 集成点：CannedMessageModule drawFrame FREETEXT 部分（混合 ASCII+CJK 渲染）
  - MessageRenderer（收到消息显示）也已支持：generateLines + drawStringWithEmotes + calculateLineHeights 均改为 CJK 感知
