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
//   - Subscribes to TOPIC_TELE → uses Sender’s temp for fan
// ══════════════════════════════════════════════════════════


// ── Listener-only includes + objects ─────────────────────
#ifdef DEVICE_LISTENER
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool fanOn = false;      // fan state — persists between loops
bool buzzerBusy = false; // (kept for future use)
int  lastPirState = LOW; // tracks previous PIR state for rising edge

// values coming from Sender telemetry
float remoteTemp = NAN;
float remoteHum  = NAN;
float remoteLux  = NAN;
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

float readDistance() {
    float sum = 0;
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_TRIG, LOW);
        delayMicroseconds(3);
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);
        long d = pulseIn(PIN_ECHO, HIGH, 30000);
        if (d > 0) {
            sum += d / 58.0f;
            ok++;
        }
        delay(30);
    }
    return ok ? sum / ok : -1.0f;
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            Serial.println("\n[WiFi] FAILED");
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting as focuscube-sender ...");
        if (mqtt.connect("focuscube-sender")) {
            Serial.println("OK");
        } else {
            Serial.printf("FAILED rc=%d retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[SENDER] Booting...");

    pinMode(PIN_TRIG, OUTPUT);         // HC-SR04 trigger
    pinMode(PIN_ECHO, INPUT);          // HC-SR04 echo
    pinMode(PIN_PIR, INPUT);           // PIR motion sensor
    pinMode(PIN_MQ135_DO, INPUT);      // MQ135 digital alert
    pinMode(PIN_MQ135_AO, INPUT);      // MQ135 analog raw
    pinMode(PIN_BUTTON, INPUT_PULLUP); // tactile button
    pinMode(PIN_BUZZER, OUTPUT);       // active buzzer
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

void loop() {
    if (!mqtt.connected())
        connectMQTT();
    mqtt.loop();

    float temp    = dht.readTemperature();      // °C
    float hum     = dht.readHumidity();         // %
    float dist    = readDistance();             // cm, -1 if no echo
    int   pir     = digitalRead(PIN_PIR);       // HIGH = motion
    int   mq_raw  = analogRead(PIN_MQ135_AO);   // 0–4095
    int   mq_alert= digitalRead(PIN_MQ135_DO);  // HIGH = bad air
    float lux     = lightMeter.readLightLevel();// lux

    bool buzzerRule = (pir == HIGH);
    if (buzzerRule) {
        digitalWrite(PIN_BUZZER, LOW);
        delay(1000);
        digitalWrite(PIN_BUZZER, HIGH);
        delay(2000);
        Serial.println("[RULE] Buzzer beeping");
    } else {
        digitalWrite(PIN_BUZZER, HIGH);
    }

    static unsigned long lastSend = 0;
    if (millis() - lastSend >= TELE_INTERVAL) {
        lastSend = millis();

        StaticJsonDocument<512> doc;
        doc["node_id"] = "focuscube-sender";
        doc["type"]    = "telemetry";
        JsonObject s   = doc["payload"].createNestedObject("sensors");
        s["temp_c"]    = isnan(temp) ? -1 : roundf(temp * 10) / 10;
        s["humidity"]  = isnan(hum)  ? -1 : roundf(hum  * 10) / 10;
        s["lux"]       = lux;
        s["dist_cm"]   = dist;
        s["mq135_raw"] = mq_raw;
        s["mq135_alert"] = mq_alert;
        s["pir"]       = pir;
        doc["rssi"]    = WiFi.RSSI();

        char buf[512];
        serializeJson(doc, buf);
        mqtt.publish(TOPIC_TELE, buf, true);

        Serial.println("────────── FocusSense TX ──────────");
        Serial.printf("  Temp     : %.1f C\n", isnan(temp) ? -1 : temp);
        Serial.printf("  Humidity : %.1f %%\n", isnan(hum) ? -1 : hum);
        Serial.printf("  Lux      : %.1f lux\n", lux);
        Serial.printf("  Distance : %.1f cm\n", dist);
        Serial.printf("  PIR      : %s\n", pir ? "MOTION" : "still");
        Serial.printf("  MQ135    : raw=%d  alert=%s\n", mq_raw, mq_alert ? "BAD" : "OK");
        Serial.printf("  RSSI     : %d dBm\n", WiFi.RSSI());
        Serial.println("────────────────────────────────────\n");
    }
}

#endif // DEVICE_SENDER


// ══════════════════════════════════════════════════════════
// ────────────────────── LISTENER ──────────────────────────
// ══════════════════════════════════════════════════════════
#ifdef DEVICE_LISTENER

void setTrafficLight(float lux) {
    // turn all off first
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);

    if (lux < 50.0f) {
        digitalWrite(PIN_LED_RED, HIGH);
    } else if (lux < 200.0f) {
        digitalWrite(PIN_LED_YELLOW, HIGH);
    } else {
        digitalWrite(PIN_LED_GREEN, HIGH);
    }
}

