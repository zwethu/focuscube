# test_model.py
import joblib
import numpy as np

model = joblib.load("TGGS.joblib")

LABEL_MAP = {0: "BAD", 1: "NEUTRAL", 2: "GOOD"}

test_cases = [
    # (temp_c, humidity, aqi, lux,  expected)
    (24.0,  63.0,   67,  323,  "GOOD"),     # normal comfortable
    (33.5,  66.0,  112,  266,  "BAD"),      # too hot
    (20.0,  75.0,   27,  274,  "NEUTRAL"),  # cool + humid
    (26.0,  52.0,   61,  338,  "GOOD"),     # good conditions
    (26.0,  58.0,  142,  243,  "BAD"),      # bad air quality
]

print(f"{'Temp':>6} {'Hum':>6} {'AQI':>6} {'Lux':>6} | {'Expected':<10} {'Predicted':<10} {'Match'}")
print("-" * 60)

for temp, hum, aqi, lux, expected in test_cases:
    features = np.array([[temp, hum, aqi, lux]])
    raw = int(model.predict(features)[0])
    predicted = LABEL_MAP.get(raw, "UNKNOWN")
    match = "✓" if predicted == expected else "✗"
    print(f"{temp:>6.1f} {hum:>6.1f} {aqi:>6} {lux:>6} | {expected:<10} {predicted:<10} {match}")