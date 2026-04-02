# src/gateway/state.py
from __future__ import annotations

import threading
from collections import deque
from typing import Any

from gateway.models import CanonicalEvent, FocusScore


class LiveState:
    """
    Thread-safe in-memory ring buffers.
    No database — pure deque, capped at configured maxlen.
    """

    def __init__(
        self,
        telemetry_maxlen: int = 100,
        focus_maxlen:     int = 100,
        event_maxlen:     int = 200,
    ) -> None:
        self._lock = threading.Lock()
        self._telemetry: deque[CanonicalEvent] = deque(maxlen=telemetry_maxlen)
        self._focus:     deque[FocusScore]     = deque(maxlen=focus_maxlen)
        self._events:    deque[CanonicalEvent] = deque(maxlen=event_maxlen)

    # ── Writers (called from MQTT thread) ────────────────────────

    def push_telemetry(self, event: CanonicalEvent) -> None:
        with self._lock:
            self._telemetry.append(event)

    def push_focus(self, score: FocusScore) -> None:
        with self._lock:
            self._focus.append(score)

    def push_event(self, event: CanonicalEvent) -> None:
        with self._lock:
            self._events.append(event)

    # ── Readers (called from Flask thread) ───────────────────────

    def latest_telemetry(self) -> CanonicalEvent | None:
        with self._lock:
            return self._telemetry[-1] if self._telemetry else None

    def latest_focus(self) -> FocusScore | None:
        with self._lock:
            return self._focus[-1] if self._focus else None

    def telemetry_history(self, n: int = 100) -> list[dict]:
        with self._lock:
            items = list(self._telemetry)[-n:]
        return [e.model_dump() for e in items]

    def focus_history(self, n: int = 100) -> list[dict]:
        with self._lock:
            items = list(self._focus)[-n:]
        return [s.model_dump() for s in items]

    def recent_events(self, n: int = 50) -> list[dict]:
        with self._lock:
            items = list(self._events)[-n:]
        return [e.model_dump() for e in items]

    def summary(self) -> dict[str, Any]:
        with self._lock:
            latest_t = self._telemetry[-1] if self._telemetry else None
            latest_f = self._focus[-1]     if self._focus     else None
        return {
            "latest_telemetry": latest_t.model_dump() if latest_t else None,
            "latest_focus":     latest_f.model_dump() if latest_f else None,
            "telemetry_count":  len(self._telemetry),
            "focus_count":      len(self._focus),
            "event_count":      len(self._events),
        }