# src/gateway/models.py
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from pydantic import BaseModel, Field


def utc_now_iso() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


# ── Inbound: what the ESP32 Sender publishes ──────────────────────

class SensorPayload(BaseModel):
    """Typed view of sensors{} inside a telemetry message."""
    temp_c:       float | None = None
    humidity:     float | None = None
    lux:          float | None = None
    dist_cm:      float | None = None
    mq135_raw:    int   | None = None
    mq135_alert:  int   | None = None   # 1 = bad air, 0 = ok
    pir:          int   | None = None   # 1 = motion, 0 = none
    sound_do:     int   | None = None   # 1 = sound detected
    sound_level:  int   | None = None   # analog 0-4095


class TelemetryMessage(BaseModel):
    """Full telemetry packet from focuscube-sender."""
    node_id:  str
    type:     str = "telemetry"
    payload:  dict[str, Any] = Field(default_factory=dict)
    rssi:     int | None = None

    @property
    def sensors(self) -> SensorPayload:
        return SensorPayload.model_validate(
            self.payload.get("sensors", {})
        )


# ── Canonical event (workshop pattern) ───────────────────────────

class CanonicalEvent(BaseModel):
    """Normalised envelope for every inbound MQTT message."""
    ts:         str  = Field(default_factory=utc_now_iso)
    source:     str                          # "focuscube-sender" | "focuscube-listener"
    category:   str                          # "telemetry" | "event.pir" | "actuator.ack"
    topic:      str
    qos:        int
    retained:   bool = False
    node_id:    str | None = None
    msg_id:     str | None = None
    payload:    dict[str, Any] = Field(default_factory=dict)
    raw:        dict[str, Any] = Field(default_factory=dict)


# ── Focus scoring ─────────────────────────────────────────────────

class FocusComponents(BaseModel):
    temp_ok:     bool = False   # temperature in comfortable range
    light_ok:    bool = False   # lux in good range
    air_ok:      bool = False   # MQ135 not alerting
    presence_ok: bool = False   # person detected at desk
    quiet_ok:    bool = False   # no disruptive sound


class FocusScore(BaseModel):
    ts:         str = Field(default_factory=utc_now_iso)
    score:      int                          # 0–100
    label:      str                          # FOCUSED | LIGHT_DISTRACTION | DISTRACTED | DEGRADED | AWAY
    components: FocusComponents = Field(default_factory=FocusComponents)
    sensors:    SensorPayload   = Field(default_factory=SensorPayload)
    derived:    dict            = Field(default_factory=dict)


# ── Outbound: commands sent TO the ESP32 Listener ─────────────────

class ListenerCommand(BaseModel):
    """Command published to focuscube/listener/cmd."""
    action:  str                             # "play" | "stop"
    reason:  str | None = None