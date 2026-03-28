#pragma once

/**
 * TCA9535PWR I²C IO 扩展器 — 4×4 矩阵键盘 + 电源管理 + LoRa RST 控制
 *
 * 硬件：TI TCA9535PWR
 *   - A0=0, A1=0, A2=0 → I²C 地址 0x20
 *   - P0.0~P0.3：行输出（ROW0~ROW3），逐行拉低扫描
 *   - P0.4~P0.7：列输入（COL0~COL3），读取按键状态
 *   - P1.2：电源使能（POWER_EN），高电平有效，驱动 MOS 管维持供电
 *   - P1.3：电源开机按钮（POWER_BOOT），输入，低电平有效（按键按下接地）
 *   - P1.4：LoRa RST 输出（通过 I²C 控制 RadioLib 复位序列）
 *   - P1.5：状态指示灯，低电平点亮
 *
 * 电源管理逻辑：
 *   开机：物理按键按下 → MOS 导通 → ESP32/TCA9535 得电
 *         init() 读 P1.3，持续按住 2 秒 → tca9535PowerEn(true) 维持供电
 *         未按满 2 秒松开 → 不拉高 POWER_EN → MOS 断开 → 断电
 *   关机：运行中 P1.3 持续低电平 2 秒 → tca9535PowerEn(false) → 断电
 *
 * 寄存器布局：
 *   0x00  Input Port 0      (只读)
 *   0x01  Input Port 1
 *   0x02  Output Port 0     (控制行输出电平)
 *   0x03  Output Port 1     (P1.2 POWER_EN, P1.4 LoRa RST)
 *   0x04  Polarity Inversion Port 0
 *   0x05  Polarity Inversion Port 1
 *   0x06  Configuration Port 0  (1=input, 0=output)
 *   0x07  Configuration Port 1
 *
 * 使用方式：在 variant.h 中定义以下宏，main.cpp 会自动初始化：
 *   #define HAS_TCA9535_BUTTON
 *   #define TCA9535_INT_PIN  5              // 中断引脚（可选，低电平有效）
 *   #define TCA9535_KEY_MAP { ... }        // 4×4=16 元素的按键映射
 *   #define TCA9535_LORA_RST_VIRTUAL_PIN 200 // LoRa RST 虚拟引脚号
 *   #define LORA_RESET TCA9535_LORA_RST_VIRTUAL_PIN
 */

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Wire.h>

#ifndef TCA9535_I2C_ADDR
#define TCA9535_I2C_ADDR 0x20 // A0=A1=A2=0
#endif

// 矩阵行列数
#ifndef TCA9535_ROWS
#define TCA9535_ROWS 4
#endif
#ifndef TCA9535_COLS
#define TCA9535_COLS 4
#endif
#define TCA9535_KEY_COUNT (TCA9535_ROWS * TCA9535_COLS)

// 行输出掩码：P0.0~P0.3 = bit3~bit0
// 扫描第 n 行时，将对应 bit 拉低（0），其余拉高（1）
#define TCA9535_ROW_MASK(r)   (0xF0 | ~(1u << (r)))
#define TCA9535_ALL_ROWS_HIGH 0xF0

// 列输入掩码：P0.4~P0.7 = bit7~bit4
#define TCA9535_COL_MASK 0xF0

// 电源管理常量
#ifndef TCA9535_POWER_BOOT_HOLD_MS
#define TCA9535_POWER_BOOT_HOLD_MS 2000 // 开机/关机需持续按住的时间（毫秒）
#endif
#ifndef TCA9535_POWER_BOOT_CHECK_MS
#define TCA9535_POWER_BOOT_CHECK_MS 50 // P1.3 轮询间隔（毫秒）
#endif

// TCA9535 寄存器定义
#define TCA9535_REG_INPUT_P0   0x00
#define TCA9535_REG_INPUT_P1   0x01
#define TCA9535_REG_OUTPUT_P0  0x02
#define TCA9535_REG_OUTPUT_P1  0x03
#define TCA9535_REG_INVERT_P0  0x04
#define TCA9535_REG_INVERT_P1  0x05
#define TCA9535_REG_CONFIG_P0  0x06
#define TCA9535_REG_CONFIG_P1  0x07

// P1 口引脚位掩码
#define TCA9535_BIT_P12  (1u << 2) // POWER_EN 输出
#define TCA9535_BIT_P13  (1u << 3) // POWER_BOOT 输入
#define TCA9535_BIT_P14  (1u << 4) // LoRa RST 输出
#define TCA9535_BIT_P15  (1u << 5) // 状态指示灯输出（低电平点亮）
#define TCA9535_BIT_P16  (1u << 6) // GPS RST 输出
#define TCA9535_BIT_P17  (1u << 7) // GPS EN 输出（高电平有效）

/**
 * 通过 I²C 控制 TCA9535 P1.2 上的电源使能（POWER_EN）。
 * 高电平有效：控制 MOS 管维持系统供电。
 * 此函数是 static 的，可被任意上下文调用（无需实例）。
 * @param on true=上电（高电平），false=断电（低电平）
 */
static inline bool tca9535PowerEn(bool on)
{
    // 读取当前 P1 输出寄存器值
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1Out = Wire.read();

    // 修改 P1.2 位
    if (on)
        p1Out |= TCA9535_BIT_P12;  // 拉高 = 上电
    else
        p1Out &= ~TCA9535_BIT_P12; // 拉低 = 断电

    // 写回
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    Wire.write(p1Out);
    return (Wire.endTransmission() == 0);
}

