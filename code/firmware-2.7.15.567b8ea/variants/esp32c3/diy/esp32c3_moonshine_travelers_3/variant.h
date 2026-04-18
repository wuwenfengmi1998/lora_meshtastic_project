#define PIN_BUZZER 12
#define BUZZER_PIN 12

#define BUTTON_PIN 9

// GPIO9 短按直接触发 SELECT（而非默认的 USER_PRESS 切换）
// 长按无功能（关机由 POWER_BOOT 长按处理）
#ifndef BUTTON_SINGLE_PRESS_EVENT
#define BUTTON_SINGLE_PRESS_EVENT INPUT_BROKER_SELECT
#endif
#define BUTTON_DISABLE_LONG_PRESS 1


#define HAS_SCREEN 1
#define USE_SH1106

// 启用 CannedMessageModule（快捷回复/自由文本输入）
#define CANNED_MESSAGE_MODULE_ENABLE 1

#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 0
#define I2C_SCL 1

#define HAS_GPS 1
#define GPS_RX_PIN 21
#define GPS_TX_PIN 20


#define BATTERY_PIN            2
#define ADC_CHANNEL            ADC1_GPIO2_CHANNEL
#define ADC_MULTIPLIER         2.0f

// -----------------------------------------------------------------------
// TCA9535PWR I²C IO 扩展器 — 4×4 矩阵键盘 + 电源控制 + LoRa RST
//   - 与屏幕共用 I²C 总线 (SDA=0, SCL=1)
//   - A0=0, A1=0, A2=0 → 地址 0x20 (TCA9535_I2C_ADDR)
//   - P0.0~P0.3 行输出（ROW0~ROW3），P0.4~P0.7 列输入（COL0~COL3）
//   - P1.0 键盘背光输出（高电平点亮，按键时亮 5 秒后自动熄灭）
//   - P1.2 电源使能（POWER_EN），高电平有效，驱动 MOS 管维持供电
//   - P1.3 电源开机按钮（POWER_BOOT），输入，低电平有效
//         开机：持续按住 2 秒 → POWER_EN 拉高维持供电
//         关机：运行中持续按住 2 秒 → POWER_EN 拉低断电
//   - P1.4 LoRa RST 输出（通过 I²C 控制 RadioLib 复位序列）
//   - P1.6 GPS_RST 输出（通过 tca9535GpsReset() 控制，init 中释放）
//   - P1.7 GPS_EN 输出（高电平有效，通过 enablePin 桥接到 TCA9535）
//   - P1.1 CHARGE_DET 输入（高电平=正在充电）
//   - 中断引脚 GPIO5，低电平有效，下降沿触发
// -----------------------------------------------------------------------
#define HAS_TCA9535_BUTTON
#define TCA9535_INT_PIN         5      // TCA9535 INT → GPIO5（低电平有效，下降沿触发）
#define TCA9535_POWER_EN_BIT    (1u << 2)  // P1.2 = 电源使能（高电平=开机）
#define TCA9535_CHARGE_DET_PIN  (1u << 1)  // P1.1 = 充电检测输入（高电平=正在充电）

// 按键映射：4×4 矩阵，行优先排列
//   KEY[0]=ROW0·COL0, KEY[1]=ROW0·COL1, ..., KEY[15]=ROW3·COL3
//   低电平有效（按下接地，列读取到低电平=按下）
//   九宫格：key0~2=1~3, key4~6=4~6, key8~10=7~9, key12=*, key13=0, key14=#
//   方向键：key3=UP, key7=DOWN, key11=LEFT, key15=RIGHT
//   SELECT 由 GPIO9 短按触发，CANCEL 由 POWER_BOOT(P1.3) 短按触发
#define TCA9535_KEY_MAP                                                                                                        \
    {                                                                                                                          \
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

#define TCA9535_KEY_CHAR_MAP                                                                                                   \
    {                                                                                                                          \
        '1', /* key0  */                                                                                                      \
        '2', /* key1  */                                                                                                      \
        '3', /* key2  */                                                                                                      \
         0,  /* key3  → 方向键 */                                                                                             \
        '4', /* key4  */                                                                                                      \
        '5', /* key5  */                                                                                                      \
        '6', /* key6  */                                                                                                      \
         0,  /* key7  → 方向键 */                                                                                             \
        '7', /* key8  */                                                                                                      \
        '8', /* key9  */                                                                                                      \
        '9', /* key10 */                                                                                                      \
         0,  /* key11 → 方向键 */                                                                                             \
        '*', /* key12 */                                                                                                      \
        '0', /* key13 */                                                                                                      \
        '#', /* key14 */                                                                                                      \
         0,  /* key15 → 方向键 */                                                                                             \
    }

#define E22_400M33S
#ifdef E22_400M33S
#define SETTING_MAX_POWER 22
#define NUM_PA_POINTS 1
#define TX_GAIN_LORA 0
#define SX126X_MAX_POWER 22
//#define HAS_LORA_FEM 1
#endif

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

// LoRa RST 通过 TCA9535 P1.4 控制
// 使用虚拟引脚号 200，由自定义 HAL 拦截并转发到 I²C
#define TCA9535_LORA_RST_VIRTUAL_PIN 200
#define TCA9535_LORA_RST_REG     TCA9535_REG_OUTPUT_P1  // P1 输出寄存器
#define TCA9535_LORA_RST_BIT     (1u << 4)              // P1.4

// GPS RST (P1.6) 和 GPS EN (P1.7) 通过 TCA9535 I²C 控制
// 不定义 PIN_GPS_RESET/PIN_GPS_EN 为虚拟引脚（避免 GpioHwPin 访问无效 GPIO）
// 改为在 main.cpp 中 createGps() 后替换 enablePin 为 TCA9535 GpioPin
#define TCA9535_GPS_HAS_CTRL 1

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET TCA9535_LORA_RST_VIRTUAL_PIN
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define SX126X_RXEN 13
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define TCXO_OPTIONAL
