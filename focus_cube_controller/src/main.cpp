#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ══════════════════════════════════════════════════════════
// SENDER (focuscube-sender)
//   - Reads sensors: DHT22, BH1750, HC-SR04, PIR, MQ135
//   - Rule: buzzer ON when PIR + distance 50–65 cm
//   - Publishes telemetry JSON every 5s  → TOPIC_TELE
//   - Publishes PIR event on rising edge → TOPIC_PIR
//
// LISTENER (focuscube-listener)
//   - Reads sensors: DHT22, BH1750, PIR, MQ135
//   - Rule 1: Fan ON/OFF based on temperature hysteresis
//   - Rule 2: Traffic light LEDs based on lux level
//   - Rule 3: Short buzzer beep on local PIR rising edge
//   - Rule 4: OLED shows live sensor + actuator state
//   - Subscribes to TOPIC_CMD → handles play/stop buzzer
//   - Publishes ACK after executing command → TOPIC_ACK
//
// platformio.ini build flags:
//   Sender   → build_flags = -D DEVICE_SENDER
//   Listener → build_flags = -D DEVICE_LISTENER
// ══════════════════════════════════════════════════════════

// ── Listener-only includes + objects ─────────────────────
#ifdef DEVICE_LISTENER
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool fanOn = false;      // fan state — persists between loops
bool buzzerBusy = false; // prevents PIR beep interrupting a command
int lastPirState = LOW;  // tracks previous PIR state for rising edge
#endif

// ── Shared objects (both devices) ────────────────────────
DHT dht(PIN_DHT22, DHT22);
BH1750 lightMeter;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ══════════════════════════════════════════════════════════
// ─────────────────────── SENDER ───────────────────────────
// ══════════════════════════════════════════════════════════
#ifdef DEVICE_SENDER

// ── readDistance() ────────────────────────────────────────
// Fires HC-SR04 3 times and returns average distance in cm.
// Returns -1 if no echo received within timeout.
float readDistance()
{
    float sum = 0;
    int ok = 0;
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(PIN_TRIG, LOW);
        delayMicroseconds(3);
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);
        long d = pulseIn(PIN_ECHO, HIGH, 30000);
        if (d > 0)
        {
            sum += d / 58.0f;
            ok++;
        }
        delay(30);
    }
    return ok ? sum / ok : -1.0f;
}

// ── connectWiFi() ─────────────────────────────────────────
// Joins the WiFi network from config.h.
// Times out after 15 seconds if AP is unreachable.
void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - start > 15000)
        {
            Serial.println("\n[WiFi] FAILED");
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
}

