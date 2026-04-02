# src/gateway/config.py
from __future__ import annotations

from pathlib import Path
from typing import Any
import tomllib

from pydantic import BaseModel, Field


class MQTTConfig(BaseModel):
    host:          str       = "127.0.0.1"
    port:          int       = 1883
    username:      str | None = None
    password:      str | None = None
    client_id:     str       = "focuscube-gateway"
    keepalive_sec: int       = 60


class TopicConfig(BaseModel):
    # What the gateway subscribes to (inbound from ESP32s)
    subscriptions: list[str] = Field(
        default_factory=lambda: [
            "focuscube/sender/telemetry",
            "focuscube/sender/event/pir",
            "focuscube/listener/ack",
        ]
    )
    # Topics the gateway publishes TO
    cmd_topic: str = "focuscube/listener/cmd"


class ThresholdConfig(BaseModel):
    """All tunable focus-scoring thresholds in one place."""
    temp_min:       float = 20.0   # °C
    temp_max:       float = 27.0   # °C
    lux_min:        float = 200.0  # lx
    lux_max:        float = 1000.0 # lx
    dist_near_cm:   float = 50.0
    dist_far_cm:    float = 65.0
    mq135_bad_do:   int   = 1      # digital HIGH = bad air
    mq135_mid_low:  int   = 1500
    mq135_good_low: int   = 2500


class StateConfig(BaseModel):
    """In-memory ring buffer size."""
    telemetry_maxlen: int = 100
    focus_maxlen:     int = 100
    event_maxlen:     int = 200


class LoggingConfig(BaseModel):
    directory:    str = "./logs"
    app_file:     str = "gateway.log"
    message_file: str = "messages.log"
    level:        str = "INFO"


class ObservabilityConfig(BaseModel):
    enabled: bool = True
    host:    str  = "0.0.0.0"
    port:    int  = 8080


class APIConfig(BaseModel):
    host:  str  = "0.0.0.0"
    port:  int  = 5000
    debug: bool = False


class AppConfig(BaseModel):
    mqtt:          MQTTConfig          = Field(default_factory=MQTTConfig)
    topics:        TopicConfig         = Field(default_factory=TopicConfig)
    thresholds:    ThresholdConfig     = Field(default_factory=ThresholdConfig)
    state:         StateConfig         = Field(default_factory=StateConfig)
    logging:       LoggingConfig       = Field(default_factory=LoggingConfig)
    observability: ObservabilityConfig = Field(default_factory=ObservabilityConfig)
    api:           APIConfig           = Field(default_factory=APIConfig)


def load_config(path: str | Path | None) -> AppConfig:
    if path is None:
        return AppConfig()
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"Config file not found: {p}")
    with p.open("rb") as f:
        raw: dict[str, Any] = tomllib.load(f)
    return AppConfig.model_validate(raw)