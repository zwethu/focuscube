#pragma once

// ─── Wi-Fi ────────────────────────────────────────────────
#define WIFI_SSID "Exploratory"
#define WIFI_PASS "!tggs2025"

// ─── MQTT Broker = Raspberry Pi IP ───────────────────────
#define MQTT_BROKER "192.168.0.164"
#define MQTT_PORT 1883

// ─── Topics ───────────────────────────────────────────────
#define TOPIC_TELE "focuscube/sender/telemetry"
#define TOPIC_PIR  "focuscube/sender/event/pir"
#define TOPIC_CMD  "focuscube/listener/cmd"
#define TOPIC_ACK  "focuscube/listener/ack"

// ─── Shared Pins ──────────────────────────────────────────
#define PIN_TRIG     23
#define PIN_ECHO     18
#define PIN_PIR      32
#define PIN_MQ135_DO 34
#define PIN_MQ135_AO 35
#define PIN_DHT22    33
#define PIN_BUTTON   27
#define PIN_BUZZER   25

// ─── Listener-only Pins ───────────────────────────────────
#define PIN_LED_RED    4
#define PIN_LED_YELLOW 15
#define PIN_LED_GREEN  5
#define PIN_FAN        13

// ─── Thresholds ───────────────────────────────────────────
#define LUX_LOW_THRESHOLD 50.0f
#define FAN_ON_TEMP       27.0f
#define FAN_OFF_TEMP      25.0f
#define MQ135_BAD_DO      1
#define MQ135_MID_LOW     1500
#define MQ135_GOOD_LOW    2500
#define DIST_NEAR_CM      45.0f
#define DIST_FAR_CM       65.0f
#define TELE_INTERVAL     5000

// ─── Current Arduino OLED / I2C config ───────────────────
#define I2C_SDA_PIN   21
#define I2C_SCL_PIN   22

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_ADDR     0x3C

// ─── Compatibility macros for old oled.c driver ──────────
#define OLED_SDA_GPIO I2C_SDA_PIN
#define OLED_SCL_GPIO I2C_SCL_PIN
#define OLED_I2C_PORT I2C_NUM_0
#define OLED_WIDTH    SCREEN_WIDTH
#define OLED_HEIGHT   SCREEN_HEIGHT 