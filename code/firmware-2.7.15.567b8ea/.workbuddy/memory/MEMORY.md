# 长期记忆 — Meshtastic 固件项目

## 项目基本信息
- 项目路径：`C:\Users\wuwen\Documents\prj\lora_meshtastic_project\code\firmware-2.7.15.567b8ea`
- 固件版本：2.7.15.567b8ea
- 当前开发变体：`esp32c3_moonshine_travelers_3`（位于 `variants/esp32c3/diy/esp32c3_moonshine_travelers_3/`）

## esp32c3_moonshine 系列变体谱系

| 变体 | 屏幕 | GPS | TCA9535 | LoRa模组 | 蜂鸣器 | 振子 |
|------|------|-----|---------|----------|--------|------|
| esp32c3_moonshine | 无 | 无 | 无 | E22_400M33S | 无 | 无 |
| esp32c3_moonshine_mv | 有(SH1106) | 有 | 有 | RA-01SC-P(3dBm) | 无 | 有 |
| esp32c3_moonshine_travelers | 有(SH1106) | 有 | 有 | RA-01SC-P(3dBm) | 无 | 有 |
| esp32c3_moonshine_travelers_3 | 有(SH1106) | 有 | 有 | E22_400M33S(22dBm) | GPIO12 | 有 |

## esp32c3_moonshine_travelers_3 硬件引脚汇总

### ESP32-C3 直连引脚
- GPIO0: I2C_SCL（屏幕+TCA9535共用）
- GPIO1: I2C_SDA
- GPIO2: BATTERY_PIN（ADC，分压比2.0x）
- GPIO3: LORA_DIO1
- GPIO4: LORA_BUSY
- GPIO5: TCA9535_INT（下降沿，低有效）
- GPIO6: LORA_MISO
- GPIO7: LORA_MOSI
- GPIO8: LORA_CS
- GPIO9: BUTTON_PIN（短按=SELECT，长按功能禁用）
- GPIO10: LORA_SCK
- GPIO12: PIN_BUZZER / BUZZER_PIN
- GPIO13: SX126X_RXEN
- GPIO20: GPS_TX（GPS RX端）
- GPIO21: GPS_RX（GPS TX端）

### TCA9535 I²C IO扩展（地址0x20）
- P0.0~P0.3: 矩阵键盘行输出（ROW0~ROW3）
- P0.4~P0.7: 矩阵键盘列输入（COL0~COL3）
- P1.0: 键盘背光（高电平点亮，5秒无操作自动熄灭）
- P1.1: CHARGE_DET输入（高电平=充电中）
- P1.2: POWER_EN输出（高电平=MOS维持供电）
- P1.3: POWER_BOOT输入（低电平有效，长按2秒关机）
- P1.4: LoRa RST输出（虚拟引脚号200）
- P1.5: 状态指示灯（低电平点亮，500ms周期闪烁）
- P1.6: 振子VIBRATOR（高电平震动）
- P1.7: GPS_EN输出（高电平=GPS上电）

### LoRa模组（E22_400M33S / LLCC68 / SX126X兼容）
- LORA_RESET: 虚拟引脚200（通过TCA9535 P1.4控制）
- SX126X_DIO2_AS_RF_SWITCH: 启用
- SX126X_DIO3_TCXO_VOLTAGE: 1.8V（TCXO可选）

## 关键代码逻辑

### 开机流程
1. 用户按住电源键 → MOS导通 → ESP32+TCA9535上电
2. main.cpp Wire.begin() 后立即调用 `tca9535PowerEn(true)` 锁住供电
3. setupModules() 后创建 TCA9535ButtonThread，调用 init()
4. init() 检查开机已确认，状态机进入RUNNING，触发300ms开机震动

### 关机流程
- runOnce() 检测 P1.3(POWER_BOOT)，持续按住2秒 → 触发关机震动300ms → 发送SHUTDOWN事件

### 矩阵键盘扫描
- 有TCA9535_INT_PIN时：中断驱动（下降沿），20ms防抖
- 无中断时：50ms轮询
- 扫描：逐行拉低→读列状态→16位状态bitmap→边沿检测→派发InputEvent

### 4×4键盘映射（九宫格布局）
- key0~2=1~3，key4~6=4~6，key8~10=7~9
- key12=*，key13=0，key14=#
- key3=UP，key7=DOWN，key11=LEFT，key15=RIGHT
- GPIO9短按=SELECT，P1.3短按=CANCEL

## 项目约定
- 变体文件路径：variants/esp32c3/diy/{变体名}/variant.h
- 自定义驱动放在 src/input/ 目录下
- 虚拟引脚号200用于TCA9535控制的LoRa RST
