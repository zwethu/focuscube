# src/gateway/focus_engine.py
from __future__ import annotations

import time
from collections import deque
from dataclasses import asdict, dataclass, field
from enum import Enum
from typing import Deque

from gateway.config import ThresholdConfig
from gateway.models import CanonicalEvent, FocusComponents, FocusScore, SensorPayload


# ── Focus state enum (matches your C typedef) ────────────────────

class FocusState(str, Enum):
    FOCUSED    = "FOCUSED"
    DISTRACTED = "DISTRACTED"
    BAD_ENV    = "BAD_ENV"
    AWAY       = "AWAY"


# ── Derived metrics computed over a sliding window ───────────────

@dataclass
class DerivedMetrics:
    movement_freq:      float = 0.0   # moves per minute
    session_duration:   float = 0.0   # minutes of continuous presence
    avg_noise_level:    float = 0.0   # average sound ADC over session
    heat_index:         float = 0.0   # comfort score from temp + humidity
    air_degradation:    float = 0.0   # rate of MQ135 ADC rise per minute
    env_comfort_score:  int   = 0     # 0–100
    focus_score:        int   = 0     # 0–100


# ── Session tracker — holds sliding window history ───────────────

class SessionTracker:
    """
    Maintains per-session sliding windows for derived metrics.
    All windows are time-bounded (last 60 seconds by default).
    """

    def __init__(self, window_sec: int = 60) -> None:
        self._window_sec   = window_sec
        self._dist_history:  Deque[tuple[float, float]] = deque()  # (ts, dist_cm)
        self._sound_history: Deque[tuple[float, float]] = deque()  # (ts, sound_level)
        self._air_history:   Deque[tuple[float, int]]   = deque()  # (ts, mq135_raw)
        self._presence_start: float | None = None                  # ts when presence began

    def update(self, s: SensorPayload) -> None:
        now = time.time()
        self._evict(now)

        if s.dist_cm is not None:
            self._dist_history.append((now, s.dist_cm))

        if s.sound_do is not None and s.mq135_raw is not None:
            # Use mq135_raw for sound_level proxy if no analog sound value
            pass
        if s.mq135_raw is not None:
            self._air_history.append((now, s.mq135_raw))

        # Track sound level — use mq135_raw as sound_level
        # (sound analog is separate; stored separately below)
        if s.sound_level is not None:
            self._sound_history.append((now, float(s.sound_level)))

        # Session presence tracking
        if s.pir == 1:
            if self._presence_start is None:
                self._presence_start = now
        else:
            self._presence_start = None

    def compute(self) -> DerivedMetrics:
        now = time.time()
        self._evict(now)

        return DerivedMetrics(
            movement_freq=self._movement_freq(),
            session_duration=self._session_duration(now),
            avg_noise_level=self._avg_noise(),
            heat_index=0.0,        # computed separately from sensors
            air_degradation=self._air_degradation(),
            env_comfort_score=0,   # computed in score_event
            focus_score=0,         # computed in score_event
        )

    # ── Private ───────────────────────────────────────────────────

    def _evict(self, now: float) -> None:
        cutoff = now - self._window_sec
        for dq in (self._dist_history, self._sound_history, self._air_history):
            while dq and dq[0][0] < cutoff:
                dq.popleft()

    def _movement_freq(self) -> float:
        """Moves per minute — count dist deltas > 5 cm in window."""
        if len(self._dist_history) < 2:
            return 0.0
        moves = 0
        items = list(self._dist_history)
        for i in range(1, len(items)):
            delta = abs(items[i][1] - items[i - 1][1])
            if delta > 5.0:
                moves += 1
        # normalise to per-minute
        elapsed_min = self._window_sec / 60.0
        return round(moves / elapsed_min, 2)

    def _session_duration(self, now: float) -> float:
        """Minutes of continuous presence."""
        if self._presence_start is None:
            return 0.0
        return round((now - self._presence_start) / 60.0, 2)

    def _avg_noise(self) -> float:
        if not self._sound_history:
            return 0.0
        return round(sum(v for _, v in self._sound_history) / len(self._sound_history), 1)

    def _air_degradation(self) -> float:
        """Rate of MQ135 ADC rise per minute (positive = getting worse)."""
        if len(self._air_history) < 2:
            return 0.0
        ts0, v0 = self._air_history[0]
        ts1, v1 = self._air_history[-1]
        elapsed_min = (ts1 - ts0) / 60.0
        if elapsed_min < 0.01:
            return 0.0
        return round((v1 - v0) / elapsed_min, 2)


# ── Main scoring function ─────────────────────────────────────────

