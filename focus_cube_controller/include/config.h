#pragma once

// ─── Wi-Fi ────────────────────────────────────────────────
#define WIFI_SSID   "Timmy's"
#define WIFI_PASS   "forwifilol"

// ─── MQTT Broker = Raspberry Pi IP ───────────────────────
#define MQTT_BROKER "10.223.93.145"
#define MQTT_PORT   1883

// ─── Topics ───────────────────────────────────────────────
#define TOPIC_TELE  "focuscube/sender/telemetry"
#define TOPIC_PIR   "focuscube/sender/event/pir"
#define TOPIC_CMD   "focuscube/listener/cmd"
#define TOPIC_ACK   "focuscube/listener/ack"

// ─── Shared Pins  inputs ──────────────────────────────────────────
#define PIN_TRIG        23
#define PIN_ECHO        18
#define PIN_PIR         32
#define PIN_MQ135_DO    34
#define PIN_MQ135_AO    35
#define PIN_DHT22       33
#define PIN_BUTTON      27
#define PIN_BUZZER      25
// ─── Thresholds: Light (BH1750) ───────────────────────────
#define LUX_LOW_THRESHOLD  50.0f   // below this = low light warning

// todo


// ─── Listener-only Pins ───────────────────────────────────
#define PIN_LED_RED     21
#define PIN_LED_YELLOW  15
#define PIN_LED_GREEN    5
#define PIN_FAN         13
// todo

// ─── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDR     0x3C

// ─── Thresholds: Temperature / Fan ────────────────────────
#define FAN_ON_TEMP   27.5f
#define FAN_OFF_TEMP  27.0f

// ─── Thresholds: Air Quality (MQ135 analog) ───────────────
// Raw ADC 0–4095 on ESP32 12-bit
#define MQ135_BAD_DO     1      // digital pin HIGH = bad air
#define MQ135_MID_LOW  1500     // analog >= this → mid range
#define MQ135_GOOD_LOW 2500     // analog >= this → good air

// ─── Thresholds: Distance (HC-SR04) ───────────────────────
#define DIST_NEAR_CM   45.0f
#define DIST_FAR_CM    65.0f


// ─── Telemetry interval ───────────────────────────────────
#define TELE_INTERVAL  5000