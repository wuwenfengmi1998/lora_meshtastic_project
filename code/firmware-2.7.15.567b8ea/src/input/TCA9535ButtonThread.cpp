#include "TCA9535ButtonThread.h"

#include "main.h"

using namespace concurrency;

// 默认按键映射（4×4 矩阵，行优先：KEY[0]=ROW0·COL0 ... KEY[15]=ROW3·COL3）
// 仅保留方向键，SELECT/CANCEL 由其他按键处理
// variant.h 中可用 #define TCA9535_KEY_MAP { ... } 覆盖
// -----------------------------------------------------------------------
#ifndef TCA9535_KEY_MAP
#define TCA9535_KEY_MAP                                                                                                         \
    {                                                                                                                           \
        INPUT_BROKER_NONE,   /* key0  = ROW0·COL0 */                                                                           \
        INPUT_BROKER_NONE,   /* key1  = ROW0·COL1 */                                                                           \
        INPUT_BROKER_NONE,   /* key2  = ROW0·COL2 */                                                                           \
        INPUT_BROKER_UP,     /* key3  = ROW0·COL3 */                                                                           \
        INPUT_BROKER_NONE,   /* key4  = ROW1·COL0 */                                                                           \
        INPUT_BROKER_NONE,   /* key5  = ROW1·COL1 */                                                                           \
        INPUT_BROKER_NONE,   /* key6  = ROW1·COL2 */                                                                           \
        INPUT_BROKER_DOWN,   /* key7  = ROW1·COL3 */                                                                           \
        INPUT_BROKER_NONE,   /* key8  = ROW2·COL0 */                                                                           \
        INPUT_BROKER_NONE,   /* key9  = ROW2·COL1 */                                                                           \
        INPUT_BROKER_NONE,   /* key10 = ROW2·COL2 */                                                                           \
        INPUT_BROKER_LEFT,   /* key11 = ROW2·COL3 */                                                                           \
        INPUT_BROKER_NONE,   /* key12 = ROW3·COL0 */                                                                           \
        INPUT_BROKER_NONE,   /* key13 = ROW3·COL1 */                                                                           \
        INPUT_BROKER_NONE,   /* key14 = ROW3·COL2 */                                                                           \
        INPUT_BROKER_RIGHT,  /* key15 = ROW3·COL3 */                                                                           \
    }
#endif

static const input_broker_event tca9535KeyMap[TCA9535_KEY_COUNT] = TCA9535_KEY_MAP;

// -----------------------------------------------------------------------
// 中断标志（ISR -> runOnce 通信，volatile，只做 set/clear）
// -----------------------------------------------------------------------
static volatile bool tca9535IntPending = false;

#ifdef TCA9535_INT_PIN
static void IRAM_ATTR tca9535ISR()
{
    tca9535IntPending = true;
}
#endif

// -----------------------------------------------------------------------
// 构造 / 初始化
// -----------------------------------------------------------------------
TCA9535ButtonThread::TCA9535ButtonThread(const char *name, TwoWire *wire)
    : OSThread(name), _wire(wire), _originName(name)
{
    if (inputBroker)
        inputBroker->registerSource(this);
}

