"""APIgame web dashboard — FastAPI + WebSocket + MQTT relay.

Serves the big-screen static files and relays MQTT state/leaderboard
messages to all connected WebSocket clients.

Run locally:
    uvicorn app:app --host 127.0.0.1 --port 8010
"""

import asyncio
import json
import logging
import os
import threading
from contextlib import asynccontextmanager
from pathlib import Path

import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(name)s %(levelname)s %(message)s",
)
logger = logging.getLogger("apigame-web")

ENV_PATH = Path(__file__).resolve().parent.parent / "config" / ".env"
load_dotenv(ENV_PATH)

# ---------------------------------------------------------------------------
# WebSocket client registry (thread-safe via _lock)
# ---------------------------------------------------------------------------

_ws_clients: dict[WebSocket, list[str]] = {}
_lock = threading.Lock()


def _enqueue_to_all(message: str) -> None:
    """Thread-safe: push a message to every connected client's buffer."""
    with _lock:
        for buf in _ws_clients.values():
            buf.append(message)


# ---------------------------------------------------------------------------
# MQTT client (runs in a background thread via paho loop_start)
# ---------------------------------------------------------------------------

def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("MQTT connected")
        client.subscribe("apigame/state")
        client.subscribe("apigame/leaderboard")
    else:
        logger.error("MQTT connect failed: rc=%s", rc)


def _on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.warning("MQTT unexpected disconnect: rc=%s", rc)


def _on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
    except (json.JSONDecodeError, UnicodeDecodeError):
        return

    topic_key = msg.topic.split("/")[-1]  # "state" or "leaderboard"
    envelope = json.dumps({"topic": topic_key, "data": data})
    _enqueue_to_all(envelope)


def _start_mqtt() -> mqtt.Client:
    try:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION1,
            client_id="apigame-web",
        )
    except (AttributeError, TypeError):
        client = mqtt.Client(client_id="apigame-web")

    host = os.getenv("MQTT_HOST", "127.0.0.1")
    port = int(os.getenv("MQTT_PORT", "1883"))
    user = os.getenv("MQTT_USER", "esp32")
    pw = os.getenv("MQTT_PASS", "")

    client.username_pw_set(user, pw)
    client.on_connect = _on_connect
    client.on_disconnect = _on_disconnect
    client.on_message = _on_message
    client.connect(host, port)
    client.loop_start()
    return client


# ---------------------------------------------------------------------------
# FastAPI lifespan
# ---------------------------------------------------------------------------

_mqtt_client: mqtt.Client | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _mqtt_client
    _mqtt_client = _start_mqtt()
    logger.info("APIgame web started")
    yield
    _mqtt_client.loop_stop()
    _mqtt_client.disconnect()
    logger.info("APIgame web stopped")


app = FastAPI(lifespan=lifespan)


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.get("/health")
async def health():
    return {"status": "ok"}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    buf: list[str] = []
    with _lock:
        _ws_clients[ws] = buf
    logger.info("WS client connected (%d total)", len(_ws_clients))
    try:
        while True:
            await asyncio.sleep(0.05)
            with _lock:
                pending = list(buf)
                buf.clear()
            for msg in pending:
                await ws.send_text(msg)
    except Exception:
        pass
    finally:
        with _lock:
            _ws_clients.pop(ws, None)
        logger.info("WS client disconnected (%d remaining)", len(_ws_clients))


# Static files — mount last so explicit routes take priority
app.mount("/", StaticFiles(directory="static", html=True), name="static")
