"""Pure game logic for APIgame — no I/O dependencies.

Contains:
- TaskGenerator: weighted random task generation with anti-repeat logic.
- SessionState: per-player state machine (IDLE → TUTORIAL → ROUND → RESULTS).
- Leaderboard: SQLite persistence for high scores.
"""

from __future__ import annotations

import enum
import random
import sqlite3
import uuid
from typing import Optional

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

VERB_TEXT = {
    "GET": "Refresh calendar",
    "POST": "Add an event",
    "PUT": "Update an event",
    "DELETE": "Remove an event",
}

TUTORIAL_STEPS = [
    {"expected_verb": "GET", "text": VERB_TEXT["GET"]},
    {"expected_verb": "POST", "text": VERB_TEXT["POST"]},
    {"expected_verb": "PUT", "text": VERB_TEXT["PUT"]},
    {"expected_verb": "DELETE", "text": VERB_TEXT["DELETE"]},
]

ROUND_DURATION_S = 60
RESULTS_DURATION_S = 10
POINTS_CORRECT = 3
POINTS_WRONG = -2
POINTS_API_ERROR = -2

# Round verb weights (no GET)
_ROUND_WEIGHTS = [("POST", 40), ("PUT", 30), ("DELETE", 30)]
_ROUND_VERBS, _ROUND_CUMULATIVE = zip(*_ROUND_WEIGHTS)
_ROUND_CUM_WEIGHTS = []
_running = 0
for _, w in _ROUND_WEIGHTS:
    _running += w
    _ROUND_CUM_WEIGHTS.append(_running)


# ---------------------------------------------------------------------------
# TaskGenerator
# ---------------------------------------------------------------------------


class TaskGenerator:
    """Generates round tasks with weighted verb distribution."""

    def __init__(self) -> None:
        self._counter = 0
        self._history: list[str] = []

    def reset(self) -> None:
        self._counter = 0
        self._history = []

    def next(self, event_count: int) -> dict:
        """Generate the next round task.

        Args:
            event_count: current number of calendar events. If <1, forces POST.

        Returns:
            {"task_id": "tN", "text": "...", "expected_verb": "POST/PUT/DELETE"}
        """
        if event_count < 1:
            verb = "POST"
        else:
            verb = self._pick_verb()

        self._history.append(verb)
        self._counter += 1
        return {
            "task_id": f"t{self._counter}",
            "text": VERB_TEXT[verb],
            "expected_verb": verb,
        }

    def _pick_verb(self) -> str:
        """Weighted random pick respecting the no-triple-repeat rule."""
        for _ in range(20):  # safety cap
            verb = random.choices(
                list(_ROUND_VERBS), cum_weights=_ROUND_CUM_WEIGHTS
            )[0]
            if not self._would_triple(verb):
                return verb
        # Fallback: pick any verb that doesn't triple
        for v in _ROUND_VERBS:
            if not self._would_triple(v):
                return v
        return random.choice(list(_ROUND_VERBS))  # pragma: no cover

    def _would_triple(self, verb: str) -> bool:
        if len(self._history) < 2:
            return False
        return self._history[-1] == verb and self._history[-2] == verb

    @staticmethod
    def tutorial_task(step: int) -> dict:
        """Return the tutorial task for a given step (0–3).

        Raises IndexError/ValueError for invalid steps.
        """
        if step < 0 or step >= len(TUTORIAL_STEPS):
            raise ValueError(f"Invalid tutorial step: {step}")
        t = TUTORIAL_STEPS[step]
        return {
            "task_id": f"tutorial-{step}",
            "text": t["text"],
            "expected_verb": t["expected_verb"],
        }


# ---------------------------------------------------------------------------
# SessionState
# ---------------------------------------------------------------------------


class State(enum.Enum):
    IDLE = "IDLE"
    NAME_ENTRY = "NAME_ENTRY"
    TUTORIAL = "TUTORIAL"
    ROUND = "ROUND"
    RESULTS = "RESULTS"
    PLAY_AGAIN = "PLAY_AGAIN"


