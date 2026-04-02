# src/gateway/topic_parser.py
from __future__ import annotations

from typing import Any

from gateway.models import CanonicalEvent


def parse_incoming(
    topic: str,
    payload: dict[str, Any],
    qos: int,
    retained: bool,
) -> CanonicalEvent | None:
    """
    Map focuscube MQTT topics to CanonicalEvent.

    Supported topics:
      focuscube/sender/telemetry         → category="telemetry"
      focuscube/sender/event/pir         → category="event.pir"
      focuscube/listener/ack             → category="actuator.ack"
    """
    parts = topic.split("/")

    # ── Sender topics: focuscube/sender/{channel...} ──────────────
    if len(parts) >= 3 and parts[0] == "focuscube" and parts[1] == "sender":
        channel = "/".join(parts[2:])
        category = _sender_category(channel, payload)
        return CanonicalEvent(
            source="focuscube-sender",
            category=category,
            topic=topic,
            qos=qos,
            retained=retained,
            node_id=payload.get("node_id"),
            msg_id=payload.get("msg_id"),
            payload=payload.get("payload", payload),  # normalise nested or flat
            raw=payload,
        )

    # ── Listener ACK: focuscube/listener/ack ──────────────────────
    if len(parts) == 3 and parts[0] == "focuscube" and parts[1] == "listener" and parts[2] == "ack":
        return CanonicalEvent(
            source="focuscube-listener",
            category="actuator.ack",
            topic=topic,
            qos=qos,
            retained=retained,
            node_id=payload.get("node_id"),
            payload=payload,
            raw=payload,
        )

    return None


def _sender_category(channel: str, payload: dict[str, Any]) -> str:
    # Prefer explicit type field if present
    packet_type = payload.get("type")
    if isinstance(packet_type, str) and packet_type:
        return packet_type
    # Fall back to channel → dotted category
    return channel.replace("/", ".")