bool TCA9535ButtonThread::init()
{
    // ===================================================================
    // 第一步：配置 P1 口方向
    // P1.2 = 输出（POWER_EN），P1.3 = 输入（POWER_BOOT），P1.4 = 输出（LoRa RST），P1.5 = 输出（状态灯）
    // Configuration 寄存器：1=input, 0=output
    // P1.2=bit2=0, P1.3=bit3=1, P1.4=bit4=0, P1.5=bit5=0 → 0xCB (1100 1011)
    // ===================================================================
    if (!writeReg(TCA9535_REG_CONFIG_P1, 0xCB)) {
        LOG_WARN("TCA9535: P1 config write failed");
        return false;
    }

    // 确保 P1.4 输出高电平（LoRa RST 高 = 正常工作）
    // 注意：此时不拉高 POWER_EN，等开机确认后再拉高
    tca9535LoraReset(true);

    // P1.5 状态灯默认熄灭（高电平）
    tca9535StatusLed(false);

    // ===================================================================
    // 第二步：开机检测 — 等待用户持续按住 P1.3 达 2 秒
    //   物理按键已使 MOS 导通（ESP32 得电），但 POWER_EN 尚未拉高
    //   用户必须持续按住 2 秒，否则 init() 返回 false → 系统不完成启动
    // ===================================================================
    LOG_INFO("TCA9535: Waiting for power button hold (%d ms)...", TCA9535_POWER_BOOT_HOLD_MS);

    uint32_t holdStart = 0;
    bool wasPressed = false;

    while (true) {
        bool pressed = tca9535ReadPowerBoot(_wire);

        if (pressed && !wasPressed) {
            // 按键刚按下，记录起始时间
            holdStart = millis();
            wasPressed = true;
        } else if (!pressed && wasPressed) {
            // 按键松开 — 检查是否按够时间
            uint32_t held = millis() - holdStart;
            if (held >= TCA9535_POWER_BOOT_HOLD_MS) {
                // 按够 2 秒，确认开机
                LOG_INFO("TCA9535: Power button held %lu ms -> boot confirmed", held);
                break;
            } else {
                // 未按够，重新等待
                LOG_INFO("TCA9535: Power button released after %lu ms (need %d), waiting...", held,
                         TCA9535_POWER_BOOT_HOLD_MS);
                wasPressed = false;
            }
        } else if (pressed && wasPressed) {
            // 持续按住中，检查是否已达 2 秒（即使没松开也确认）
            if ((millis() - holdStart) >= TCA9535_POWER_BOOT_HOLD_MS) {
                LOG_INFO("TCA9535: Power button held >= %d ms -> boot confirmed", TCA9535_POWER_BOOT_HOLD_MS);
                break;
            }
        }

        delay(TCA9535_POWER_BOOT_CHECK_MS);
    }

    // ===================================================================
    // 第三步：确认开机 → 拉高 POWER_EN 维持供电
    // ===================================================================
    if (!tca9535PowerEn(true)) {
        LOG_WARN("TCA9535: Failed to set POWER_EN high");
        return false;
    }
    LOG_INFO("TCA9535: POWER_EN set HIGH (system powered)");
    _powerState = TCA9535PowerState::RUNNING;

    // ===================================================================
    // 第四步：配置 P0 口方向（矩阵键盘）
    // ===================================================================
    uint8_t configP0 = 0xF0; // P0.0~P0.3 输出（行），P0.4~P0.7 输入（列）
    if (!writeReg(TCA9535_REG_CONFIG_P0, configP0)) {
        LOG_WARN("TCA9535: P0 config write failed (addr=0x%02x)", TCA9535_I2C_ADDR);
        return false;
    }

    // 行输出初始状态：全部拉高（未选中任何行）
    if (!writeReg(TCA9535_REG_OUTPUT_P0, TCA9535_ALL_ROWS_HIGH)) {
        LOG_WARN("TCA9535: P0 output write failed");
        return false;
    }

    // 极性不反转
    writeReg(TCA9535_REG_INVERT_P0, 0x00);
    writeReg(TCA9535_REG_INVERT_P1, 0x00);

    // 读取初始状态，避免第一次扫描产生误报
    scanMatrix(_lastKeys);

#ifdef TCA9535_INT_PIN
    pinMode(TCA9535_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TCA9535_INT_PIN), tca9535ISR, FALLING);
    LOG_INFO("TCA9535: INT on GPIO%d", TCA9535_INT_PIN);
#endif

    LOG_INFO("TCA9535 init OK (addr=0x%02x, matrix %dx%d, power=RUNNING)", TCA9535_I2C_ADDR, TCA9535_ROWS,
             TCA9535_COLS);
    return true;
}

