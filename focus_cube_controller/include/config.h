#pragma once

// ─── Wi-Fi ────────────────────────────────────────────────
#define WIFI_SSID   "your_wifi_ssid"
#define WIFI_PASS   "your_wifi_password"

// ─── MQTT Broker = Raspberry Pi IP ───────────────────────
#define MQTT_BROKER "192.168.x.x"
#define MQTT_PORT   1883

// ─── Topics ───────────────────────────────────────────────
#define TOPIC_TELE  "focuscube/sender/telemetry"
#define TOPIC_PIR   "focuscube/sender/event/pir"
#define TOPIC_CMD   "focuscube/listener/cmd"
#define TOPIC_ACK   "focuscube/listener/ack"

// ─── Shared Pins ──────────────────────────────────────────
#define PIN_TRIG       23
#define PIN_ECHO       18
#define PIN_PIR        32
#define PIN_MQ135_DO   34
#define PIN_MQ135_AO   35
#define PIN_DHT22      33
#define PIN_BUTTON     27
#define PIN_BUZZER     25

// ─── Listener-only Pins ───────────────────────────────────
#define PIN_LED_RED    15
#define PIN_LED_YELLOW 2
#define PIN_LED_GREEN  5
#define PIN_FAN        13

// ─── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

// ─── Thresholds ───────────────────────────────────────────
#define FAN_ON_TEMP   30.0
#define FAN_OFF_TEMP  28.0
#define TELE_INTERVAL 5000