"""APIgame MQTT-Calendar bridge agent.

Subscribes to device actions and session events, runs game logic,
executes Calendar API calls, and publishes results/tasks/state/leaderboard.

Run:
    python agent.py
"""

import json
import logging
import os
import threading
import time
from pathlib import Path
from typing import Optional

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

from calendar_client import CalendarClient
from task_engine import Leaderboard, SessionState, State

logger = logging.getLogger("apigame-agent")

ENV_PATH = Path(__file__).resolve().parent.parent / "config" / ".env"


class Agent:
    """MQTT-game bridge: wires device messages to task engine + calendar."""

    def __init__(self) -> None:
        load_dotenv(ENV_PATH)

        self._mqtt_host = os.getenv("MQTT_HOST", "127.0.0.1")
        self._mqtt_port = int(os.getenv("MQTT_PORT", "1883"))
        self._mqtt_user = os.getenv("MQTT_USER", "esp32")
        self._mqtt_pass = os.getenv("MQTT_PASS", "")

        self._calendar = CalendarClient()
        self._leaderboard = Leaderboard()
        self._sessions: dict[str, SessionState] = {}
        self._lock = threading.Lock()

        # paho-mqtt v2 requires CallbackAPIVersion; fall back for v1
        try:
            self._client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION1,
                client_id="apigame-agent",
            )
        except (AttributeError, TypeError):
            self._client = mqtt.Client(client_id="apigame-agent")

        self._client.username_pw_set(self._mqtt_user, self._mqtt_pass)
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message

        self._running = False

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        """Connect to MQTT and start the main loop + background ticker."""
        self._client.connect(self._mqtt_host, self._mqtt_port)
        self._running = True

        tick_thread = threading.Thread(target=self._tick_loop, daemon=True)
        tick_thread.start()

        logger.info(
            "Starting MQTT loop on %s:%s", self._mqtt_host, self._mqtt_port
        )
        self._client.loop_forever()

    # ------------------------------------------------------------------
    # MQTT callbacks
    # ------------------------------------------------------------------

    def _on_connect(self, client, userdata, flags, rc):
        if rc != 0:
            logger.error("MQTT connect failed: rc=%s", rc)
            return
        logger.info("Connected to MQTT broker")
        client.subscribe("apigame/device/+/action")
        client.subscribe("apigame/device/+/session")
        client.subscribe("apigame/admin/reset")

    def _on_message(self, client, userdata, msg):
        topic = msg.topic
        try:
            payload = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            logger.warning("Bad payload on %s", topic)
            return

        parts = topic.split("/")

        if topic == "apigame/admin/reset":
            self._handle_admin_reset()
        elif (
            len(parts) == 4
            and parts[0] == "apigame"
            and parts[1] == "device"
        ):
            dev_id = parts[2]
            kind = parts[3]
            if kind == "session":
                self._handle_session(dev_id, payload)
            elif kind == "action":
                self._handle_action(dev_id, payload)

    # ------------------------------------------------------------------
    # Session handling
    # ------------------------------------------------------------------

    def _handle_session(self, dev_id: str, payload: dict) -> None:
        msg_type = payload.get("type")

        if msg_type == "name":
            self._handle_name(dev_id, payload)
        elif msg_type == "play_again":
            self._handle_play_again(dev_id)
        elif msg_type == "round_end":
            logger.info("[%s] Device reports round_end", dev_id)

    def _handle_name(self, dev_id: str, payload: dict) -> None:
        name = payload.get("name", "???")[:3].upper()
        logger.info("[%s] New session: %s", dev_id, name)

        # Seed calendar
        try:
            event_ids = self._calendar.seed()
            event_count = len(event_ids)
            logger.info("Calendar seeded: %d events", event_count)
        except Exception:
            logger.exception("Calendar seed failed")
            event_count = 0

        with self._lock:
            session = SessionState()
            session.start_session(name, event_count=event_count)
            self._sessions[dev_id] = session
            task = {**session.current_task, "round_id": session.round_id}

        self._publish_task(dev_id, task)
        self._publish_state(dev_id)

    def _handle_play_again(self, dev_id: str) -> None:
        with self._lock:
            session = self._sessions.get(dev_id)
            if not session or session.state != State.RESULTS:
                return
            logger.info("[%s] Play again", dev_id)
            session.play_again()

        # Seed fresh calendar
        try:
            event_ids = self._calendar.seed()
            with self._lock:
                session.event_count = len(event_ids)
        except Exception:
            logger.exception("Calendar seed failed on play-again")

        self._publish_state(dev_id)

    # ------------------------------------------------------------------
    # Action handling
    # ------------------------------------------------------------------

    def _handle_action(self, dev_id: str, payload: dict) -> None:
        verb = payload.get("verb", "").upper()

        with self._lock:
            session = self._sessions.get(dev_id)
            if not session:
                logger.warning("[%s] Action for unknown session", dev_id)
                return
            state = session.state

        if state == State.TUTORIAL:
            self._handle_tutorial_action(dev_id, session, verb)
        elif state == State.ROUND:
            self._handle_round_action(dev_id, session, verb)
        elif state == State.PLAY_AGAIN:
            self._handle_play_again_action(dev_id, session, verb)
        else:
            logger.warning(
                "[%s] Action in unexpected state %s", dev_id, state.value
            )

    def _handle_tutorial_action(
        self, dev_id: str, session: SessionState, verb: str
    ) -> None:
        with self._lock:
            expected = session.current_task["expected_verb"]
            correct = session.advance_tutorial(verb)

        if not correct:
            result = {
                "round_id": session.round_id,
                "verb": verb,
                "expected_verb": expected,
                "status": 200,
                "correct": False,
                "points_delta": 0,
                "score_total": 0,
                "latency_ms": 0,
            }
            self._publish_result(dev_id, result)
            return

        # Correct verb -- execute the calendar operation
        cal_result = self._execute_calendar_op(verb)

        result = {
            "round_id": session.round_id,
            "verb": verb,
            "expected_verb": verb,
            "status": cal_result.get("status", 200),
            "correct": True,
            "points_delta": 0,
            "score_total": 0,
            "latency_ms": cal_result.get("latency_ms", 0),
        }
        self._publish_result(dev_id, result)

        # Track event-count changes from tutorial calendar ops
        with self._lock:
            if verb == "POST":
                session.event_count += 1
            elif verb == "DELETE":
                session.event_count = max(0, session.event_count - 1)

            # Publish next task (next tutorial step or first round task)
            if session.current_task:
                task = {**session.current_task, "round_id": session.round_id}
                self._publish_task(dev_id, task)

            now_round = session.state == State.ROUND

        self._publish_state(dev_id)

        if now_round:
            logger.info("[%s] Tutorial complete -> ROUND", dev_id)

    def _handle_round_action(
        self, dev_id: str, session: SessionState, verb: str
    ) -> None:
        # Execute calendar operation
        cal_result = self._execute_calendar_op(verb)
        api_error = cal_result.get("status", 200) >= 400

        with self._lock:
            if session.state != State.ROUND:
                return  # timer expired between check and execution
            result = session.submit_action(verb, api_error=api_error)
            if result is None:
                return

        # Enrich result with HTTP info from calendar op
        result["status"] = cal_result.get("status", 200)
        result["latency_ms"] = cal_result.get("latency_ms", 0)

        self._publish_result(dev_id, result)

        # Publish next task
        with self._lock:
            if session.current_task and session.state == State.ROUND:
                task = {**session.current_task, "round_id": session.round_id}
                self._publish_task(dev_id, task)

        self._publish_state(
            dev_id,
            last_request=self._make_last_request(verb, cal_result),
        )

        logger.info(
            "[%s] %s -> correct=%s score=%d time=%ds",
            dev_id,
            verb,
            result["correct"],
            result["score_total"],
            int(session.time_left),
        )

    def _handle_play_again_action(
        self, dev_id: str, session: SessionState, verb: str
    ) -> None:
        if verb != "GET":
            return

        logger.info("[%s] Play-again GET -> starting round", dev_id)

        cal_result = self._execute_calendar_op("GET")

        with self._lock:
            session.start_round_from_play_again()
            round_id = session.round_id

        result = {
            "round_id": round_id,
            "verb": "GET",
            "expected_verb": "GET",
            "status": cal_result.get("status", 200),
            "correct": True,
            "points_delta": 0,
            "score_total": 0,
            "latency_ms": cal_result.get("latency_ms", 0),
        }
        self._publish_result(dev_id, result)

        with self._lock:
            if session.current_task:
                task = {**session.current_task, "round_id": round_id}
                self._publish_task(dev_id, task)

        self._publish_state(dev_id)

    # ------------------------------------------------------------------
    # Calendar operations
    # ------------------------------------------------------------------

    def _execute_calendar_op(self, verb: str) -> dict:
        """Run the Calendar API call for the given verb."""
        try:
            if verb == "GET":
                t0 = time.time()
                self._calendar.list_events()
                latency = round((time.time() - t0) * 1000)
                return {"status": 200, "latency_ms": latency}
            if verb == "POST":
                return self._calendar.insert_random()
            if verb == "PUT":
                return self._calendar.patch_random()
            if verb == "DELETE":
                return self._calendar.delete_random()
        except Exception:
            logger.exception("Calendar API error for %s", verb)
            return {"status": 500, "error": "api_error", "latency_ms": 0}

        return {"status": 400, "error": f"unknown verb: {verb}", "latency_ms": 0}

    # ------------------------------------------------------------------
    # Publishing helpers
    # ------------------------------------------------------------------

    def _publish(self, topic: str, data) -> None:
        self._client.publish(topic, json.dumps(data))

    def _publish_task(self, dev_id: str, task: dict) -> None:
        self._publish(f"apigame/device/{dev_id}/task", task)

    def _publish_result(self, dev_id: str, result: dict) -> None:
        self._publish(f"apigame/device/{dev_id}/result", result)

    def _publish_state(
        self, dev_id: str, last_request: Optional[dict] = None
    ) -> None:
        """Publish calendar snapshot + current player info to apigame/state."""
        try:
            events = self._calendar.list_events()
            calendar_data = [
                {
                    "id": e.get("id"),
                    "summary": e.get("summary", ""),
                    "start": e.get("start", {}).get("dateTime", ""),
                }
                for e in events
            ]
        except Exception:
            logger.exception("Failed to list events for state publish")
            calendar_data = []

        with self._lock:
            session = self._sessions.get(dev_id)
            if session and session.player_name:
                current_player = {
                    "name": session.player_name,
                    "score": session.score,
                    "time_left_s": int(session.time_left),
                }
            else:
                current_player = None

        state = {
            "calendar": calendar_data,
            "last_request": last_request,
            "current_player": current_player,
        }
        self._publish("apigame/state", state)

    def _publish_leaderboard(self) -> None:
        top = self._leaderboard.top(10)
        self._publish("apigame/leaderboard", top)
        logger.info("Published leaderboard (%d entries)", len(top))

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _make_last_request(verb: str, cal_result: dict) -> dict:
        """Build a last_request dict for the big-screen live log."""
        event_id = cal_result.get("event_id", "?")
        path_map = {
            "GET": "/calendar/v3/calendars/primary/events",
            "POST": "/calendar/v3/calendars/primary/events",
            "PUT": f"/calendar/v3/calendars/primary/events/{event_id}",
            "DELETE": f"/calendar/v3/calendars/primary/events/{event_id}",
        }
        # PUT is labelled in the UI but wire implementation is PATCH
        method = "PATCH" if verb == "PUT" else verb
        return {
            "method": method,
            "path": path_map.get(verb, "/unknown"),
            "status": cal_result.get("status", 200),
            "ts": int(time.time()),
        }

    # ------------------------------------------------------------------
    # Background timer
    # ------------------------------------------------------------------

    def _tick_loop(self) -> None:
        """Tick every session once per second for round/results countdowns."""
        while self._running:
            time.sleep(1)

            with self._lock:
                dev_ids = list(self._sessions.keys())

            for dev_id in dev_ids:
                with self._lock:
                    session = self._sessions.get(dev_id)
                    if not session:
                        continue
                    prev_state = session.state
                    session.tick(1.0)
                    new_state = session.state
                    name = session.player_name
                    score = session.score
                    round_id = session.round_id

                # ROUND -> RESULTS: record score, publish leaderboard
                if prev_state == State.ROUND and new_state == State.RESULTS:
                    logger.info(
                        "[%s] Round expired -- %s scored %d",
                        dev_id,
                        name,
                        score,
                    )
                    self._leaderboard.record(name, score, round_id)
                    self._publish_leaderboard()
                    self._publish_state(dev_id)

                # RESULTS -> IDLE: timeout with no play-again
                elif prev_state == State.RESULTS and new_state == State.IDLE:
                    logger.info("[%s] Results timeout -> IDLE", dev_id)
                    self._publish_state(dev_id)
                    with self._lock:
                        self._sessions.pop(dev_id, None)

    # ------------------------------------------------------------------
    # Admin
    # ------------------------------------------------------------------

    def _handle_admin_reset(self) -> None:
        logger.info("Admin reset received")
        try:
            self._calendar.seed()
        except Exception:
            logger.exception("Calendar seed failed on admin reset")

        with self._lock:
            self._sessions.clear()

        self._publish_state("admin")
        self._publish_leaderboard()


# ======================================================================
# Entry point
# ======================================================================

if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )
    agent = Agent()
    agent.start()