void showOLED(float temp, float hum, int mq_raw, int mq_alert, float lux, bool fan) {
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

// ── MQTT callback: receive Sender telemetry ───────────────
void onTelemetry(char *topic, byte *payload, unsigned int len) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.println("[MQTT RX] Failed to parse telemetry JSON");
        return;
    }

    JsonObject s = doc["payload"]["sensors"];
    remoteTemp = s["temp_c"]  | NAN;
    remoteHum  = s["humidity"]| NAN;
    remoteLux  = s["lux"]     | NAN;

    Serial.printf("[MQTT RX] remoteTemp=%.1f  remoteHum=%.1f  remoteLux=%.1f\n",
                  remoteTemp, remoteHum, remoteLux);
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            Serial.println("\n[WiFi] FAILED");
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(onTelemetry);

    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting as focuscube-listener ...");
        if (mqtt.connect("focuscube-listener")) {
            Serial.println("OK");
            mqtt.subscribe(TOPIC_TELE, 1);
        } else {
            Serial.printf("FAILED rc=%d retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[LISTENER] Booting...");

    pinMode(PIN_PIR, INPUT);           // PIR motion sensor
    pinMode(PIN_MQ135_DO, INPUT);      // MQ135 digital alert
    pinMode(PIN_MQ135_AO, INPUT);      // MQ135 analog raw
    pinMode(PIN_BUTTON, INPUT_PULLUP); // tactile button
    pinMode(PIN_LED_RED, OUTPUT);      // traffic light red
    pinMode(PIN_LED_YELLOW, OUTPUT);   // traffic light yellow
    pinMode(PIN_LED_GREEN, OUTPUT);    // traffic light green
    pinMode(PIN_FAN, OUTPUT);          // fan relay/transistor

    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_FAN, HIGH); // assume active-LOW relay: HIGH = OFF

    dht.begin();        // still available if you want local DHT later
    Wire.begin(21, 22); // I2C: SDA=21, SCL=22

    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
        Serial.println("[SENSOR] BH1750 ready");
    else
        Serial.println("[SENSOR] BH1750 not found - check wiring");

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
        Serial.println("[OLED] Not found - check wiring");
    else {
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

void loop() {
    if (!mqtt.connected())
        connectMQTT();
    mqtt.loop();

    // ── Local sensors only (lux/temp/hum come from Sender via MQTT) ──
    int pir     = digitalRead(PIN_PIR);
    int mq_raw  = analogRead(PIN_MQ135_AO);
    int mq_alert= digitalRead(PIN_MQ135_DO);

    Serial.printf("[MQTT/FAN] remoteTemp=%.2f  fanOn=%s\n",
                  remoteTemp, fanOn ? "ON" : "OFF");

    // ── Rule 1: Fan hysteresis using remoteTemp ───────────
    if (isnan(remoteTemp)) {
        Serial.println("[FAN DEBUG] remoteTemp is NaN -> no telemetry yet");
    } else {
        if (remoteTemp >= FAN_ON_TEMP) {
            fanOn = true;
            Serial.println("[FAN] threshold reached (remote), turning ON");
        }
        if (remoteTemp <= FAN_OFF_TEMP) {
            fanOn = false;
            Serial.println("[FAN] threshold reached (remote), turning OFF");
        }
    }

    // active-LOW relay: LOW = ON, HIGH = OFF
    digitalWrite(PIN_FAN, fanOn ? HIGH : LOW);

    // ── Rule 2: Traffic light LEDs using remoteLux ────────
    if (isnan(remoteLux))
        Serial.println("[LUX DEBUG] remoteLux is NaN -> no telemetry yet");
    else {
        Serial.printf("[LUX DEBUG] remoteLux=%.1f lux\n", remoteLux);
        setTrafficLight(remoteLux);
    }

    // ── Rule 3: PIR log on motion start ───────────────────
    if (pir == HIGH && lastPirState == LOW)
        Serial.println("[PIR] Motion detected");
    lastPirState = pir;

    // ── Rule 4: OLED update ───────────────────────────────
    showOLED(remoteTemp, remoteHum, mq_raw, mq_alert, remoteLux, fanOn);

    Serial.printf("[LISTENER] temp=%.1f fan=%s lux=%.0f air=%s pir=%s\n",
                  remoteTemp,
                  fanOn ? "ON" : "OFF",
                  remoteLux,
                  mq_alert ? "BAD" : "OK",
                  pir == HIGH ? "MOTION" : "still");

    delay(500);
}

#endif // DEVICE_LISTENER