# src/gateway/predictor.py
from __future__ import annotations
import joblib
import numpy as np
from pathlib import Path

_MODEL_PATH = Path(__file__).resolve().parent.parent.parent / "models" / "TGGS.joblib"
_model = joblib.load(_MODEL_PATH)

# Label map matching training: 0=Bad, 1=Neutral, 2=Good
_LABEL_MAP = {0: "BAD", 1: "NEUTRAL", 2: "GOOD"}

def predict_mood(
    temp_c: float,
    humidity: float,
    air_quality_index: int,
    lux: float,
) -> str:
    """
    Predict mood level from environmental sensors.
    Feature order MUST match training: [temperatureCelsius, humidityPercent, airQualityIndex, lightingLux]
    Returns: 'BAD', 'NEUTRAL', or 'GOOD'
    """
    features = np.array([[temp_c, humidity, air_quality_index, lux]])
    label_int = int(_model.predict(features)[0])
    return _LABEL_MAP.get(label_int, "NEUTRAL")