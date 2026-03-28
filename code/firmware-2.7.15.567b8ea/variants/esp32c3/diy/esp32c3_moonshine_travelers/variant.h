#define BUTTON_PIN 9


#define HAS_SCREEN 1
#define USE_SH1106

#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 0
#define I2C_SCL 1

#define HAS_GPS 0
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
//   - P1.2 电源使能（POWER_EN），高电平有效，驱动 MOS 管维持供电
//   - P1.3 电源开机按钮（POWER_BOOT），输入，低电平有效
//         开机：持续按住 2 秒 → POWER_EN 拉高维持供电
//         关机：运行中持续按住 2 秒 → POWER_EN 拉低断电
//   - P1.4 LoRa RST 输出（通过 I²C 控制 RadioLib 复位序列）
//   - 中断引脚 GPIO5，低电平有效，下降沿触发
// -----------------------------------------------------------------------
#define HAS_TCA9535_BUTTON
#define TCA9535_INT_PIN         5      // TCA9535 INT → GPIO5（低电平有效，下降沿触发）
#define TCA9535_POWER_EN_BIT    (1u << 2)  // P1.2 = 电源使能（高电平=开机）

// 按键映射：4×4 矩阵，行优先排列
//   KEY[0]=ROW0·COL0, KEY[1]=ROW0·COL1, ..., KEY[15]=ROW3·COL3
// 低电平有效（按下接地，列读取到低电平=按下）
#define TCA9535_KEY_MAP                                                                                                        \
    {                                                                                                                          \
        INPUT_BROKER_SELECT, /* ROW0·COL0 */                                                                                 \
        INPUT_BROKER_UP,     /* ROW0·COL1 */                                                                                 \
        INPUT_BROKER_DOWN,   /* ROW0·COL2 */                                                                                 \
        INPUT_BROKER_LEFT,   /* ROW0·COL3 */                                                                                 \
        INPUT_BROKER_RIGHT,  /* ROW1·COL0 */                                                                                 \
        INPUT_BROKER_CANCEL, /* ROW1·COL1 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW1·COL2 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW1·COL3 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW2·COL0 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW2·COL1 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW2·COL2 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW2·COL3 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW3·COL0 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW3·COL1 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW3·COL2 */                                                                                 \
        INPUT_BROKER_NONE,   /* ROW3·COL3 */                                                                                 \
    }


#define RA_01SC_P

#ifdef RA_01SC_P
#define SETTING_MAX_POWER 3
#define TX_GAIN_LORA 0
#define SX126X_MAX_POWER 3
#endif

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

// LoRa RST 通过 TCA9535 P1.4 控制
// 使用虚拟引脚号 200，由自定义 HAL 拦截并转发到 I²C
#define TCA9535_LORA_RST_VIRTUAL_PIN 200
#define TCA9535_LORA_RST_REG     TCA9535_REG_OUTPUT_P1  // P1 输出寄存器
#define TCA9535_LORA_RST_BIT     (1u << 4)              // P1.4

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET TCA9535_LORA_RST_VIRTUAL_PIN
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define TCXO_OPTIONAL