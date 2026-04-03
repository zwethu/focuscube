# src/gateway/service.py
from __future__ import annotations

import logging
import time
import json

from gateway.config import AppConfig
from gateway.focus_engine import SessionTracker, score_event
from gateway.logging_setup import log_message
from gateway.metrics import Metrics
from gateway.models import CanonicalEvent, ListenerCommand
from gateway.mqtt_bridge import MQTTBridge
from gateway.state import LiveState
from gateway.predictor import predict_mood


class GatewayService:

    def __init__(
        self,
        cfg:            AppConfig,
        logger:         logging.Logger,
        message_logger: logging.Logger,
    ) -> None:
        self._cfg            = cfg
        self._logger         = logger
        self._message_logger = message_logger
        self._metrics        = Metrics()
        self._tracker        = SessionTracker(window_sec=60)
        self._state          = LiveState(
            telemetry_maxlen=cfg.state.telemetry_maxlen,
            focus_maxlen=cfg.state.focus_maxlen,
            event_maxlen=cfg.state.event_maxlen,
        )
        self._mqtt = MQTTBridge(
            cfg.mqtt,
            cfg.topics,
            logger,
            self._on_event,
        )

    @property
    def state(self) -> LiveState:
        return self._state

    @property
    def metrics(self) -> Metrics:
        return self._metrics

    @property
    def cfg(self) -> AppConfig:
        return self._cfg

    def start(self) -> None:
        self._logger.info("FocusCube gateway starting...")
        self._mqtt.start()
        self._logger.info(
            "MQTT bridge connected → %s:%s",
            self._cfg.mqtt.host,
            self._cfg.mqtt.port,
        )

    def stop(self) -> None:
        self._mqtt.stop()
        self._logger.info("FocusCube gateway stopped.")

    def send_command(self, command: ListenerCommand) -> None:
        self._mqtt.publish_command(command)

    def wait_forever(self) -> None:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            self._logger.info("Shutdown signal received")
            self.stop()

    # ── Internal event handler (runs in MQTT thread) ─────────────

    def _on_event(self, event: CanonicalEvent) -> None:
        self._metrics.inc("mqtt_ingress_total")

        log_message(
            self._message_logger,
            direction="mqtt_in",
            topic=event.topic,
            payload=event.raw,
            meta={"source": event.source, "category": event.category},
        )

        # Telemetry → store + score
        if event.category == "telemetry":
            self._state.push_telemetry(event)
            self._metrics.inc("telemetry_total")

            focus = score_event(event, self._cfg.thresholds, self._tracker)
            if focus is not None:
                self._state.push_focus(focus)
                self._metrics.inc(f"focus_label.{focus.label.lower()}")
                self._logger.debug(
                    "Focus score=%d label=%s", focus.score, focus.label
                )
                sensors = focus.sensors
                if all(v is not None for v in [
                    sensors.temp_c, sensors.humidity, sensors.mq135_raw, sensors.lux
                ]):
                    aqi = int((sensors.mq135_raw / 4095) * 150)  # scale to 0-150
                    mood = predict_mood(
                        temp_c=sensors.temp_c,
                        humidity=sensors.humidity,
                        air_quality_index=aqi,
                        lux=sensors.lux,
                    )
                    self._mqtt.publish_command(
                        ListenerCommand(action="mood", reason=mood)
                    )
                    self._logger.info("[MOOD] Predicted=%s", mood)

        # PIR / other events → event log
        elif event.category.startswith("event."):
            self._state.push_event(event)
            self._metrics.inc(f"event.{event.category}")
            self._logger.info(
                "Event received category=%s node=%s",
                event.category,
                event.node_id,
            )

        # Listener ACK
        elif event.category == "actuator.ack":
            self._state.push_event(event)
            self._metrics.inc("actuator_ack_total")
            self._logger.info(
                "Listener ACK: %s", event.payload
            )