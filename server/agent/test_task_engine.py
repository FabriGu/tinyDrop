"""Tests for task_engine module — TaskGenerator, SessionState, Leaderboard.

TDD RED phase: all tests written before implementation.
Run: cd server/agent && python -m pytest test_task_engine.py -v
"""

import os
import sqlite3
import tempfile
from collections import Counter
from unittest.mock import patch

import pytest

from task_engine import (
    Leaderboard,
    SessionState,
    State,
    TaskGenerator,
    TUTORIAL_STEPS,
)


# ---------------------------------------------------------------------------
# TaskGenerator
# ---------------------------------------------------------------------------


class TestTaskGenerator:
    def setup_method(self):
        self.gen = TaskGenerator()

    # -- basic structure --

    def test_next_returns_dict_with_required_keys(self):
        task = self.gen.next(event_count=3)
        assert "task_id" in task
        assert "text" in task
        assert "expected_verb" in task

    def test_task_id_increments(self):
        t1 = self.gen.next(event_count=3)
        t2 = self.gen.next(event_count=3)
        assert t1["task_id"] == "t1"
        assert t2["task_id"] == "t2"

    def test_task_text_four_words_or_less(self):
        for _ in range(50):
            task = self.gen.next(event_count=3)
            assert len(task["text"].split()) <= 4

    def test_expected_verb_is_valid(self):
        for _ in range(50):
            task = self.gen.next(event_count=3)
            assert task["expected_verb"] in ("POST", "PUT", "DELETE")

    # -- distribution (round verbs only: POST/PUT/DELETE, no GET) --

    def test_no_get_during_round(self):
        verbs = [self.gen.next(event_count=5)["expected_verb"] for _ in range(200)]
        assert "GET" not in verbs

    def test_distribution_roughly_40_30_30(self):
        verbs = [self.gen.next(event_count=10)["expected_verb"] for _ in range(1000)]
        counts = Counter(verbs)
        total = len(verbs)
        # Allow ±10% tolerance
        assert 0.25 <= counts["POST"] / total <= 0.55
        assert 0.15 <= counts["PUT"] / total <= 0.45
        assert 0.15 <= counts["DELETE"] / total <= 0.45

    # -- no triple repeat --

    def test_no_triple_repeat(self):
        history = []
        for _ in range(500):
            task = self.gen.next(event_count=5)
            history.append(task["expected_verb"])
            if len(history) >= 3:
                assert not (
                    history[-1] == history[-2] == history[-3]
                ), f"Triple repeat detected: {history[-3:]}"

    # -- force POST when empty calendar --

    def test_force_post_when_event_count_zero(self):
        for _ in range(20):
            task = self.gen.next(event_count=0)
            assert task["expected_verb"] == "POST"

    def test_force_post_resets_after_events_exist(self):
        task = self.gen.next(event_count=0)
        assert task["expected_verb"] == "POST"
        # After events exist again, other verbs should appear eventually
        verbs = set()
        for _ in range(100):
            t = self.gen.next(event_count=5)
            verbs.add(t["expected_verb"])
        assert len(verbs) > 1  # Not all POST

    # -- prompt text matches verb --

    def test_post_prompt_text(self):
        gen = TaskGenerator()
        # Force POST via empty calendar
        task = gen.next(event_count=0)
        assert task["text"] == "Add an event"

    def test_verb_text_mapping(self):
        seen = {}
        for _ in range(300):
            task = self.gen.next(event_count=5)
            verb = task["expected_verb"]
            seen[verb] = task["text"]
        assert seen["POST"] == "Add an event"
        assert seen["PUT"] == "Update an event"
        assert seen["DELETE"] == "Remove an event"

    # -- tutorial task --

    def test_tutorial_task(self):
        task = TaskGenerator.tutorial_task(step=0)
        assert task["expected_verb"] == "GET"
        assert task["text"] == "Refresh calendar"

        task = TaskGenerator.tutorial_task(step=1)
        assert task["expected_verb"] == "POST"
        assert task["text"] == "Add an event"

        task = TaskGenerator.tutorial_task(step=2)
        assert task["expected_verb"] == "PUT"
        assert task["text"] == "Update an event"

        task = TaskGenerator.tutorial_task(step=3)
        assert task["expected_verb"] == "DELETE"
        assert task["text"] == "Remove an event"

    def test_tutorial_task_invalid_step(self):
        with pytest.raises((IndexError, ValueError)):
            TaskGenerator.tutorial_task(step=4)

    # -- reset --

    def test_reset_clears_history_and_counter(self):
        self.gen.next(event_count=3)
        self.gen.next(event_count=3)
        self.gen.reset()
        task = self.gen.next(event_count=3)
        assert task["task_id"] == "t1"