// ── connectMQTT() ─────────────────────────────────────────
// Connects to Mosquitto broker as "focuscube-sender".
// Sender only publishes — no callback needed.
// Retries every 3 seconds on failure.
void connectMQTT()
{
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    while (!mqtt.connected())
    {
        Serial.print("[MQTT] Connecting as focuscube-sender ...");
        if (mqtt.connect("focuscube-sender"))
        {
            Serial.println("OK");
        }
        else
        {
            Serial.printf("FAILED rc=%d retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

// ── setup() ───────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("[SENDER] Booting...");

    pinMode(PIN_TRIG, OUTPUT);         // HC-SR04 trigger
    pinMode(PIN_ECHO, INPUT);          // HC-SR04 echo
    pinMode(PIN_PIR, INPUT);           // PIR motion sensor
    pinMode(PIN_MQ135_DO, INPUT);      // MQ135 digital alert
    pinMode(PIN_MQ135_AO, INPUT);      // MQ135 analog raw
    pinMode(PIN_BUTTON, INPUT_PULLUP); // tactile button
    pinMode(PIN_BUZZER, OUTPUT);       // active buzzer
    pinMode(PIN_DHT22, INPUT);         // DHT22 temperature/humidity sensor
    pinMode(LUX_LOW_THRESHOLD, INPUT); // BH1750 light sensor
    digitalWrite(PIN_BUZZER, LOW);     // buzzer OFF at boot

    dht.begin();
    Wire.begin(21, 22); // I2C: SDA=21, SCL=22

    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
        Serial.println("[SENSOR] BH1750 ready");
    else
        Serial.println("[SENSOR] BH1750 not found - check wiring");

    connectWiFi();
    connectMQTT();
    Serial.println("[SENDER] Ready");
}

// ── loop() ────────────────────────────────────────────────
void loop()
{
    if (!mqtt.connected())
        connectMQTT();
    mqtt.loop();

    // ── Read all sensors ──────────────────────────────────
    float temp = dht.readTemperature();       // °C
    float hum = dht.readHumidity();           // %
    float dist = readDistance();              // cm, -1 if no echo
    int pir = digitalRead(PIN_PIR);           // HIGH = motion
    int mq_raw = analogRead(PIN_MQ135_AO);    // 0–4095
    int mq_alert = digitalRead(PIN_MQ135_DO); // HIGH = bad air
    float lux = lightMeter.readLightLevel();  // lux

 // ── Rule: Buzzer repeating beep while motion detected ────
bool buzzerRule = (pir == HIGH);

if (buzzerRule) {
    digitalWrite(PIN_BUZZER, LOW); delay(1000);
    digitalWrite(PIN_BUZZER, HIGH);  delay(2000);
    Serial.println("[RULE] Buzzer beeping");
} else {
    digitalWrite(PIN_BUZZER, HIGH);
}

// ── Telemetry publish (every TELE_INTERVAL ms) ────────
static unsigned long lastSend = 0;
if (millis() - lastSend >= TELE_INTERVAL) {
    lastSend = millis();

    StaticJsonDocument<512> doc;
    doc["node_id"] = "focuscube-sender";
    doc["type"]    = "telemetry";
    JsonObject s   = doc["payload"].createNestedObject("sensors");
    s["temp_c"]      = isnan(temp) ? -1 : roundf(temp * 10) / 10;
    s["humidity"]    = isnan(hum)  ? -1 : roundf(hum  * 10) / 10;
    s["lux"]         = lux;
    s["dist_cm"]     = dist;
    s["mq135_raw"]   = mq_raw;
    s["mq135_alert"] = mq_alert;
    s["pir"]         = pir;
    doc["rssi"]      = WiFi.RSSI();

    char buf[512];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_TELE, buf, true);

    Serial.println("────────── FocusSense TX ──────────");
    Serial.printf("  Temp     : %.1f C\n",   isnan(temp) ? -1 : temp);
    Serial.printf("  Humidity : %.1f %%\n",  isnan(hum)  ? -1 : hum);
    Serial.printf("  Lux      : %.1f lux\n", lux);
    Serial.printf("  Distance : %.1f cm\n",  dist);
    Serial.printf("  PIR : %s\n", pir ? "MOTION" : "still");
    Serial.printf("  MQ135    : raw=%d  alert=%s\n", mq_raw, mq_alert ? "BAD" : "OK");
    Serial.printf("  RSSI     : %d dBm\n",   WiFi.RSSI());
    Serial.println("────────────────────────────────────\n");
}

// ── PIR event publish (rising edge only — data only) ──
// This just notifies the RPi gateway that motion started.
// It does NOT send a "play" command to the Listener buzzer.
// static int lastPir = LOW;
// if (pir == HIGH && lastPir == LOW) {
//     mqtt.publish(TOPIC_PIR,
//         "{\"event\":\"motion_detected\",\"node\":\"focuscube-sender\"}");
//     Serial.println("[EVENT] PIR rising edge published");
// }
// lastPir = pir;
}

#endif // DEVICE_SENDER

// ══════════════════════════════════════════════════════════
// ────────────────────── LISTENER ──────────────────────────
// ══════════════════════════════════════════════════════════
#ifdef DEVICE_LISTENER

// ── onCommand() ───────────────────────────────────────────
// MQTT callback triggered when a message arrives on
// TOPIC_CMD. Parses JSON payload.action and either:
//   "play" → beep buzzer 3 times then publish ACK
//   "stop" → silence buzzer then publish ACK
// buzzerBusy flag prevents PIR beep from interrupting
void onCommand(char *topic, byte *payload, unsigned int len)
{
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, len))
        return;

    const char *action = doc["payload"]["action"] | "";
    Serial.printf("[CMD] action=%s\n", action);

    if (strcmp(action, "play") == 0)
    {
        buzzerBusy = true;
        for (int i = 0; i < 3; i++)
        {
            digitalWrite(PIN_BUZZER, HIGH);
            delay(200);
            digitalWrite(PIN_BUZZER, LOW);
            delay(200);
        }
        buzzerBusy = false;
        mqtt.publish(TOPIC_ACK, "{\"status\":\"done\",\"action\":\"play\"}");
    }
    else if (strcmp(action, "stop") == 0)
    {
        buzzerBusy = false; // clear flag on stop too
        digitalWrite(PIN_BUZZER, LOW);
        mqtt.publish(TOPIC_ACK, "{\"status\":\"done\",\"action\":\"stop\"}");
    }
}

