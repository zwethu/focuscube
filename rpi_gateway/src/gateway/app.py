# src/gateway/app.py
from __future__ import annotations

from flask import Flask, jsonify, request

from gateway.models import ListenerCommand
from gateway.service import GatewayService


def create_app(service: GatewayService) -> Flask:
    app = Flask(__name__)

    # ── Health & metrics ─────────────────────────────────────────

    @app.get("/health")
    def health():
        return jsonify({"status": "ok"})

    @app.get("/metrics")
    def metrics():
        return jsonify(service.metrics.snapshot())

    # ── Live state ───────────────────────────────────────────────

    @app.get("/api/status")
    def status():
        """Latest telemetry + focus score in one call."""
        return jsonify(service.state.summary())

    @app.get("/api/telemetry/latest")
    def telemetry_latest():
        data = service.state.latest_telemetry()
        if data is None:
            return jsonify({"error": "no data yet"}), 404
        return jsonify(data.model_dump())

    @app.get("/api/telemetry/history")
    def telemetry_history():
        n = min(int(request.args.get("n", 100)), 500)
        return jsonify(service.state.telemetry_history(n))

    @app.get("/api/focus/latest")
    def focus_latest():
        data = service.state.latest_focus()
        if data is None:
            return jsonify({"error": "no data yet"}), 404
        return jsonify(data.model_dump())

    @app.get("/api/focus/history")
    def focus_history():
        n = min(int(request.args.get("n", 100)), 500)
        return jsonify(service.state.focus_history(n))

    @app.get("/api/events")
    def events():
        n = min(int(request.args.get("n", 50)), 200)
        return jsonify(service.state.recent_events(n))

    # ── Commands to Listener ESP32 ───────────────────────────────

    @app.post("/api/command")
    def send_command():
        """
        POST /api/command
        Body: { "action": "play" | "stop", "reason": "optional string" }

        Publishes to focuscube/listener/cmd via MQTTBridge.
        """
        body   = request.get_json(force=True, silent=True) or {}
        action = body.get("action")

        if action not in ("play", "stop"):
            return jsonify({
                "error": "action must be 'play' or 'stop'"
            }), 400

        command = ListenerCommand(
            action=action,
            reason=body.get("reason"),
        )
        service.send_command(command)

        return jsonify({
            "status":  "published",
            "action":  action,
            "topic":   service.cfg.topics.cmd_topic,
        }), 202

    return app