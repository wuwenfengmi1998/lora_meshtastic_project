#include "TCA9535ButtonThread.h"

#include "main.h"

using namespace concurrency;

// 默认按键映射（4×4 矩阵，行优先：KEY[0]=ROW0·COL0 ... KEY[15]=ROW3·COL3）
// variant.h 中可用 #define TCA9535_KEY_MAP { ... } 覆盖
// -----------------------------------------------------------------------
#ifndef TCA9535_KEY_MAP
#define TCA9535_KEY_MAP                                                                                                         \
    {                                                                                                                           \
        INPUT_BROKER_MATRIXKEY, /* key0  = ROW0·COL0 → '1' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key1  = ROW0·COL1 → '2' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key2  = ROW0·COL2 → '3' */                                                                \
        INPUT_BROKER_UP,        /* key3  = ROW0·COL3 */                                                                       \
        INPUT_BROKER_MATRIXKEY, /* key4  = ROW1·COL0 → '4' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key5  = ROW1·COL1 → '5' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key6  = ROW1·COL2 → '6' */                                                                \
        INPUT_BROKER_DOWN,      /* key7  = ROW1·COL3 */                                                                       \
        INPUT_BROKER_MATRIXKEY, /* key8  = ROW2·COL0 → '7' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key9  = ROW2·COL1 → '8' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key10 = ROW2·COL2 → '9' */                                                                \
        INPUT_BROKER_LEFT,      /* key11 = ROW2·COL3 */                                                                       \
        INPUT_BROKER_MATRIXKEY, /* key12 = ROW3·COL0 → '*' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key13 = ROW3·COL1 → '0' */                                                                \
        INPUT_BROKER_MATRIXKEY, /* key14 = ROW3·COL2 → '#' */                                                                \
        INPUT_BROKER_RIGHT,     /* key15 = ROW3·COL3 */                                                                       \
    }
#endif

// 默认按键字符映射（仅 INPUT_BROKER_MATRIXKEY 类型的按键使用）
// 传 ASCII 字符，CannedMessageModule 会根据 kbchar 走文本输入路径
// variant.h 中可用 #define TCA9535_KEY_CHAR_MAP { ... } 覆盖
#ifndef TCA9535_KEY_CHAR_MAP
#define TCA9535_KEY_CHAR_MAP                                                                                                    \
    {                                                                                                                           \
        '1', /* key0  = ROW0·COL0 */                                                                                           \
        '2', /* key1  = ROW0·COL1 */                                                                                           \
        '3', /* key2  = ROW0·COL2 */                                                                                           \
         0,  /* key3  = ROW0·COL3 → 方向键，无字符 */                                                                          \
        '4', /* key4  = ROW1·COL0 */                                                                                           \
        '5', /* key5  = ROW1·COL1 */                                                                                           \
        '6', /* key6  = ROW1·COL2 */                                                                                           \
         0,  /* key7  = ROW1·COL3 → 方向键，无字符 */                                                                          \
        '7', /* key8  = ROW2·COL0 */                                                                                           \
        '8', /* key9  = ROW2·COL1 */                                                                                           \
        '9', /* key10 = ROW2·COL2 */                                                                                           \
         0,  /* key11 = ROW2·COL3 → 方向键，无字符 */                                                                          \
        '*', /* key12 = ROW3·COL0 */                                                                                           \
        '0', /* key13 = ROW3·COL1 */                                                                                           \
        '#', /* key14 = ROW3·COL2 */                                                                                           \
         0,  /* key15 = ROW3·COL3 → 方向键，无字符 */                                                                          \
    }
#endif

static const input_broker_event tca9535KeyMap[TCA9535_KEY_COUNT] = TCA9535_KEY_MAP;
static const unsigned char tca9535KeyCharMap[TCA9535_KEY_COUNT] = TCA9535_KEY_CHAR_MAP;

// -----------------------------------------------------------------------
// 中断标志（ISR -> runOnce 通信，volatile，只做 set/clear）
// -----------------------------------------------------------------------
static volatile bool tca9535IntPending = false;

