# src/gateway/logging_setup.py
from __future__ import annotations

import json
import logging
import logging.handlers
from pathlib import Path
from typing import Any

from gateway.config import LoggingConfig


def setup_logging(cfg: LoggingConfig) -> tuple[logging.Logger, logging.Logger]:
    """
    Returns (app_logger, message_logger).
    app_logger    → human-readable gateway operational logs
    message_logger → structured JSON per MQTT message
    """
    log_dir = Path(cfg.directory)
    log_dir.mkdir(parents=True, exist_ok=True)

    level = getattr(logging, cfg.level.upper(), logging.INFO)

    # ── App logger ───────────────────────────────────────────────
    app_logger = logging.getLogger("gateway.app")
    app_logger.setLevel(level)
    if not app_logger.handlers:
        fmt = logging.Formatter(
            "%(asctime)s [%(levelname)s] %(name)s: %(message)s",
            datefmt="%Y-%m-%dT%H:%M:%S",
        )
        sh = logging.StreamHandler()
        sh.setFormatter(fmt)
        fh = logging.handlers.RotatingFileHandler(
            log_dir / cfg.app_file, maxBytes=5_000_000, backupCount=3
        )
        fh.setFormatter(fmt)
        app_logger.addHandler(sh)
        app_logger.addHandler(fh)

    # ── Message logger (JSON lines) ──────────────────────────────
    msg_logger = logging.getLogger("gateway.messages")
    msg_logger.setLevel(logging.DEBUG)
    msg_logger.propagate = False
    if not msg_logger.handlers:
        mfh = logging.handlers.RotatingFileHandler(
            log_dir / cfg.message_file, maxBytes=10_000_000, backupCount=5
        )
        mfh.setFormatter(logging.Formatter("%(message)s"))
        msg_logger.addHandler(mfh)

    return app_logger, msg_logger


def log_message(
    logger: logging.Logger,
    direction: str,
    topic: str,
    payload: Any,
    meta: dict | None = None,
) -> None:
    record = {"direction": direction, "topic": topic, "payload": payload}
    if meta:
        record.update(meta)
    logger.debug(json.dumps(record, default=str))