# ---------------------------------------------------------------------------
# SessionState
# ---------------------------------------------------------------------------


class TestSessionState:
    def setup_method(self):
        self.session = SessionState()

    # -- initial state --

    def test_initial_state_is_idle(self):
        assert self.session.state == State.IDLE

    # -- start_session --

    def test_start_session_transitions_to_tutorial(self):
        self.session.start_session("ZAC")
        assert self.session.state == State.TUTORIAL
        assert self.session.player_name == "ZAC"
        assert self.session.score == 0
        assert self.session.tutorial_step == 0

    def test_start_session_generates_round_id(self):
        self.session.start_session("BOB")
        assert self.session.round_id is not None
        assert self.session.round_id.startswith("r")

    def test_start_session_sets_event_count(self):
        self.session.start_session("ANA", event_count=3)
        assert self.session.event_count == 3

    # -- tutorial flow --

    def test_tutorial_has_four_steps(self):
        assert len(TUTORIAL_STEPS) == 4

    def test_tutorial_order_get_post_put_delete(self):
        expected = ["GET", "POST", "PUT", "DELETE"]
        assert [s["expected_verb"] for s in TUTORIAL_STEPS] == expected

    def test_advance_tutorial_correct_verb(self):
        self.session.start_session("ZAC")
        current_task = self.session.current_task
        assert current_task["expected_verb"] == "GET"

        result = self.session.advance_tutorial("GET")
        assert result is True
        assert self.session.tutorial_step == 1
        assert self.session.current_task["expected_verb"] == "POST"

    def test_advance_tutorial_wrong_verb_no_advance(self):
        self.session.start_session("ZAC")
        result = self.session.advance_tutorial("POST")  # wrong, expected GET
        assert result is False
        assert self.session.tutorial_step == 0

    def test_advance_tutorial_wrong_verb_no_score_penalty(self):
        self.session.start_session("ZAC")
        self.session.advance_tutorial("POST")  # wrong
        assert self.session.score == 0

    def test_complete_tutorial_transitions_to_round(self):
        self.session.start_session("ZAC")
        self.session.advance_tutorial("GET")
        self.session.advance_tutorial("POST")
        self.session.advance_tutorial("PUT")
        self.session.advance_tutorial("DELETE")
        assert self.session.state == State.ROUND
        assert self.session.time_left == 60

    def test_tutorial_sets_current_task(self):
        self.session.start_session("ZAC")
        assert self.session.current_task is not None
        assert self.session.current_task["text"] == "Refresh calendar"

    def test_advance_tutorial_returns_false_when_not_in_tutorial(self):
        # Session in IDLE
        result = self.session.advance_tutorial("GET")
        assert result is False

    # -- round / submit_action --

    def _enter_round(self, session=None):
        """Helper: advance session through tutorial into ROUND state."""
        s = session or self.session
        s.start_session("ZAC", event_count=3)
        s.advance_tutorial("GET")
        s.advance_tutorial("POST")
        s.advance_tutorial("PUT")
        s.advance_tutorial("DELETE")
        return s

    def test_submit_action_correct_verb_scores_plus_3(self):
        self._enter_round()
        task = self.session.current_task
        result = self.session.submit_action(task["expected_verb"])
        assert result["correct"] is True
        assert result["points_delta"] == 3
        assert self.session.score == 3

    def test_submit_action_wrong_verb_scores_minus_2(self):
        self._enter_round()
        task = self.session.current_task
        wrong_verb = "GET" if task["expected_verb"] != "GET" else "POST"
        result = self.session.submit_action(wrong_verb)
        assert result["correct"] is False
        assert result["points_delta"] == -2
        assert self.session.score == -2

    def test_submit_action_returns_result_dict(self):
        self._enter_round()
        task = self.session.current_task
        result = self.session.submit_action(task["expected_verb"])
        assert "round_id" in result
        assert "verb" in result
        assert "expected_verb" in result
        assert "correct" in result
        assert "points_delta" in result
        assert "score_total" in result

    def test_submit_action_advances_task(self):
        self._enter_round()
        task1 = self.session.current_task
        self.session.submit_action(task1["expected_verb"])
        task2 = self.session.current_task
        assert task2["task_id"] != task1["task_id"]

    def test_submit_action_api_error_scores_minus_2(self):
        self._enter_round()
        result = self.session.submit_action("POST", api_error=True)
        assert result["points_delta"] == -2

    def test_score_can_go_negative(self):
        self._enter_round()
        for _ in range(5):
            task = self.session.current_task
            wrong = "GET" if task["expected_verb"] != "GET" else "POST"
            self.session.submit_action(wrong)
        assert self.session.score < 0

    def test_submit_action_not_in_round_returns_none(self):
        # Session in IDLE
        result = self.session.submit_action("GET")
        assert result is None

    def test_cumulative_scoring(self):
        self._enter_round()
        # Correct + correct + wrong = 3 + 3 - 2 = 4
        t1 = self.session.current_task
        self.session.submit_action(t1["expected_verb"])
        t2 = self.session.current_task
        self.session.submit_action(t2["expected_verb"])
        t3 = self.session.current_task
        wrong = "GET" if t3["expected_verb"] != "GET" else "POST"
        self.session.submit_action(wrong)
        assert self.session.score == 4

    # -- event_count tracking --

    def test_submit_post_increments_event_count(self):
        self._enter_round()
        initial = self.session.event_count
        self.session.submit_action("POST")
        # Event count should increase by 1 regardless of correctness
        # (the action still executes per context doc)
        assert self.session.event_count == initial + 1

    def test_submit_delete_decrements_event_count(self):
        self._enter_round()
        initial = self.session.event_count
        self.session.submit_action("DELETE")
        assert self.session.event_count == max(0, initial - 1)

    # -- tick / timer --

    def test_tick_decrements_time(self):
        self._enter_round()
        assert self.session.time_left == 60
        self.session.tick(1.0)
        assert self.session.time_left == 59

    def test_tick_transitions_to_results_on_zero(self):
        self._enter_round()
        self.session.tick(60.0)
        assert self.session.state == State.RESULTS
        assert self.session.time_left == 0

    def test_tick_results_timeout_to_idle(self):
        self._enter_round()
        self.session.tick(60.0)  # → RESULTS
        assert self.session.state == State.RESULTS
        self.session.tick(10.0)  # 10s play-again window expires
        assert self.session.state == State.IDLE

    def test_tick_no_effect_in_idle(self):
        self.session.tick(5.0)
        assert self.session.state == State.IDLE

    def test_tick_no_effect_in_tutorial(self):
        self.session.start_session("ZAC")
        self.session.tick(100.0)
        assert self.session.state == State.TUTORIAL

    def test_tick_fractional_seconds(self):
        self._enter_round()
        self.session.tick(0.5)
        self.session.tick(0.5)
        assert self.session.time_left == 59

    def test_results_play_again_window_is_10s(self):
        self._enter_round()
        self.session.tick(60.0)  # → RESULTS
        self.session.tick(9.0)  # Still in results (9 < 10)
        assert self.session.state == State.RESULTS
        self.session.tick(1.0)  # Now 10s → IDLE
        assert self.session.state == State.IDLE

    # -- play_again --

    def test_play_again_keeps_name(self):
        self._enter_round()
        self.session.tick(60.0)  # → RESULTS
        self.session.play_again()
        assert self.session.player_name == "ZAC"

    def test_play_again_zeros_score(self):
        self._enter_round()
        task = self.session.current_task
        self.session.submit_action(task["expected_verb"])  # +3
        self.session.tick(60.0)  # → RESULTS
        self.session.play_again()
        assert self.session.score == 0

    def test_play_again_skips_tutorial(self):
        self._enter_round()
        self.session.tick(60.0)  # → RESULTS
        self.session.play_again()
        # Should be in a state waiting for GET to start round
        assert self.session.state == State.PLAY_AGAIN

    def test_play_again_get_starts_round(self):
        self._enter_round()
        self.session.tick(60.0)  # → RESULTS
        self.session.play_again()
        assert self.session.state == State.PLAY_AGAIN
        result = self.session.start_round_from_play_again()
        assert self.session.state == State.ROUND
        assert self.session.time_left == 60

    def test_play_again_generates_new_round_id(self):
        self._enter_round()
        old_round_id = self.session.round_id
        self.session.tick(60.0)
        self.session.play_again()
        self.session.start_round_from_play_again()
        assert self.session.round_id != old_round_id

    def test_play_again_not_in_results_is_noop(self):
        self._enter_round()
        # Still in ROUND, not RESULTS
        self.session.play_again()
        assert self.session.state == State.ROUND  # unchanged

    # -- timeout --

    def test_timeout_resets_to_idle(self):
        self._enter_round()
        self.session.timeout()
        assert self.session.state == State.IDLE
        assert self.session.player_name is None
        assert self.session.score == 0

    # -- full flow --

    def test_full_game_flow(self):
        s = SessionState()
        assert s.state == State.IDLE

        # Name entry → tutorial
        s.start_session("ABC", event_count=3)
        assert s.state == State.TUTORIAL

        # Tutorial
        s.advance_tutorial("GET")
        s.advance_tutorial("POST")
        s.advance_tutorial("PUT")
        s.advance_tutorial("DELETE")
        assert s.state == State.ROUND

        # Play round
        task = s.current_task
        result = s.submit_action(task["expected_verb"])
        assert result["correct"] is True

        # Time runs out
        s.tick(60.0)
        assert s.state == State.RESULTS

        # Play again
        s.play_again()
        assert s.state == State.PLAY_AGAIN
        s.start_round_from_play_again()
        assert s.state == State.ROUND
        assert s.score == 0

        # Time runs out again, no play again
        s.tick(60.0)
        assert s.state == State.RESULTS
        s.tick(10.0)
        assert s.state == State.IDLE