/**
 * 通过 I²C 读取 TCA9535 P1.3（POWER_BOOT）输入状态。
 * @return true=按键按下（低电平），false=按键松开（高电平）
 */
static inline bool tca9535ReadPowerBoot(TwoWire *wire = &Wire)
{
    wire->beginTransmission(TCA9535_I2C_ADDR);
    wire->write(TCA9535_REG_INPUT_P1);
    if (wire->endTransmission(false) != 0)
        return false; // I²C 错误时返回 false（未按下）
    if (wire->requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1In = wire->read();
    // P1.3 低电平 = 按下
    return !(p1In & TCA9535_BIT_P13);
}

/**
 * 通过 I²C 控制 TCA9535 P1.4 上的 LoRa RST。
 * 此函数是 static 的，可被自定义 HAL 在任意上下文调用（无需实例）。
 * @param high true=释放复位（高电平），false=触发复位（低电平）
 */
static inline bool tca9535LoraReset(bool high)
{
    // 读取当前 P1 输出寄存器值
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1Out = Wire.read();

    // 修改 P1.4 位
    if (high)
        p1Out |= TCA9535_BIT_P14;  // 拉高 = 释放复位
    else
        p1Out &= ~TCA9535_BIT_P14; // 拉低 = 触发复位

    // 写回
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    Wire.write(p1Out);
    return (Wire.endTransmission() == 0);
}

/**
 * 通过 I²C 控制 TCA9535 P1.5 上的状态指示灯。
 * 低电平点亮，高电平熄灭。
 * @param on true=点亮（低电平），false=熄灭（高电平）
 */
static inline bool tca9535StatusLed(bool on)
{
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1Out = Wire.read();

    // 修改 P1.5 位：低电平点亮
    if (on)
        p1Out &= ~TCA9535_BIT_P15; // 拉低 = 点亮
    else
        p1Out |= TCA9535_BIT_P15;  // 拉高 = 熄灭

    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    Wire.write(p1Out);
    return (Wire.endTransmission() == 0);
}

/**
 * 通过 I²C 控制 TCA9535 P1.6 上的 GPS RST。
 * @param high true=释放复位（高电平），false=触发复位（低电平）
 */
static inline bool tca9535GpsReset(bool high)
{
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1Out = Wire.read();

    if (high)
        p1Out |= TCA9535_BIT_P16;  // 拉高 = 释放复位
    else
        p1Out &= ~TCA9535_BIT_P16; // 拉低 = 触发复位

    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    Wire.write(p1Out);
    return (Wire.endTransmission() == 0);
}

/**
 * 通过 I²C 控制 TCA9535 P1.7 上的 GPS EN。
 * 高电平有效：拉高 = GPS 上电，拉低 = GPS 断电。
 * @param on true=上电（高电平），false=断电（低电平）
 */
static inline bool tca9535GpsEn(bool on)
{
    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    uint8_t p1Out = Wire.read();

    if (on)
        p1Out |= TCA9535_BIT_P17;  // 拉高 = GPS 上电
    else
        p1Out &= ~TCA9535_BIT_P17; // 拉低 = GPS 断电

    Wire.beginTransmission(TCA9535_I2C_ADDR);
    Wire.write(TCA9535_REG_OUTPUT_P1);
    Wire.write(p1Out);
    return (Wire.endTransmission() == 0);
}

/**
 * 电源管理状态机
 */
enum class TCA9535PowerState : uint8_t {
    BOOT_PENDING,  // 等待开机确认（init 阶段，检测 P1.3 是否按满 2 秒）
    RUNNING,       // 正常运行，POWER_EN 已拉高
    SHUTDOWN_PENDING, // 关机倒计时（P1.3 持续按住中）
};

class TCA9535ButtonThread : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TCA9535ButtonThread(const char *name, TwoWire *wire = &Wire);

    /**
     * 初始化 TCA9535 并执行开机检测：
     *   1. 配置 P1 口方向（P1.2/P1.4 输出，P1.3 输入）
     *   2. 等待检测 P1.3 是否持续按住 2 秒
     *   3. 如果是 → 拉高 POWER_EN，继续初始化键盘
     *   4. 如果否 → 不拉高 POWER_EN（MOS 断开 → 断电）
     *   5. 配置 P0 口方向和矩阵键盘
     * @return true 如果开机确认成功且键盘初始化完成
     */
    bool init();

    /// 当前电源状态
    TCA9535PowerState powerState() const { return _powerState; }

  protected:
    int32_t runOnce() override;

  private:
    TwoWire *_wire;
    const char *_originName;

    // 电源管理
    TCA9535PowerState _powerState = TCA9535PowerState::BOOT_PENDING;
    uint32_t _powerBtnPressStart = 0; // 按键按下时刻 (millis)

    // 上次扫描结果（16 位，每 bit 对应 row*4+col），用于边沿检测
    uint16_t _lastKeys = 0x0000;

    // P1.5 状态灯闪烁控制
    bool _statusLedOn = false;
    uint32_t _statusLedToggleMs = 0;

    // 写寄存器
    bool writeReg(uint8_t reg, uint8_t val);

    // 读寄存器
    bool readReg(uint8_t reg, uint8_t &val);

    // 扫描矩阵一次，返回 16 位按键状态（bit=1 表示按下）
    bool scanMatrix(uint16_t &keys);

    // 派发事件到 InputBroker
    void dispatchEvent(input_broker_event evt);
};

// 仅在 HAS_TCA9535_BUTTON 启用时导出全局指针声明
#ifdef HAS_TCA9535_BUTTON
extern TCA9535ButtonThread *tca9535ButtonThread;
#endif
