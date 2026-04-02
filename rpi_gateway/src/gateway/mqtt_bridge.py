# src/gateway/mqtt_bridge.py — direct workshop pattern, focuscube topics
from __future__ import annotations

import json
import logging
from typing import Callable

import paho.mqtt.client as mqtt

from gateway.config import MQTTConfig, TopicConfig
from gateway.models import ListenerCommand
from gateway.topic_parser import parse_incoming


class MQTTBridge:

    def __init__(
        self,
        mqtt_cfg: MQTTConfig,
        topic_cfg: TopicConfig,
        logger: logging.Logger,
        on_event: Callable,
    ) -> None:
        self._mqtt_cfg  = mqtt_cfg
        self._topic_cfg = topic_cfg
        self._logger    = logger
        self._on_event  = on_event

        self._client = mqtt.Client(
            client_id=mqtt_cfg.client_id,
            protocol=mqtt.MQTTv311,
        )
        if mqtt_cfg.username:
            self._client.username_pw_set(mqtt_cfg.username, mqtt_cfg.password)

        self._client.on_connect    = self._on_connect
        self._client.on_message    = self._on_message
        self._client.on_disconnect = self._on_disconnect

    def start(self) -> None:
        self._client.connect(
            self._mqtt_cfg.host,
            self._mqtt_cfg.port,
            self._mqtt_cfg.keepalive_sec,
        )
        self._client.loop_start()

    def stop(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()

    def publish_command(self, command: ListenerCommand) -> None:
        topic   = self._topic_cfg.cmd_topic
        payload = json.dumps(
            {"payload": {"action": command.action, "reason": command.reason}},
            separators=(",", ":"),
        )
        self._client.publish(topic, payload, qos=1)
        self._logger.info("[CMD] Published action=%s to %s", command.action, topic)

    # ── Private callbacks ────────────────────────────────────────

    def _on_connect(
        self,
        client: mqtt.Client,
        userdata: object,
        flags: dict,
        rc: int,
    ) -> None:
        if rc != 0:
            self._logger.error("MQTT connect failed: %s", rc)
            return
        self._logger.info(
            "Connected to MQTT broker %s:%s",
            self._mqtt_cfg.host,
            self._mqtt_cfg.port,
        )
        for topic in self._topic_cfg.subscriptions:
            client.subscribe(topic, qos=1)
            self._logger.info("Subscribed → %s", topic)

    def _on_disconnect(
        self,
        client: mqtt.Client,
        userdata: object,
        rc: int,
    ) -> None:
        self._logger.warning("Disconnected from MQTT broker: %s", rc)

    def _on_message(
        self,
        client: mqtt.Client,
        userdata: object,
        msg: mqtt.MQTTMessage,
    ) -> None:
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except Exception as exc:
            self._logger.error(
                "Invalid JSON on topic=%s err=%s", msg.topic, exc
            )
            return

        event = parse_incoming(msg.topic, payload, msg.qos, msg.retain)
        if event is None:
            return
        self._on_event(event)