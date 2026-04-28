# APIgame — TLDR

An arcade-style physical game that teaches HTTP verbs (GET, POST, PUT, DELETE) by making players drive *real* Google Calendar API calls from a handheld controller. You pick a verb, it actually hits the Google Calendar API, and results show up on a big screen in real time.

## How a round works

1. Player picks a 3-letter arcade name (joystick to scroll letters, button to confirm)
2. 4-step tutorial walks you through each verb: GET = refresh, POST = create event, PUT = update event, DELETE = remove event
3. 60-second timed round — prompts like "Add an event" appear, you pick the matching HTTP verb. +3 for correct, -2 for wrong. Every action hits the real Calendar API.
4. Score goes on the leaderboard. Press button to play again (skips tutorial) or wait 10s to return to idle.

## The stack (4 pieces)

| Piece | Tech | What it does |
|---|---|---|
| **Handheld controller** | ESP32 + ILI9341 TFT screen + joystick + button | Displays game UI, reads player input, sends/receives MQTT messages |
| **MQTT broker** | Mosquitto on a DigitalOcean droplet (TLS on port 8883) | Message bus between ESP32 and server — topics like `apigame/device/handheld-01/action` |
| **Python agent** | Python on the same droplet | Receives player actions via MQTT, runs game logic (scoring, task generation, state machine), calls Google Calendar API, publishes results back |
| **Big screen** | Vanilla HTML/CSS/JS served by FastAPI, connected via WebSocket | Shows a live calendar view, scrolling API request log, and leaderboard on a TV/monitor |

## Data flow for one player action

```
Player presses POST on controller
  -> ESP32 publishes MQTT: {"verb":"POST"} to apigame/device/handheld-01/action
  -> Mosquitto routes to Python agent
  -> Agent checks: is POST correct? -> scores it (+3 or -2)
  -> Agent calls Google Calendar API: events().insert(...)
  -> Agent publishes result back via MQTT to ESP32 (score, status code)
  -> Agent publishes state via MQTT to FastAPI web server
  -> FastAPI pushes state over WebSocket to big screen browser
  -> Big screen updates calendar view + live log + leaderboard
```

## Key tech decisions

- **MQTT** (not HTTP) between ESP32 and server — lightweight, persistent connection, pub/sub pattern fits the real-time game loop
- **Google Calendar API with OAuth2** — refresh token stored on the server only (never on the ESP32). Uses `google-api-python-client`.
- **SQLite** for leaderboard — simple, no setup, good enough for showcase traffic
- **No frameworks on the big screen** — vanilla JS with WebSocket, no React/Vue needed for what is essentially a real-time data display
- **FastAPI** as a WebSocket relay — subscribes to MQTT state updates and broadcasts to browser clients
- **TFT_eSPI** library for the display, **PubSubClient** for MQTT on Arduino, **ArduinoJson** for message parsing

## Infrastructure

Everything runs on one DigitalOcean droplet (`tinydrop.win`) that already hosts another project. APIgame lives at the path prefix `/apigame/` via Nginx reverse proxy. The ESP32 connects over TLS to `mqtt.tinydrop.win:8883`. The agent connects to Mosquitto locally on `127.0.0.1:1883`. Both the agent and web server run as systemd services.
