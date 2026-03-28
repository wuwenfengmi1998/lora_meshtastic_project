# Changelog

基于 [Meshtastic 官方固件 v2.7.15](https://github.com/meshtastic/firmware/tree/2.7.15) (commit `567b8ea`)。

格式参考 [Keep a Changelog](https://keepachangelog.com/)。

---

## [Unreleased]

### Added

#### CN 频段支持
- 新增中国 CN 频段定义：470.0–510.0 MHz，100 信道，`SETTING_MAX_POWER` 宏保护默认 3 dBm
- 修改文件：`src/mesh/RadioInterface.cpp`（`RDEF(CN, ...)`）

#### esp32c3_moonshine_travelers 旅行者版 — TCA9535PWR IO 扩展器驱动
- 新增 `src/input/TCA9535ButtonThread.h` / `.cpp`：TCA9535PWR 4×4 矩阵键盘驱动
  - I²C 地址 0x20（A0=A1=A2=0），与 SH1106 屏幕共用 Wire 总线
  - P0.0~P0.3 行输出，P0.4~P0.7 列输入，逐行拉低扫描，50µs 行间延时
  - 支持中断模式（GPIO5 下降沿触发）和轮询模式
  - 默认按键映射：SELECT / UP / DOWN / LEFT / RIGHT / CANCEL（可通过 variant.h 覆盖）
  - `src/input/TCA9535ButtonThread.h` — 类声明、寄存器宏定义、静态工具函数
  - `src/input/TCA9535ButtonThread.cpp` — 矩阵扫描、边沿检测、事件派发

#### 电源管理（P1.2 POWER_EN + P1.3 POWER_BOOT）
- P1.2 = POWER_EN 输出，高电平有效，驱动 MOS 管维持供电
  - `tca9535PowerEn(bool on)` — read-modify-write P1.2，static inline
- P1.3 = POWER_BOOT 输入，低电平有效（按键按下接地）
  - `tca9535ReadPowerBoot()` — 读取 P1.3 状态，static inline
- 开机流程：物理按键 → MOS 导通 → ESP32 得电 → init() 检测 P1.3 持续按住 2 秒 → POWER_EN 拉高维持供电
  - 未按满 2 秒松开 → 不拉高 POWER_EN → MOS 断开 → 自动断电
- 关机流程：运行中 P1.3 持续按住 2 秒 → 清空屏幕 → POWER_EN 拉低 → 用户松手后 MOS 断开断电
- 电源状态机：`BOOT_PENDING` → `RUNNING` → `SHUTDOWN_PENDING`
- P1 口配置：`0xEB`（P1.2=输出, P1.3=输入, P1.4=输出）

#### LoRa RST 通过 TCA9535 P1.4 控制
- 新增 `TCA9535GpioHal` 自定义 HAL 子类（在 `src/main.cpp`）
  - 继承 `LockingArduinoHal`，拦截虚拟引脚 200 的 `pinMode()` / `digitalWrite()` 转发到 I²C
  - `LORA_RESET = TCA9535_LORA_RST_VIRTUAL_PIN(200)`
  - RadioLib 的 `findChip()` / `reset()` 全链路通过 I²C 控制 P1.4
- `tca9535LoraReset(bool high)` — read-modify-write P1.4，static inline

#### variant.h 配置（esp32c3_moonshine_travelers）
- `HAS_TCA9535_BUTTON` — 启用 TCA9535 按键驱动
- `TCA9535_INT_PIN 5` — 中断引脚
- `TCA9535_POWER_EN_BIT (1u << 2)` — 电源使能位掩码
- `TCA9535_KEY_MAP { ... }` — 4×4 矩阵按键映射
- `TCA9535_LORA_RST_VIRTUAL_PIN 200` — LoRa RST 虚拟引脚

#### main.cpp 集成
- `#ifdef HAS_TCA9535_BUTTON` 条件编译包含 TCA9535ButtonThread.h 并实例化
- 在 `setupModules()` 后调用 `tca9535ButtonThread->init()`
- `#ifdef TCA9535_LORA_RST_VIRTUAL_PIN` 条件编译使用 `TCA9535GpioHal` 作为 RadioLib HAL

### Changed

#### 按键映射更新（key3/key7/key11/key15 = 方向键）
- 矩阵按键映射从 `key1=UP, key2=DOWN, key3=LEFT, key4=RIGHT` 改为 `key3=UP, key7=DOWN, key11=LEFT, key15=RIGHT`
- 方向键全部位于 COL3 列（key3=ROW0·COL3, key7=ROW1·COL3, key11=ROW2·COL3, key15=ROW3·COL3）
- SELECT 由 GPIO9 短按处理，CANCEL 由 POWER_BOOT(P1.3) 短按处理

#### P1.5 状态灯改为独立 1 秒闪烁
- P1.5 状态灯从 `GpioSplitter` + `LED_PIN` 同步驱动改为独立驱动
- 在 `TCA9535ButtonThread::runOnce()` 中每 500ms 翻转（亮 500ms + 灭 500ms = 1 秒周期）
- 移除 `Led.cpp` 中的 `GpioTca9535LedPin` 类及相关 `#ifdef TCA9535_LORA_RST_VIRTUAL_PIN` 代码

### Fixed

- **矩阵扫描 cols 整数提升 bug**：`~` 运算符对 `uint8_t` 提升为 `int`，导致 `cols` 高 4 位被污染，key4~key15 永远无法触发
  - 修复：`((~(p0In & 0xF0)) >> 4) & 0x0F` — 显式截断到 4 bit
- **LTO 链接错误**：编译时 `-flto` 导致 `undefined reference to TCA9535ButtonThread::*`
  - 原因：.h/.cpp 中的 `#if defined(HAS_TCA9535_BUTTON)` 守卫导致部分编译单元中符号被丢弃
  - 修复：去掉 .h/.cpp 中的条件守卫，类定义和实现始终编译；main.cpp 中的实例化仍由 `#ifdef` 控制

---

## TODO（未来计划）

- [ ] **升级 IO 扩展器 TCA9535 → PCAL9535**
  - 原因：TCA9535 无可配置内部上拉，矩阵键盘列线悬空易受电磁干扰
  - PCAL9535 pin-compatible，支持软件可配置上拉/下拉寄存器（0x41~0x46）、每引脚独立中断遮罩、可配置输出驱动强度
  - 替代方案：PCB 上列线加 10kΩ 外部上拉电阻

---

## 自定义板卡概览

| 板卡 | MCU | LoRa 模块 | 屏幕 | GPS | 按键输入 | 特殊功能 |
|------|-----|-----------|------|-----|----------|----------|
| esp32c3_moonshine | ESP32-C3 | E220-400M30S | 无 | 无 | BUTTON_PIN=9 | — |
| esp32c3_moonshine (fw) | ESP32-C3 | E220-400M30S / E22_400M33S | 无 | 无 | BUTTON_PIN=9 | USB CDC, 电池 ADC |
| esp32c3_moonshine_mv | ESP32-C3 | RA-01SC-P | SSD1306 | 有 | PCF8574 6 键 | NeoPixel, GPS EN |
| esp32c3_moonshine_travelers | ESP32-C3 | RA-01SC-P | SH1106 | 无 | TCA9535 4×4 矩阵 | 电源管理, LoRa RST via I²C |
