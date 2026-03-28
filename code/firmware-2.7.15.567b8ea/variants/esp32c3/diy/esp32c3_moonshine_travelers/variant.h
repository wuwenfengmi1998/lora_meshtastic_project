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


#define RA_01SC_P

#ifdef RA_01SC_P
#define SETTING_MAX_POWER 3
#define TX_GAIN_LORA 0
#define SX126X_MAX_POWER 3
#endif

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET RADIOLIB_NC
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