def score_event(
    event:   CanonicalEvent,
    cfg:     ThresholdConfig,
    tracker: SessionTracker,
) -> FocusScore | None:
    """
    Convert a telemetry CanonicalEvent into a FocusScore.
    Returns None if the event is not a telemetry packet.
    """
    if event.category != "telemetry":
        return None

    sensors = SensorPayload.model_validate(
        event.payload.get("sensors", event.payload)
    )

    # Update sliding window tracker
    tracker.update(sensors)
    derived = tracker.compute()

    # Compute heat index (Steadman simplified formula)
    derived.heat_index = _heat_index(sensors.temp_c, sensors.humidity)

    # ── State machine (priority order from your spec) ─────────────
    state = _evaluate_state(sensors, derived, cfg)

    # ── Component flags ───────────────────────────────────────────
    components = FocusComponents(
        temp_ok=sensors.temp_c is not None and sensors.temp_c <= 32.0,
        light_ok=sensors.lux is not None and sensors.lux >= 100.0,
        air_ok=sensors.mq135_alert is not None and sensors.mq135_alert != cfg.mq135_bad_do,
        presence_ok=sensors.pir == 1,
        quiet_ok=sensors.sound_do == 0 and (
            sensors.sound_level is None or sensors.sound_level <= 3000
        ),
    )

    # ── Environment comfort score 0–100 ───────────────────────────
    env_score = _env_comfort_score(sensors, cfg)
    derived.env_comfort_score = env_score

    # ── Focus score 0–100 ─────────────────────────────────────────
    focus_score = _focus_score(state, components, derived)
    derived.focus_score = focus_score

    return FocusScore(
        score=focus_score,
        label=state.value,
        components=components,
        sensors=sensors,
        derived=asdict(derived),
    )


# ── State evaluator ───────────────────────────────────────────────

def _evaluate_state(
    s:       SensorPayload,
    derived: DerivedMetrics,
    cfg:     ThresholdConfig,
) -> FocusState:
    # 1. AWAY — person not at desk
    if not s.pir:
        return FocusState.AWAY

    # 2. BAD_ENV — environment overrides everything
    bad_temp     = s.temp_c   is not None and s.temp_c   > 32.0
    bad_humidity = s.humidity  is not None and s.humidity > 75.0
    bad_air      = s.mq135_alert == cfg.mq135_bad_do
    bad_light    = s.lux      is not None and s.lux      < 50.0
    bad_sound    = s.sound_do == 1

    if bad_temp or bad_humidity or bad_air or bad_light or bad_sound:
        return FocusState.BAD_ENV

    # 3. DISTRACTED — distraction signals
    moving = derived.movement_freq > 5.0          # > 5 moves/min
    noisy  = (
        s.sound_level is not None and s.sound_level > 3000
    ) or s.sound_do == 1
    dark   = s.lux is not None and s.lux < 100.0

    if moving or noisy or dark:
        return FocusState.DISTRACTED

    # 4. FOCUSED
    return FocusState.FOCUSED


# ── Scoring helpers ───────────────────────────────────────────────

def _env_comfort_score(s: SensorPayload, cfg: ThresholdConfig) -> int:
    """
    Environment comfort 0–100.
    5 components × 20 pts each:
      temp, humidity, air, light, sound
    """
    score = 0

    if s.temp_c is not None and cfg.temp_min <= s.temp_c <= cfg.temp_max:
        score += 20
    if s.humidity is not None and 30.0 <= s.humidity <= 60.0:
        score += 20
    if s.mq135_alert is not None and s.mq135_alert != cfg.mq135_bad_do:
        score += 20
    if s.lux is not None and cfg.lux_min <= s.lux <= cfg.lux_max:
        score += 20
    if s.sound_do == 0:
        score += 20

    return score


def _focus_score(
    state:      FocusState,
    components: FocusComponents,
    derived:    DerivedMetrics,
) -> int:
    """
    Focus score 0–100 based on state + component penalties.
    """
    if state == FocusState.AWAY:
        return 0
    if state == FocusState.BAD_ENV:
        return max(0, derived.env_comfort_score - 20)

    # Base from env comfort
    base = derived.env_comfort_score

    # Presence bonus
    if components.presence_ok:
        base = min(100, base + 10)

    # Movement penalty — higher freq = lower focus
    movement_penalty = min(30, int(derived.movement_freq * 3))
    base = max(0, base - movement_penalty)

    # Noise penalty
    noise_penalty = 0
    if derived.avg_noise_level > 2000:
        noise_penalty = 10
    if derived.avg_noise_level > 3000:
        noise_penalty = 20
    base = max(0, base - noise_penalty)

    # Session duration bonus — sustained presence = more focused
    if derived.session_duration >= 5.0:    # 5+ minutes
        base = min(100, base + 5)
    if derived.session_duration >= 25.0:   # 25+ minutes (Pomodoro-style)
        base = min(100, base + 5)

    return base


def _heat_index(temp_c: float | None, humidity: float | None) -> float:
    """
    Simplified Steadman heat index.
    Returns temp_c unchanged if inputs are missing.
    """
    if temp_c is None or humidity is None:
        return temp_c or 0.0
    # Convert to Fahrenheit for formula
    t = temp_c * 9 / 5 + 32
    h = humidity
    hi = (
        -42.379
        + 2.04901523 * t
        + 10.14333127 * h
        - 0.22475541 * t * h
        - 0.00683783 * t * t
        - 0.05481717 * h * h
        + 0.00122874 * t * t * h
        + 0.00085282 * t * h * h
        - 0.00000199 * t * t * h * h
    )
    # Convert back to Celsius
    return round((hi - 32) * 5 / 9, 1)