// -----------------------------------------------------------------------
// 主循环：关机检测 → 矩阵扫描 → 边沿检测 → 事件派发
// -----------------------------------------------------------------------
int32_t TCA9535ButtonThread::runOnce()
{
    // ===================================================================
    // 关机检测：运行中 P1.3 持续按住 2 秒 → 断电
    // ===================================================================
    if (_powerState == TCA9535PowerState::RUNNING) {
        bool pressed = tca9535ReadPowerBoot(_wire);

        if (pressed && _powerBtnPressStart == 0) {
            // 按键刚按下，记录起始时间
            _powerBtnPressStart = millis();
            LOG_DEBUG("TCA9535: Shutdown button pressed, timing...");
        } else if (pressed && _powerBtnPressStart != 0) {
            // 持续按住中
            uint32_t held = millis() - _powerBtnPressStart;
            if (held >= TCA9535_POWER_BOOT_HOLD_MS) {
                LOG_WARN("TCA9535: Power button held %lu ms -> SHUTDOWN", held);
                _powerBtnPressStart = 0;
                dispatchEvent(INPUT_BROKER_SHUTDOWN);
            }
        } else if (!pressed && _powerBtnPressStart != 0) {
            // 按键松开，未达关机时间 → 短按 = CANCEL 事件
            uint32_t held = millis() - _powerBtnPressStart;
            LOG_DEBUG("TCA9535: Power button short press (%lu ms) -> CANCEL", held);
            _powerBtnPressStart = 0;
            dispatchEvent(INPUT_BROKER_CANCEL);
        }
    }

    // ===================================================================
    // P1.5 状态灯闪烁：500ms 亮 + 500ms 灭 = 1 秒周期
    // ===================================================================
    if (millis() - _statusLedToggleMs >= 500) {
        _statusLedToggleMs = millis();
        _statusLedOn = !_statusLedOn;
        tca9535StatusLed(_statusLedOn);
    }

    // ===================================================================
    // 矩阵键盘扫描（仅 RUNNING 状态）
    // ===================================================================
#ifdef TCA9535_INT_PIN
    // 无中断挂起则跳过，节省 I²C 带宽
    if (!tca9535IntPending)
        return TCA9535_POWER_BOOT_CHECK_MS;
    tca9535IntPending = false;
#endif

    uint16_t currentKeys = 0x0000;
    if (!scanMatrix(currentKeys)) {
        LOG_WARN("TCA9535: scan failed");
        return 50;
    }

    // 边沿检测：新按下（上升沿）和释放（下降沿）
    uint16_t pressed  = currentKeys & ~_lastKeys;  // 新按下的键
    uint16_t released = ~currentKeys & _lastKeys;  // 新释放的键（目前不处理）

    _lastKeys = currentKeys;

    // 遍历所有键位，派发按下事件
    for (uint8_t i = 0; i < TCA9535_KEY_COUNT; i++) {
        if (pressed & (1u << i)) {
            input_broker_event evt = tca9535KeyMap[i];
            if (evt != INPUT_BROKER_NONE) {
                dispatchEvent(evt);
            }
        }
    }
    (void)released; // 目前仅处理按下边沿

#ifdef TCA9535_INT_PIN
    return 20; // 中断模式：20ms 防抖窗口（每轮扫描间隔）
#else
    return 50; // 轮询模式：50ms 扫描间隔
#endif
}

// -----------------------------------------------------------------------
// 矩阵扫描：逐行拉低，读列状态
// 返回 16 位，bit[i] = 1 表示 ROW(i/4)·COL(i%4) 被按下（低电平）
// -----------------------------------------------------------------------
bool TCA9535ButtonThread::scanMatrix(uint16_t &keys)
{
    keys = 0x0000;

    for (uint8_t row = 0; row < TCA9535_ROWS; row++) {
        // 拉低当前行，其余行保持高
        uint8_t outVal = TCA9535_ROW_MASK(row);
        if (!writeReg(TCA9535_REG_OUTPUT_P0, outVal)) {
            return false;
        }

        // 短延时，等待电平稳定（行列电容充放电）
        delayMicroseconds(50);

        // 读取 P0 输入寄存器
        uint8_t p0In = 0xFF;
        if (!readReg(TCA9535_REG_INPUT_P0, p0In)) {
            return false;
        }

        // 提取列位（P0.4~P0.7），低电平=按下
        // 将列状态从高4位移到低位，便于索引
        // 注意：~ 运算会将 uint8_t 提升为 int，必须 & 0x0F 截断到 4 bit
        uint8_t cols = ((~(p0In & TCA9535_COL_MASK)) >> 4) & 0x0F; // bit0=COL0, bit3=COL3

        // 组装到 keys（每行 4 列）
        keys |= ((uint16_t)cols << (row * TCA9535_COLS));
    }

    // 扫描完毕，恢复所有行高电平
    writeReg(TCA9535_REG_OUTPUT_P0, TCA9535_ALL_ROWS_HIGH);

    return true;
}

// -----------------------------------------------------------------------
// 私有：I²C 读写
// -----------------------------------------------------------------------
bool TCA9535ButtonThread::writeReg(uint8_t reg, uint8_t val)
{
    _wire->beginTransmission(TCA9535_I2C_ADDR);
    _wire->write(reg);
    _wire->write(val);
    return (_wire->endTransmission() == 0);
}

bool TCA9535ButtonThread::readReg(uint8_t reg, uint8_t &val)
{
    _wire->beginTransmission(TCA9535_I2C_ADDR);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0)
        return false;
    if (_wire->requestFrom((uint8_t)TCA9535_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    val = _wire->read();
    return true;
}

void TCA9535ButtonThread::dispatchEvent(input_broker_event evt)
{
    InputEvent e = {};
    e.source     = _originName;
    e.inputEvent = evt;
    e.kbchar     = 0;
    e.touchX     = 0;
    e.touchY     = 0;
    this->notifyObservers(&e);
}