// ── beepMotionAlert() ─────────────────────────────────────
// Fires a single 200ms buzzer beep on PIR rising edge.
// Skipped entirely if buzzerBusy is true (command running).
void beepMotionAlert()
{
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
}

// ── setTrafficLight() ─────────────────────────────────────
// Turns on one LED based on lux from local BH1750:
//   lux < 50   → RED    (too dark — bad for focus)
//   lux < 200  → YELLOW (moderate — acceptable)
//   lux >= 200 → GREEN  (good lighting for focus)
void setTrafficLight(float lux)
{
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);

    if (lux < 50)
        digitalWrite(PIN_LED_RED, HIGH);
    else if (lux < 200)
        digitalWrite(PIN_LED_YELLOW, HIGH);
    else
        digitalWrite(PIN_LED_GREEN, HIGH);
}

// ── showOLED() ────────────────────────────────────────────
// Redraws OLED every loop with latest sensor values:
//   Row 0 (y= 0): header
//   Row 1 (y=12): temperature + fan state
//   Row 2 (y=22): humidity
//   Row 3 (y=32): air quality raw value + alert flag
//   Row 4 (y=42): lux + LOW warning if below threshold
void showOLED(float temp, float hum, int mq_raw, int mq_alert, float lux, bool fan)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("=== FocusCube ===");

    display.setCursor(0, 12);
    if (!isnan(temp))
        display.printf("T:%.1fC  Fan:%s", temp, fan ? "ON" : "OFF");
    else
        display.println("T: Error");

    display.setCursor(0, 22);
    if (!isnan(hum))
        display.printf("H:%.0f%%", hum);

    display.setCursor(0, 32);
    display.printf("Air:%s (%d)", mq_alert ? "BAD" : "OK", mq_raw);

    display.setCursor(0, 42);
    display.printf("Lux:%.0f%s", lux, lux < LUX_LOW_THRESHOLD ? " LOW!" : " lx");

    display.display();
}

// ── connectWiFi() ─────────────────────────────────────────
// Joins the WiFi network from config.h.
// Times out after 15 seconds if AP is unreachable.
void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - start > 15000)
        {
            Serial.println("\n[WiFi] FAILED");
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
}