# ---------------------------------------------------------------------------
# Leaderboard
# ---------------------------------------------------------------------------


class TestLeaderboard:
    def setup_method(self):
        self.tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self.tmp.close()
        self.lb = Leaderboard(db_path=self.tmp.name)

    def teardown_method(self):
        os.unlink(self.tmp.name)

    def test_record_and_top(self):
        self.lb.record("ZAC", 42, "r1")
        result = self.lb.top()
        assert len(result) == 1
        assert result[0]["name"] == "ZAC"
        assert result[0]["score"] == 42
        assert result[0]["round_id"] == "r1"

    def test_top_returns_sorted_descending(self):
        self.lb.record("AAA", 10, "r1")
        self.lb.record("BBB", 50, "r2")
        self.lb.record("CCC", 30, "r3")
        result = self.lb.top()
        scores = [r["score"] for r in result]
        assert scores == [50, 30, 10]

    def test_top_limits_to_n(self):
        for i in range(20):
            self.lb.record(f"P{i:02d}", i * 10, f"r{i}")
        result = self.lb.top(n=10)
        assert len(result) == 10

    def test_top_default_is_10(self):
        for i in range(15):
            self.lb.record(f"P{i:02d}", i, f"r{i}")
        result = self.lb.top()
        assert len(result) == 10

    def test_empty_leaderboard(self):
        result = self.lb.top()
        assert result == []

    def test_record_negative_score(self):
        self.lb.record("BAD", -5, "r1")
        result = self.lb.top()
        assert result[0]["score"] == -5

    def test_multiple_entries_same_player(self):
        self.lb.record("ZAC", 42, "r1")
        self.lb.record("ZAC", 80, "r2")
        result = self.lb.top()
        assert len(result) == 2
        assert result[0]["score"] == 80

    def test_result_dict_has_played_at(self):
        self.lb.record("ZAC", 42, "r1")
        result = self.lb.top()
        assert "played_at" in result[0]

    def test_schema_columns(self):
        """Verify the DB schema has the expected columns."""
        conn = sqlite3.connect(self.tmp.name)
        cursor = conn.execute("PRAGMA table_info(scores)")
        columns = {row[1] for row in cursor.fetchall()}
        conn.close()
        assert columns == {"id", "name", "score", "round_id", "played_at"}

    def test_concurrent_writes(self):
        """Multiple rapid writes should not corrupt the DB."""
        for i in range(50):
            self.lb.record(f"P{i:02d}", i, f"r{i}")
        result = self.lb.top(n=50)
        assert len(result) == 50

    def test_custom_db_path(self):
        with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as f:
            path = f.name
        try:
            lb2 = Leaderboard(db_path=path)
            lb2.record("TST", 99, "r1")
            assert lb2.top()[0]["score"] == 99
        finally:
            os.unlink(path)

    def test_default_db_path(self):
        """Leaderboard uses /opt/apigame/data/leaderboard.db by default."""
        lb = Leaderboard.__new__(Leaderboard)
        # Don't actually init (would try to create file at /opt/...)
        # Just check the class has the right default
        import inspect
        sig = inspect.signature(Leaderboard.__init__)
        default = sig.parameters["db_path"].default
        assert default == "/opt/apigame/data/leaderboard.db"
