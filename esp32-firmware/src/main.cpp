#include <Arduino.h>
#include <ArduinoJson.h>
#include "display.h"
#include "input.h"
#include "mqtt_client.h"
#include "game_ui.h"
#include "secrets.h"

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
        // timer comes from agent via result messages, not task
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

    // start game UI
    game_ui_init();
    game_ui_set_name("---");
    game_ui_set_score(0);
    game_ui_set_timer(60);
    game_ui_set_task("Waiting...");

    Serial.println("Ready — move joystick, press button");
}

// ── Loop ─────────────────────────────────────────────────────────────

void loop() {
    // MQTT keep-alive / reconnect (non-blocking)
    mqtt_loop();

    // frame-rate limiter
    unsigned long now = millis();
    if (now - last_frame < FRAME_MS) return;
    last_frame = now;

    // read raw joystick
    int jx = joy_raw_x();
    int jy = joy_raw_y();

    // update sprite position + selection
    game_ui_update(jx, jy);

    // button → publish selected verb
    if (button_pressed()) {
        const char* verb = game_ui_get_selected_verb();
        publish_action(verb);
    }

    // render
    game_ui_draw();
}