// ── connectMQTT() ─────────────────────────────────────────
// Connects to Mosquitto broker as "focuscube-listener".
// Registers onCommand callback and subscribes to TOPIC_CMD
// with QoS 1 so no commands are missed on reconnect.
// Retries every 3 seconds on failure.
void connectMQTT()
{
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(onCommand);
    while (!mqtt.connected())
    {
        Serial.print("[MQTT] Connecting as focuscube-listener ...");
        if (mqtt.connect("focuscube-listener"))
        {
            Serial.println("OK");
            mqtt.subscribe(TOPIC_CMD, 1); // QoS 1 — no missed commands
        }
        else
        {
            Serial.printf("FAILED rc=%d retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

// ── setup() ───────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("[LISTENER] Booting...");

    pinMode(PIN_PIR, INPUT);           // PIR motion sensor
    pinMode(PIN_MQ135_DO, INPUT);      // MQ135 digital alert
    pinMode(PIN_MQ135_AO, INPUT);      // MQ135 analog raw
    pinMode(PIN_BUTTON, INPUT_PULLUP); // tactile button
    pinMode(PIN_BUZZER, OUTPUT);       // active buzzer
    pinMode(PIN_LED_RED, OUTPUT);      // traffic light red
    pinMode(PIN_LED_YELLOW, OUTPUT);   // traffic light yellow
    pinMode(PIN_LED_GREEN, OUTPUT);    // traffic light green
    pinMode(PIN_FAN, OUTPUT);          // fan relay/transistor

    // All outputs OFF at boot
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_FAN, LOW);

    dht.begin();
    Wire.begin(21, 22); // I2C: SDA=21, SCL=22

    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
        Serial.println("[SENSOR] BH1750 ready");
    else
        Serial.println("[SENSOR] BH1750 not found - check wiring");

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
        Serial.println("[OLED] Not found - check wiring");
    else
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(20, 25);
        display.println("FocusCube...");
        display.display();
    }

    connectWiFi();
    connectMQTT();
    Serial.println("[LISTENER] Ready");
}

// ── loop() ────────────────────────────────────────────────
void loop()
{
    if (!mqtt.connected())
        connectMQTT();
    mqtt.loop(); // process incoming TOPIC_CMD messages

    // ── Read all sensors ──────────────────────────────────
    float temp = dht.readTemperature();       // °C
    float hum = dht.readHumidity();           // %
    int pir = digitalRead(PIN_PIR);           // HIGH = motion
    int mq_raw = analogRead(PIN_MQ135_AO);    // 0–4095
    int mq_alert = digitalRead(PIN_MQ135_DO); // HIGH = bad air
    float lux = lightMeter.readLightLevel();  // lux

    // ── Rule 1: Fan hysteresis ────────────────────────────
    // ON  when temp rises above FAN_ON_TEMP  (30°C)
    // OFF when temp falls below FAN_OFF_TEMP (28°C)
    // The 2°C gap prevents rapid ON/OFF switching
    if (!isnan(temp))
    {
        if (temp >= FAN_ON_TEMP)
            fanOn = true;
        if (temp <= FAN_OFF_TEMP)
            fanOn = false;
    }
    digitalWrite(PIN_FAN, fanOn ? HIGH : LOW);

    // ── Rule 2: Traffic light LEDs ────────────────────────
    // Reads lux from local BH1750 and lights one LED.
    // Debug line below — remove once LED works correctly
    Serial.printf("[LUX DEBUG] %.1f lux\n", lux);
    setTrafficLight(lux);

    // ── Rule 3: PIR beep on motion start ─────────────────
    // Triggers beepMotionAlert() only on LOW→HIGH edge.
    // Skipped if buzzerBusy=true (command is running).
    if (pir == HIGH && lastPirState == LOW && !buzzerBusy)
    {
        beepMotionAlert();
        Serial.println("[PIR] Motion detected → beep");
    }
    lastPirState = pir;

    // ── Rule 4: OLED update ───────────────────────────────
    showOLED(temp, hum, mq_raw, mq_alert, lux, fanOn);

    Serial.printf("[LISTENER] temp=%.1f fan=%s lux=%.0f air=%s pir=%s\n",
                  temp,
                  fanOn ? "ON" : "OFF",
                  lux,
                  mq_alert ? "BAD" : "OK",
                  pir == HIGH ? "MOTION" : "still");

    delay(300);
}

#endif // DEVICE_LISTENER