#ifdef HAS_TCA9535_BUTTON
volatile bool tca9535IsCharging = false;
#endif

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
    // P1 口方向注释：
    // P1.0=输出(键盘背光), P1.1=输入(CHARGE_DET), P1.2=输出(POWER_EN),
    // P1.3=输入(POWER_BOOT), P1.4=输出(LoRa RST), P1.5=输出(状态灯),
    // P1.6=输出(振子 VIBRATOR), P1.7=输出(GPS EN)
    // P1 口配置寄存器：1=input, 0=output
    // bit: P1.7 P1.6 P1.5 P1.4 P1.3 P1.2 P1.1 P1.0
    //        0    0    0    0    1    0    1    0   = 0x0A
    if (!writeReg(TCA9535_REG_CONFIG_P1, 0x0A)) {
        LOG_WARN("TCA9535: P1 config write failed");
        return false;
    }

    // P1.0 键盘背光默认熄灭（低电平）
    tca9535Backlight(false);

    // 确保 P1.4 输出高电平（LoRa RST 高 = 正常工作）
    // 注意：POWER_EN 已由 main.cpp 在 Wire.begin() 后立即拉高，此处无需再操作
    tca9535LoraReset(true);

    // P1.5 状态灯默认熄灭（高电平）
    tca9535StatusLed(false);

    // P1.6 振子默认关闭（低电平）
    tca9535Vibrate(false);

    // P1.7 GPS EN 默认打开（高电平 = GPS 上电）
    tca9535GpsEn(true);

    // ===================================================================
    // 第三步：POWER_EN 已由 main.cpp 在 Wire.begin() 后立即拉高，
    //   并等待 P1.3 持续按住 2 秒确认开机（超时 3 秒则断电关机）。
    //   此处只需确认状态机进入 RUNNING，并触发开机震动 300ms。
    // ===================================================================
    LOG_INFO("TCA9535: Boot already confirmed in early boot, state=RUNNING");
    _powerState = TCA9535PowerState::RUNNING;

    // 开机震动 300ms
    tca9535Vibrate(true);
    _vibrateOn = true;
    _vibrateStartMs = millis();
    LOG_DEBUG("TCA9535: Boot vibration started (300ms)");

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
                // 关机震动 300ms，震动结束后由振子超时逻辑触发 SHUTDOWN 事件
                if (!_vibrateOn) {
                    tca9535Vibrate(true);
                    _vibrateOn = true;
                    _vibrateStartMs = millis();
                    LOG_DEBUG("TCA9535: Shutdown vibration started (300ms)");
                }
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
    // P1.6 振子超时停止：震动 300ms 后自动关闭
    // ===================================================================
    if (_vibrateOn && millis() - _vibrateStartMs >= 300) {
        _vibrateOn = false;
        tca9535Vibrate(false);
        LOG_DEBUG("TCA9535: Vibration stopped");
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
    // P1.0 键盘背光：有按键按下时点亮，5 秒无操作自动熄灭
    // ===================================================================
    if (_backlightOn && millis() - _backlightLastMs >= 5000) {
        _backlightOn = false;
        tca9535Backlight(false);
    }

    // ===================================================================
    // 充电检测：轮询 P1.1 (CHARGE_DET)，高电平=正在充电
    // ===================================================================
#ifdef TCA9535_CHARGE_DET_PIN
    if (millis() - _chargeDetLastMs >= 500) {
        _chargeDetLastMs = millis();
        bool charging = tca9535ReadChargeDet();
        if (charging != tca9535IsCharging) {
            tca9535IsCharging = charging;
            LOG_INFO("TCA9535: Charging %s", charging ? "DETECTED" : "STOPPED");
        }
    }
#endif

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
            // 按键按下 → 点亮键盘背光（重置 5 秒计时）
            if (!_backlightOn) {
                _backlightOn = true;
                _backlightLastMs = millis();
                tca9535Backlight(true);
            } else {
                _backlightLastMs = millis(); // 已亮则刷新计时
            }
            input_broker_event evt = tca9535KeyMap[i];
            if (evt != INPUT_BROKER_NONE) {
                dispatchEvent(evt, tca9535KeyCharMap[i]);
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

void TCA9535ButtonThread::dispatchEvent(input_broker_event evt, unsigned char kbchar)
{
    InputEvent e = {};
    e.source     = _originName;
    e.inputEvent = evt;
    e.kbchar     = kbchar;
    e.touchX     = 0;
    e.touchY     = 0;
    this->notifyObservers(&e);
}
