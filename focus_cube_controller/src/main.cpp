#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

#ifdef DEVICE_LISTENER
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
  bool fanOn        = false;
  int  lastPirState = LOW;
#endif

DHT          dht(PIN_DHT22, DHT22);
BH1750       lightMeter;
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

float readDistance() {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(3);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long d = pulseIn(PIN_ECHO, HIGH, 30000);
    return d == 0 ? -1.0f : d / 58.0f;
}

void connectWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
}

#ifdef DEVICE_LISTENER
void onCommand(char* topic, byte* payload, unsigned int len) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, len)) return;
    const char* action = doc["payload"]["action"] | "";
    Serial.printf("[CMD] action=%s\n", action);
    if (strcmp(action, "play") == 0) {
        for (int i = 0; i < 3; i++) {
            digitalWrite(PIN_BUZZER, HIGH); delay(200);
            digitalWrite(PIN_BUZZER, LOW);  delay(200);
        }
        mqtt.publish(TOPIC_ACK, "{\"status\":\"done\",\"action\":\"play\"}");
    } else if (strcmp(action, "stop") == 0) {
        digitalWrite(PIN_BUZZER, LOW);
        mqtt.publish(TOPIC_ACK, "{\"status\":\"done\",\"action\":\"stop\"}");
    }
}
#endif

void connectMQTT() {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    #ifdef DEVICE_LISTENER
    mqtt.setCallback(onCommand);
    #endif
    #ifdef DEVICE_SENDER
    const char* nodeId = "focuscube-sender";
    #else
    const char* nodeId = "focuscube-listener";
    #endif
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(nodeId)) {
            Serial.println("OK");
            #ifdef DEVICE_LISTENER
            mqtt.subscribe(TOPIC_CMD, 1);
            #endif
        } else {
            Serial.printf("failed rc=%d retry 3s\n", mqtt.state());
            delay(3000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_TRIG,     OUTPUT);
    pinMode(PIN_ECHO,     INPUT);
    pinMode(PIN_PIR,      INPUT);
    pinMode(PIN_MQ135_DO, INPUT);
    pinMode(PIN_BUTTON,   INPUT_PULLUP);
    pinMode(PIN_BUZZER,   OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    #ifdef DEVICE_LISTENER
    pinMode(PIN_LED_RED,    OUTPUT);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    pinMode(PIN_LED_GREEN,  OUTPUT);
    pinMode(PIN_FAN,        OUTPUT);
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_FAN, LOW);
    #endif

    dht.begin();
    Wire.begin(21, 22);
    lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

    #ifdef DEVICE_LISTENER
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(20, 25);
        display.println("FocusCube...");
        display.display();
    }
    #endif

    connectWiFi();
    connectMQTT();
}

void loop() {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

    float temp     = dht.readTemperature();
    float hum      = dht.readHumidity();
    float dist     = readDistance();
    int   pir      = digitalRead(PIN_PIR);
    int   mq_raw   = analogRead(PIN_MQ135_AO);
    int   mq_alert = digitalRead(PIN_MQ135_DO);
    float lux      = lightMeter.readLightLevel();

#ifdef DEVICE_SENDER
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
        Serial.printf("[TX] temp=%.1f lux=%.0f dist=%.1f\n", temp, lux, dist);
    }
    static int lastPir = LOW;
    if (pir == HIGH && lastPir == LOW)
        mqtt.publish(TOPIC_PIR,
            "{\"event\":\"motion_detected\",\"node\":\"focuscube-sender\"}");
    lastPir = pir;
    digitalWrite(PIN_BUZZER, pir == HIGH ? HIGH : LOW);
#endif

#ifdef DEVICE_LISTENER
    if (pir == HIGH && lastPirState == LOW) {
        digitalWrite(PIN_BUZZER, HIGH); delay(200);
        digitalWrite(PIN_BUZZER, LOW);
    }
    lastPirState = pir;
    if (!isnan(temp)) {
        if (temp >= FAN_ON_TEMP)  fanOn = true;
        if (temp <= FAN_OFF_TEMP) fanOn = false;
    }
    digitalWrite(PIN_FAN, fanOn ? HIGH : LOW);
    digitalWrite(PIN_LED_RED,    lux < 50  ? HIGH : LOW);
    digitalWrite(PIN_LED_YELLOW, (lux >= 50 && lux < 200) ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN,  lux >= 200 ? HIGH : LOW);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,  0); display.println("=== FocusCube ===");
    display.setCursor(0, 12); display.printf("T:%.1fC  H:%.0f%%\n", temp, hum);
    display.setCursor(0, 22); display.printf("Air:%d %s\n", mq_raw, mq_alert ? "BAD" : "OK");
    display.setCursor(0, 32); display.printf("Lux: %.0f lx\n", lux);
    display.setCursor(0, 42); display.printf("Dist: %.1f cm\n", dist);
    display.setCursor(0, 52); display.printf("Fan:%s PIR:%s\n",
        fanOn ? "ON":"OFF", pir ? "ON":"---");
    display.display();
#endif

    delay(300);
}