from __future__ import annotations

import argparse
import sys
import threading

from gateway.app import create_app
from gateway.config import load_config
from gateway.logging_setup import setup_logging
from gateway.service import GatewayService


def _build_parser():
    p = argparse.ArgumentParser(
        prog="focuscube-gateway",
        description="FocusCube RPi Gateway - MQTT bridge + REST API",
    )
    p.add_argument(
        "--config", "-c",
        default="config/gateway.toml",
        metavar="PATH",
        help="Path to gateway.toml (default: config/gateway.toml)",
    )
    return p


def main():
    args = _build_parser().parse_args()

    try:
        cfg = load_config(args.config)
    except FileNotFoundError as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)

    logger, message_logger = setup_logging(cfg.logging)
    logger.info("Loaded config from %s", args.config)

    service = GatewayService(cfg, logger, message_logger)
    service.start()

    flask_app = create_app(service)

    flask_thread = threading.Thread(
        target=lambda: flask_app.run(
            host=cfg.api.host,
            port=cfg.api.port,
            debug=cfg.api.debug,
            use_reloader=False,
        ),
        daemon=True,
        name="flask-api",
    )
    flask_thread.start()
    logger.info(
        "REST API listening on http://%s:%s",
        cfg.api.host,
        cfg.api.port,
    )

    service.wait_forever()


if __name__ == "__main__":
    main()