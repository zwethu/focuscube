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
#define PIN_TRIG        23
#define PIN_ECHO        18
#define PIN_PIR         32
#define PIN_MQ135_DO    34
#define PIN_MQ135_AO    35
#define PIN_DHT22       33
#define PIN_BUTTON      27
#define PIN_BUZZER      25
#define PIN_SOUND_DO    26   // Sound sensor digital output

// ─── Listener-only Pins ───────────────────────────────────
#define PIN_LED_RED     15
#define PIN_LED_YELLOW   2
#define PIN_LED_GREEN    5
#define PIN_FAN         13

// ─── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDR     0x3C

// ─── Thresholds: Temperature / Fan ────────────────────────
#define FAN_ON_TEMP   30.0f
#define FAN_OFF_TEMP  28.0f

// ─── Thresholds: Air Quality (MQ135 analog) ───────────────
// Raw ADC 0–4095 on ESP32 12-bit
#define MQ135_BAD_DO     1      // digital pin HIGH = bad air
#define MQ135_MID_LOW  1500     // analog >= this → mid range
#define MQ135_GOOD_LOW 2500     // analog >= this → good air

// ─── Thresholds: Distance (HC-SR04) ───────────────────────
#define DIST_NEAR_CM   50.0f
#define DIST_FAR_CM    65.0f

// ─── Thresholds: Light (BH1750) ───────────────────────────
#define LUX_LOW_THRESHOLD  50.0f   // below this = low light warning

// ─── Telemetry interval ───────────────────────────────────
#define TELE_INTERVAL  5000