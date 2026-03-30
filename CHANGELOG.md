# Changelog

基于 [Meshtastic 官方固件 v2.7.15](https://github.com/meshtastic/firmware/tree/2.7.15) (commit `567b8ea`)。

格式参考 [Keep a Changelog](https://keepachangelog.com/)。

---

## [Unreleased]

### Added

#### GPS 支持（esp32c3_moonshine_travelers）
- 启用 GPS 子系统：`HAS_GPS 1`，UART 引脚 `GPS_RX_PIN 20` / `GPS_TX_PIN 21`
- GPS RST（TCA9535 P1.6）和 GPS EN（TCA9535 P1.7）通过 I²C GPIO 扩展器控制
  - 通电时 P1.6、P1.7 默认拉高（GPS 上电 + 释放复位）
  - `tca9535GpsReset(bool high)` / `tca9535GpsEn(bool on)` — read-modify-write P1.6/P1.7
  - `main.cpp` 中通过 `GpioUnaryTransformer` 桥接 `gps->enablePin` → `tca9535GpsEn()`
- 配套 GPS 模块：安信可 GP-02（3.3V，NMEA 9600 bps）

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
- 开机流程：物理按键 → MOS 导通 → ESP32 得电 → `Wire.begin()` 后立即 `tca9535PowerEn(true)` 锁住供电 → 启动系统
- 关机流程：运行中 P1.3 持续按住 2 秒 → 清空屏幕 → POWER_EN 拉低 → 用户松手后 MOS 断开断电
- 电源状态机：`BOOT_PENDING` → `RUNNING` → `SHUTDOWN_PENDING`
- P1 口配置：`0x8B`（P1.2=输出, P1.3=输入, P1.4=输出, P1.5=输出, P1.6=输出, P1.7=输出）

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

#### 中文 12×12 点阵字库（esp32c3_moonshine_travelers）
- 新增 `src/graphics/fonts/ChineseFont12x12.h`：21075 字形，~535 KB flash
  - 生成工具：`tools/gen_chinese_font.mjs`（`@napi-rs/canvas` 光栅化 + 位图提取）
  - Unicode 覆盖：U+4E00–U+9FFF（CJK 统一汉字全集，含 GB2312 6763 字）+ CJK 标点 + 全角 ASCII
  - 像素判定：透明背景 + 黑色前景，alpha > 80 阈值
- `MessageRenderer.cpp`（收到的消息显示）支持 CJK 渲染：
  - `generateLines()` UTF-8 感知逐字符分词，CJK 字符作为独立 word
  - `drawStringWithEmotes()` 文本段混合渲染（ASCII 用内置字体，CJK 用 `cfont12_draw`）
  - `calculateLineHeights()` CJK 感知行高计算
- `CannedMessageModule.cpp`（快捷回复 FREETEXT 编辑）支持 CJK 渲染：
  - FREETEXT 区域用 `cfont12_drawStr()` 混合渲染 ASCII + CJK

#### 开机流程改为 early-lock
- `main.cpp`：`Wire.begin()` 后立即 `tca9535PowerEn(true)` 锁住供电，防止初始化途中掉电
- 无需额外确认步骤，用户按下按键即开机

#### 快捷回复 ↔ 九宫格输入导航（esp32c3_moonshine_travelers）
- **INACTIVE**：UP/DOWN 进入快捷回复列表（恢复原始行为）
- **ACTIVE**（快捷回复列表）：LEFT/RIGHT 进入九宫格文本输入（FREETEXT），不再映射为上下滚动
- **FREETEXT**（九宫格输入）：LEFT/RIGHT 返回快捷回复列表，保留已输入文字
- `*` 号键映射为退格键（backspace）
- `isUpEvent()` / `isDownEvent()` 移除 ACTIVE 状态对 LEFT/RIGHT 的映射

#### 充电检测轮询间隔缩短
- TCA9535 CHARGE_DET 轮询间隔从 2000ms 缩短到 500ms，加快充电状态响应

#### T9 九宫格输入法（esp32c3_moonshine_travelers）
- 三种输入模式：abc（小写） / ABC（大写） / 123（数字），按 `#` 循环切换
- 数字键 2-9：大小写模式下 multi-tap 选字母（800ms 超时自动确认），数字模式下直接输出数字
- 数字键 0 = 空格（大小写模式） / 数字 0（数字模式）
- 数字键 1 = 标点循环 `. , ! ?`（大小写模式） / 数字 1（数字模式）
- `*` 号键 = 退格（backspace）
- 屏幕右下角显示当前输入模式标签，光标位置实时预览 multi-tap 字符

#### 按键映射更新（key3/key7/key11/key15 = 方向键）
- 矩阵按键映射从 `key1=UP, key2=DOWN, key3=LEFT, key4=RIGHT` 改为 `key3=UP, key7=DOWN, key11=LEFT, key15=RIGHT`
- 方向键全部位于 COL3 列（key3=ROW0·COL3, key7=ROW1·COL3, key11=ROW2·COL3, key15=ROW3·COL3）
- SELECT 由 GPIO9 短按处理，CANCEL 由 POWER_BOOT(P1.3) 短按处理

#### P1.5 状态灯改为独立 1 秒闪烁
- P1.5 状态灯从 `GpioSplitter` + `LED_PIN` 同步驱动改为独立驱动
- 在 `TCA9535ButtonThread::runOnce()` 中每 500ms 翻转（亮 500ms + 灭 500ms = 1 秒周期）
- 移除 `Led.cpp` 中的 `GpioTca9535LedPin` 类及相关 `#ifdef TCA9535_LORA_RST_VIRTUAL_PIN` 代码

### Fixed

- **开机确认窗口导致无法启动**：`Wire.begin()` 后要求 P1.3 持续按住 2 秒确认开机，但物理开机流程中用户按下按键后很快松手，3 秒窗口内无法满足 2 秒持续按住条件，导致每次触发 `Boot not confirmed, shutting down` 并断电
  - 修复：删除开机确认窗口，只保留 `tca9535PowerEn(true)` 立即锁住供电
- **TCA9535 P1 口配置误改导致 MOS 断电**（历史记录）：加 CHARGE_DET 后将 P1 config 从正确值改为 `0x8D`，P1.2(POWER_EN) 被误配成输入（高阻），MOS 失控断电
  - 正确值：`0x0A`（P1.1=输入 CHARGE_DET, P1.3=输入 POWER_BOOT, 其余输出）：`~` 运算符对 `uint8_t` 提升为 `int`，导致 `cols` 高 4 位被污染，key4~key15 永远无法触发
  - 修复：`((~(p0In & 0xF0)) >> 4) & 0x0F` — 显式截断到 4 bit
- **LTO 链接错误**：编译时 `-flto` 导致 `undefined reference to TCA9535ButtonThread::*`
  - 原因：.h/.cpp 中的 `#if defined(HAS_TCA9535_BUTTON)` 守卫导致部分编译单元中符号被丢弃
  - 修复：去掉 .h/.cpp 中的条件守卫，类定义和实现始终编译；main.cpp 中的实例化仍由 `#ifdef` 控制
- **T9 commitMultiTap 无限递归崩溃**：`commitMultiTap()` → `runOnce()` → 检测超时又调 `commitMultiTap()` → 栈溢出
  - 修复：添加 `committingMultiTap` 重入保护标志
- **T9 payload 残留导致非预期行为**：`runOnce()` 调用后 payload 未清零，被下次调度复用
  - 修复：所有手动 `runOnce()` 调用后立即 `payload = 0`，末尾加最后防线清零
- **T9 REGENERATE_FRAMESET 重复触发导致跳回主页面**：同一调用链中多次 `notifyObservers(REGENERATE_FRAMESET)` 导致 `requestFocus()` 被消费后 `focusedModule=255` → 跳第一帧
  - 修复：`commitMultiTap` 移除多余通知，`showMultiTapPreview`/`#`键改用 `REDRAW_ONLY`，超时 commit 后跳过后续通知，LEFT/RIGHT 补加 `requestFocus()`
- **T9 大小写模式下出现数字**：旧 t9Map index 0 存放数字，大小写模式下仍会显示
  - 修复：重做 `t9LetterMap`，去掉数字项，DIGIT 模式下直接输出数字不走 multi-tap
- **T9 光标不跟随预览字符跳转**：preview 字符插入 `displayText` 后 `displayCursor` 未 +1
  - 修复：有 pending multi-tap 时 `displayCursor = cursor + 1`
- **T9 按 0 无法输出空格**：`multiTapKey` 用 `0` 表示"无 pending"，与按键 0 的值冲突
  - 修复：无效标记从 `0` 改为 `0xFF`
- **NodeInfo 不广播节点名字和公钥**：从 CLIENT_HIDDEN 切回 CLIENT/CLIENT_MUTE 后，`owner.role` 仍保持原值导致 NodeInfo 广播使用错误的 role
  - 修复：在 `AdminModule::handleSetConfig()` 末尾显式同步 `owner.role = config.device.role`
- **从 CLIENT_HIDDEN 切回 CLIENT 后 NodeInfo 永不广播**：`installRoleDefaults(CLIENT_HIDDEN)` 设置 `node_info_broadcast_secs = INT32_MAX`，但切回 CLIENT 时无对应恢复分支
  - 修复：在 `NodeDB::installRoleDefaults()` 新增 CLIENT/CLIENT_MUTE 分支，恢复 `default_node_info_broadcast_secs` 和相关广播间隔

#### 人机交互修改
- **主页按 * 进入自由文本输入模式**：移除 INACTIVE 状态下 UP/DOWN 按键触发快捷回复列表的逻辑
  - 删除 `CannedMessageModule::handleInputEvent()` 中对 `INPUT_BROKER_UP` / `INPUT_BROKER_DOWN` 的特殊处理
  - 修复 `handleMessageSelectorInput()` 在 INACTIVE 状态下错误处理 UP/DOWN 的问题
  - 按 * 键（或其他可打印字符）直接进入 FREETEXT 模式
  - 在 variant.h 中添加 `CANNED_MESSAGE_MODULE_ENABLE=1` 启用 CannedMessageModule

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
| esp32c3_moonshine_travelers | ESP32-C3 | RA-01SC-P | SH1106 | **有** | TCA9535 4×4 矩阵 | 电源管理, LoRa RST via I²C, GPS RST/EN via I²C |