class SessionState:
    """Per-player game state machine."""

    def __init__(self) -> None:
        self.state: State = State.IDLE
        self.player_name: Optional[str] = None
        self.score: int = 0
        self.round_id: Optional[str] = None
        self.current_task: Optional[dict] = None
        self.time_left: float = 0
        self.tutorial_step: int = 0
        self.event_count: int = 0
        self._results_timer: float = 0
        self._task_gen = TaskGenerator()
        self._round_counter = 0

    # -- transitions --------------------------------------------------------

    def start_session(self, name: str, event_count: int = 0) -> None:
        """Begin a new session: NAME_ENTRY is handled by ESP32, so we jump
        straight to TUTORIAL."""
        self.player_name = name
        self.score = 0
        self.tutorial_step = 0
        self.event_count = event_count
        self._round_counter += 1
        self.round_id = f"r{self._round_counter}"
        self._task_gen.reset()
        self.state = State.TUTORIAL
        self.current_task = TaskGenerator.tutorial_task(0)

    def advance_tutorial(self, verb: str) -> bool:
        """Check verb against current tutorial step.

        Returns True if correct (advances step), False otherwise.
        No score penalty for wrong answers during tutorial.
        """
        if self.state != State.TUTORIAL:
            return False

        expected = TUTORIAL_STEPS[self.tutorial_step]["expected_verb"]
        if verb != expected:
            return False

        self.tutorial_step += 1
        if self.tutorial_step >= len(TUTORIAL_STEPS):
            self._start_round()
        else:
            self.current_task = TaskGenerator.tutorial_task(self.tutorial_step)
        return True

    def submit_action(self, verb: str, api_error: bool = False) -> Optional[dict]:
        """Submit a verb during a round.

        Returns result dict or None if not in ROUND state.
        """
        if self.state != State.ROUND:
            return None

        expected = self.current_task["expected_verb"]

        # Update event count (action executes regardless of correctness)
        if verb == "POST":
            self.event_count += 1
        elif verb == "DELETE":
            self.event_count = max(0, self.event_count - 1)

        # Scoring
        if api_error:
            points = POINTS_API_ERROR
            correct = False
        elif verb == expected:
            points = POINTS_CORRECT
            correct = True
        else:
            points = POINTS_WRONG
            correct = False

        self.score += points

        result = {
            "round_id": self.round_id,
            "verb": verb,
            "expected_verb": expected,
            "correct": correct,
            "points_delta": points,
            "score_total": self.score,
        }

        # Advance to next task
        self.current_task = self._task_gen.next(
            event_count=self.event_count
        )

        return result

    def tick(self, elapsed_s: float) -> None:
        """Advance timers by elapsed_s seconds."""
        if self.state == State.ROUND:
            self.time_left = max(0, self.time_left - elapsed_s)
            if self.time_left <= 0:
                self.state = State.RESULTS
                self._results_timer = RESULTS_DURATION_S
        elif self.state == State.RESULTS:
            self._results_timer -= elapsed_s
            if self._results_timer <= 0:
                self._reset_to_idle()

    def play_again(self) -> None:
        """Player opts to play again from RESULTS screen."""
        if self.state != State.RESULTS:
            return
        self.score = 0
        self._task_gen.reset()
        self.state = State.PLAY_AGAIN

    def start_round_from_play_again(self) -> None:
        """GET press from play-again state starts a new round."""
        if self.state != State.PLAY_AGAIN:
            return
        self._round_counter += 1
        self.round_id = f"r{self._round_counter}"
        self._start_round()

    def timeout(self) -> None:
        """Force reset to IDLE (e.g. watchdog)."""
        self._reset_to_idle()

    # -- internals ----------------------------------------------------------

    def _start_round(self) -> None:
        self.state = State.ROUND
        self.time_left = ROUND_DURATION_S
        self.current_task = self._task_gen.next(
            event_count=self.event_count
        )

    def _reset_to_idle(self) -> None:
        self.state = State.IDLE
        self.player_name = None
        self.score = 0
        self.round_id = None
        self.current_task = None
        self.time_left = 0
        self.tutorial_step = 0
        self.event_count = 0
        self._results_timer = 0


# ---------------------------------------------------------------------------
# Leaderboard
# ---------------------------------------------------------------------------


class Leaderboard:
    """SQLite-backed high score persistence."""

    def __init__(
        self, db_path: str = "/opt/apigame/data/leaderboard.db"
    ) -> None:
        self._db_path = db_path
        self._conn = sqlite3.connect(db_path, check_same_thread=False)
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute(
            """
            CREATE TABLE IF NOT EXISTS scores (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                name       TEXT    NOT NULL,
                score      INTEGER NOT NULL,
                round_id   TEXT    NOT NULL,
                played_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
            """
        )
        self._conn.commit()

    def record(self, name: str, score: int, round_id: str) -> None:
        self._conn.execute(
            "INSERT INTO scores (name, score, round_id) VALUES (?, ?, ?)",
            (name, score, round_id),
        )
        self._conn.commit()

    def top(self, n: int = 10) -> list[dict]:
        cursor = self._conn.execute(
            "SELECT name, score, round_id, played_at "
            "FROM scores ORDER BY score DESC LIMIT ?",
            (n,),
        )
        return [
            {
                "name": row[0],
                "score": row[1],
                "round_id": row[2],
                "played_at": row[3],
            }
            for row in cursor.fetchall()
        ]
