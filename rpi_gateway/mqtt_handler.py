import json
import threading
import paho.mqtt.client as mqtt
from config import MQTT_BROKER, MQTT_PORT, TOPIC_TELEMETRY, TOPIC_PIR

latest_data = {
    "temp_c":       "--",
    "humidity":     "--",
    "lux":          "--",
    "dist_cm":      "--",
    "mq135_raw":    "--",
    "mq135_alert":  "--",
    "pir":          "--",
    "sound_do":     "--",
    "rssi":         "--",
    "last_pir_event": None,
    "status":       "Waiting for data...",
}

def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[MQTT] Connected to broker")
        client.subscribe(TOPIC_TELEMETRY)
        client.subscribe(TOPIC_PIR)
        latest_data["status"] = "Connected"
    else:
        print(f"[MQTT] Connection failed rc={rc}")
        latest_data["status"] = f"Connection failed (rc={rc})"

def _on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except json.JSONDecodeError:
        return

    if msg.topic == TOPIC_TELEMETRY:
        sensors = payload.get("payload", {}).get("sensors", {})
        latest_data.update({
            "temp_c":      sensors.get("temp_c",      "--"),
            "humidity":    sensors.get("humidity",    "--"),
            "lux":         sensors.get("lux",         "--"),
            "dist_cm":     sensors.get("dist_cm",     "--"),
            "mq135_raw":   sensors.get("mq135_raw",   "--"),
            "mq135_alert": sensors.get("mq135_alert", "--"),
            "pir":         sensors.get("pir",         "--"),
            "sound_do":    sensors.get("sound_do",    "--"),
            "rssi":        payload.get("rssi",        "--"),
            "status":      "Live",
        })

    elif msg.topic == TOPIC_PIR:
        latest_data["last_pir_event"] = payload.get("event", "motion_detected")

def get_latest_data():
    return latest_data

def start_mqtt():
    client = mqtt.Client(client_id="focuscube-gateway")
    client.on_connect = _on_connect
    client.on_message = _on_message
    client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_forever()

# Start in a background thread so Flask is not blocked
_thread = threading.Thread(target=start_mqtt, daemon=True)
_thread.start()