#include <Arduino.h>
#include <ArduinoJson.h>
#include "display.h"
#include "input.h"
#include "mqtt_client.h"
#include "game_ui.h"
#include "name_entry.h"
#include "tutorial.h"
#include "secrets.h"

// ── Firmware states ─────────────────────────────────────────────────
enum AppState {
    STATE_NAME_ENTRY,
    STATE_TUTORIAL,
    STATE_GAME
};

static AppState app_state = STATE_NAME_ENTRY;

// ── Frame rate ───────────────────────────────────────────────────────
static const unsigned long FRAME_MS = 33;   // ~30 fps
static unsigned long last_frame = 0;

// ── MQTT message handler ─────────────────────────────────────────────

static void on_mqtt(const char* topic, const char* payload) {
    Serial.printf("MQTT [%s] %s\n", topic, payload);

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("  JSON parse error");
        return;
    }

    String t(topic);

    if (t.endsWith("/task")) {
        const char* text = doc["text"] | "";
        game_ui_set_task(text);
    }
    else if (t.endsWith("/result")) {
        const char* verb    = doc["verb"] | "";
        int         status  = doc["status"] | 0;
        bool        correct = doc["correct"] | false;
        int         delta   = doc["points_delta"] | 0;
        int         total   = doc["score_total"] | 0;

        game_ui_set_result(verb, status, correct, delta);
        game_ui_set_score(total);
    }
}

// ── Publish player action ────────────────────────────────────────────

static void publish_action(const char* verb) {
    char topic[64];
    snprintf(topic, sizeof(topic), "apigame/device/%s/action", DEVICE_ID);

    JsonDocument doc;
    doc["verb"]     = verb;
    doc["round_id"] = "r0";          // placeholder until state machine (Step 14)
    doc["ts"]       = millis() / 1000;

    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    mqtt_publish(topic, buf);

    Serial.printf("Action → %s\n", buf);
}

// ── Publish session message ──────────────────────────────────────────

static void publish_session_name(const char* name) {
    char topic[64];
    snprintf(topic, sizeof(topic), "apigame/device/%s/session", DEVICE_ID);

    JsonDocument doc;
    doc["type"] = "name";
    doc["name"] = name;

    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    mqtt_publish(topic, buf);

    Serial.printf("Published session: %s\n", buf);
}

// ── Setup ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== APIgame ===");

    display_init();
    input_init();

    // splash while connecting
    TFT_eSPI& tft = display_get_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("APIgame", 160, 50);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Connecting WiFi...", 160, 110);

    // connect (blocking)
    mqtt_init(DEVICE_ID);
    mqtt_set_callback(on_mqtt);
    mqtt_connect();

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Connected!", 160, 140);
    delay(600);

    // start in name entry state
    app_state = STATE_NAME_ENTRY;
    name_entry_init();

    Serial.println("Ready — enter your name");
}

// ── Loop ─────────────────────────────────────────────────────────────

void loop() {
    // MQTT keep-alive / reconnect (non-blocking)
    mqtt_loop();

    // frame-rate limiter
    unsigned long now = millis();
    if (now - last_frame < FRAME_MS) return;
    last_frame = now;

    switch (app_state) {

    case STATE_NAME_ENTRY: {
        JoyDirection dir = joy_direction();
        bool btn = button_pressed();

        bool done = name_entry_update(dir, btn);
        name_entry_draw();

        if (done) {
            String name = name_entry_get_name();
            Serial.printf("Name entered: %s\n", name.c_str());

            publish_session_name(name.c_str());

            // init game UI and tutorial
            game_ui_init();
            game_ui_set_name(name.c_str());
            game_ui_set_score(0);
            game_ui_set_timer(0);

            tutorial_init();
            game_ui_set_visible_boxes(1);
            game_ui_suppress_status(true);
            game_ui_set_task(tutorial_get_prompt());

            app_state = STATE_TUTORIAL;
            Serial.println("State → TUTORIAL");
        }
        break;
    }

    case STATE_TUTORIAL: {
        int jx = joy_raw_x();
        int jy = joy_raw_y();
        game_ui_update(jx, jy);

        if (button_pressed()) {
            const char* verb = game_ui_get_selected_verb();

            if (tutorial_check_verb(verb)) {
                // Correct — publish action, advance tutorial
                publish_action(verb);
                int completed = tutorial_current_step() + 1;
                Serial.printf("Tutorial step %d/4 complete\n", completed);
                tutorial_advance();

                if (tutorial_current_step() >= 4) {
                    // Tutorial done → transition to round
                    app_state = STATE_GAME;
                    game_ui_suppress_status(false);
                    game_ui_set_timer(60);
                    game_ui_set_task("Waiting...");
                    Serial.println("State → GAME (tutorial complete)");
                    break;   // skip draw — next frame renders clean game UI
                }

                game_ui_set_visible_boxes(tutorial_current_step() + 1);
                game_ui_set_task(tutorial_get_prompt());
            } else {
                // Wrong verb — show hint, no MQTT, no penalty
                tutorial_show_hint();
                Serial.printf("Wrong verb: %s (expected %s)\n",
                              verb, tutorial_get_expected_verb());
            }
        }

        game_ui_draw();
        tutorial_draw();
        break;
    }

    case STATE_GAME: {
        int jx = joy_raw_x();
        int jy = joy_raw_y();

        game_ui_update(jx, jy);

        if (button_pressed()) {
            const char* verb = game_ui_get_selected_verb();
            publish_action(verb);
        }

        game_ui_draw();
        break;
    }

    }  // switch
}
