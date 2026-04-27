#include <Arduino.h>
#include <ArduinoJson.h>
#include "display.h"
#include "input.h"
#include "mqtt_client.h"
#include "game_ui.h"
#include "game_loop.h"
#include "name_entry.h"
#include "tutorial.h"
#include "secrets.h"

// ── Firmware states ─────────────────────────────────────────────────

enum AppState {
    STATE_IDLE,
    STATE_NAME_ENTRY,
    STATE_TUTORIAL,
    STATE_ROUND,
    STATE_RESULTS,
    STATE_PLAY_AGAIN
};

static AppState app_state = STATE_IDLE;

// ── Frame rate ───────────────────────────────────────────────────────

static const unsigned long FRAME_MS = 33;   // ~30 fps
static unsigned long last_frame = 0;

// ── Round + player tracking ─────────────────────────────────────────

static int  round_counter = 0;
static char current_round_id[16] = "r0";
static char player_name[4] = "";

// ── MQTT message flags (set by callback, consumed by loop) ──────────

static bool new_task        = false;
static bool new_leaderboard = false;

// ── Leaderboard storage ─────────────────────────────────────────────

struct LBEntry { char name[4]; int score; };
static LBEntry lb_entries[10];
static int     lb_count = 0;

// ── Results screen state ────────────────────────────────────────────

static unsigned long results_start_ms = 0;
static int  results_last_cd = -1;
static int  results_score   = 0;

// ── Play-again state ────────────────────────────────────────────────

static bool pa_get_pressed = false;

// ── Idle screen flag ────────────────────────────────────────────────

static bool idle_drawn = false;

// ── Helper: generate next round_id ──────────────────────────────────

static void next_round_id() {
    round_counter++;
    snprintf(current_round_id, sizeof(current_round_id), "r%d", round_counter);
}

// ── Parse leaderboard JSON ──────────────────────────────────────────

static void parse_leaderboard(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    JsonArray arr = doc.as<JsonArray>();
    lb_count = 0;
    for (JsonObject obj : arr) {
        if (lb_count >= 10) break;
        strncpy(lb_entries[lb_count].name, obj["name"] | "???", 3);
        lb_entries[lb_count].name[3] = '\0';
        lb_entries[lb_count].score = obj["score"] | 0;
        lb_count++;
    }
}

// ── MQTT callback ───────────────────────────────────────────────────

static void on_mqtt(const char* topic, const char* payload) {
    Serial.printf("MQTT [%s] %s\n", topic, payload);

    String t(topic);

    if (t.endsWith("/task")) {
        JsonDocument doc;
        if (deserializeJson(doc, payload)) return;
        const char* text = doc["text"] | "";
        game_ui_set_task(text);
        new_task = true;
    }
    else if (t.endsWith("/result")) {
        if (game_loop_is_active()) {
            game_loop_on_result(payload);
        } else {
            // Outside round (tutorial, play-again) — update UI directly
            JsonDocument doc;
            if (deserializeJson(doc, payload)) return;
            const char* verb = doc["verb"] | "";
            int  status      = doc["status"] | 0;
            bool correct     = doc["correct"] | false;
            int  delta       = doc["points_delta"] | 0;
            int  total       = doc["score_total"] | 0;
            game_ui_set_result(verb, status, correct, delta);
            game_ui_set_score(total);
        }
    }
    else if (t == "apigame/leaderboard") {
        parse_leaderboard(payload);
        new_leaderboard = true;
    }
}

// ── Publish helpers ─────────────────────────────────────────────────

static void publish_session(const char* type,
                            const char* key1 = nullptr,
                            const char* val1 = nullptr) {
    char topic[64];
    snprintf(topic, sizeof(topic), "apigame/device/%s/session", DEVICE_ID);

    JsonDocument doc;
    doc["type"] = type;
    if (key1) doc[key1] = val1;

    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    mqtt_publish(topic, buf);
    Serial.printf("Session → %s\n", buf);
}

static void publish_action(const char* verb) {
    char topic[64];
    snprintf(topic, sizeof(topic), "apigame/device/%s/action", DEVICE_ID);

    JsonDocument doc;
    doc["verb"]     = verb;
    doc["round_id"] = current_round_id;
    doc["ts"]       = millis() / 1000;

    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    mqtt_publish(topic, buf);
    Serial.printf("Action → %s\n", buf);
}

// ── Screen draw helpers ─────────────────────────────────────────────

static void draw_idle_screen() {
    TFT_eSPI& tft = display_get_tft();
    tft.fillScreen(TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("APIgame", 160, 55);

    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Learn HTTP verbs!", 160, 105);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Press button", 160, 155);
    tft.drawString("to start", 160, 178);
}

static void draw_results_screen(int score) {
    TFT_eSPI& tft = display_get_tft();
    tft.fillScreen(TFT_BLACK);

    // Title
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(3);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("ROUND OVER", 160, 6);

    // Score
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 160, 42);

    // Divider
    tft.drawFastHLine(10, 72, 300, 0x4208);

    // Leaderboard title
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("LEADERBOARD", 160, 78);

    // Entries — left column (1-5), right column (6-10)
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < lb_count && i < 5; i++) {
        snprintf(buf, sizeof(buf), "%d.%s %d", i + 1,
                 lb_entries[i].name, lb_entries[i].score);
        tft.setTextColor((i < 3) ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
        tft.drawString(buf, 16, 92 + i * 18);
    }
    for (int i = 5; i < lb_count && i < 10; i++) {
        snprintf(buf, sizeof(buf), "%d.%s %d", i + 1,
                 lb_entries[i].name, lb_entries[i].score);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(buf, 168, 92 + (i - 5) * 18);
    }

    // Divider before play-again prompt
    tft.drawFastHLine(10, 192, 300, 0x4208);
}

static void draw_results_countdown(int seconds) {
    TFT_eSPI& tft = display_get_tft();
    tft.fillRect(0, 198, 320, 42, TFT_BLACK);

    char buf[32];
    snprintf(buf, sizeof(buf), "PLAY AGAIN?  (%ds)", seconds);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 160, 216);
}

// ── Setup ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== APIgame ===");

    display_init();
    input_init();

    // Splash while connecting
    TFT_eSPI& tft = display_get_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("APIgame", 160, 50);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Connecting...", 160, 110);

    mqtt_init(DEVICE_ID);
    mqtt_set_callback(on_mqtt);
    mqtt_connect();

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Connected!", 160, 140);
    delay(600);

    app_state  = STATE_IDLE;
    idle_drawn = false;
    Serial.println("State -> IDLE");
}

// ── Loop ─────────────────────────────────────────────────────────────

void loop() {
    mqtt_loop();

    unsigned long now = millis();
    if (now - last_frame < FRAME_MS) return;
    last_frame = now;

    // Consume button every frame to keep edge-detection clean
    bool btn = button_pressed();

    switch (app_state) {

    // ──────────────────────────────────────────── IDLE ──
    case STATE_IDLE: {
        if (!idle_drawn) {
            draw_idle_screen();
            idle_drawn = true;
        }
        if (btn) {
            name_entry_init();
            app_state = STATE_NAME_ENTRY;
            Serial.println("State -> NAME_ENTRY");
        }
        break;
    }

    // ──────────────────────────────────────────── NAME_ENTRY ──
    case STATE_NAME_ENTRY: {
        JoyDirection dir = joy_direction();
        bool done = name_entry_update(dir, btn);
        name_entry_draw();

        if (done) {
            String n = name_entry_get_name();
            n.toCharArray(player_name, sizeof(player_name));
            Serial.printf("Name: %s\n", player_name);

            publish_session("name", "name", player_name);

            // Set up game UI + tutorial
            game_ui_init();
            game_ui_set_name(player_name);
            game_ui_set_score(0);
            game_ui_set_timer(0);
            strncpy(current_round_id, "r0", sizeof(current_round_id));

            tutorial_init();
            game_ui_set_visible_boxes(1);
            game_ui_suppress_status(true);
            game_ui_set_task(tutorial_get_prompt());

            app_state = STATE_TUTORIAL;
            Serial.println("State -> TUTORIAL");
        }
        break;
    }

    // ──────────────────────────────────────────── TUTORIAL ──
    case STATE_TUTORIAL: {
        int jx = joy_raw_x();
        int jy = joy_raw_y();
        game_ui_update(jx, jy);

        if (btn) {
            const char* verb = game_ui_get_selected_verb();

            if (tutorial_check_verb(verb)) {
                publish_action(verb);
                int step = tutorial_current_step() + 1;
                Serial.printf("Tutorial %d/4 done\n", step);
                tutorial_advance();

                if (tutorial_current_step() >= 4) {
                    // Tutorial complete -> start round
                    next_round_id();
                    publish_session("round_start", "round_id", current_round_id);
                    game_loop_start(current_round_id);
                    game_ui_set_visible_boxes(4);
                    game_ui_suppress_status(false);
                    game_ui_set_task("Waiting...");
                    app_state = STATE_ROUND;
                    Serial.printf("State -> ROUND (%s)\n", current_round_id);
                    break;
                }

                game_ui_set_visible_boxes(tutorial_current_step() + 1);
                game_ui_set_task(tutorial_get_prompt());
            } else {
                tutorial_show_hint();
                Serial.printf("Wrong: %s (expected %s)\n",
                              verb, tutorial_get_expected_verb());
            }
        }

        game_ui_draw();
        tutorial_draw();
        new_task = false;   // discard task flags during tutorial
        break;
    }

    // ──────────────────────────────────────────── ROUND ──
    case STATE_ROUND: {
        new_task = false;   // tasks applied by callback via game_ui_set_task

        int jx = joy_raw_x();
        int jy = joy_raw_y();
        game_ui_update(jx, jy);

        if (btn) {
            const char* verb = game_ui_get_selected_verb();
            publish_action(verb);
        }

        if (game_loop_tick()) {
            // Timer expired -> results
            publish_session("round_end", "round_id", current_round_id);
            results_score    = game_loop_get_score();
            results_start_ms = millis();
            results_last_cd  = -1;
            draw_results_screen(results_score);
            app_state = STATE_RESULTS;
            Serial.printf("State -> RESULTS (score %d)\n", results_score);
            break;
        }

        game_ui_draw();
        break;
    }

    // ──────────────────────────────────────────── RESULTS ──
    case STATE_RESULTS: {
        // Redraw if fresh leaderboard arrived
        if (new_leaderboard) {
            new_leaderboard = false;
            draw_results_screen(results_score);
            results_last_cd = -1;
        }

        int remaining = 10 - (int)((millis() - results_start_ms) / 1000);
        if (remaining < 0) remaining = 0;

        if (remaining != results_last_cd) {
            draw_results_countdown(remaining);
            results_last_cd = remaining;
        }

        if (btn && remaining > 0) {
            // Play again — keep name, skip tutorial
            publish_session("play_again");
            next_round_id();

            game_ui_init();
            game_ui_set_name(player_name);
            game_ui_set_visible_boxes(1);       // GET only
            game_ui_set_task("Refresh calendar");
            game_ui_set_score(0);
            game_ui_set_timer(60);
            game_ui_suppress_status(false);

            pa_get_pressed = false;
            new_task       = false;

            app_state = STATE_PLAY_AGAIN;
            Serial.println("State -> PLAY_AGAIN");
            break;
        }

        if (remaining <= 0) {
            idle_drawn = false;
            app_state  = STATE_IDLE;
            Serial.println("State -> IDLE (timeout)");
        }
        break;
    }

    // ──────────────────────────────────────────── PLAY_AGAIN ──
    case STATE_PLAY_AGAIN: {
        int jx = joy_raw_x();
        int jy = joy_raw_y();
        game_ui_update(jx, jy);

        if (!pa_get_pressed && btn) {
            const char* verb = game_ui_get_selected_verb();
            if (strcmp(verb, "GET") == 0) {
                publish_action(verb);
                pa_get_pressed = true;
                game_ui_set_task("Loading...");
                Serial.println("PLAY_AGAIN: GET pressed, waiting for task");
            }
        }

        if (pa_get_pressed && new_task) {
            // First real task arrived -> start round
            new_task = false;
            publish_session("round_start", "round_id", current_round_id);
            game_loop_start(current_round_id);
            game_ui_set_visible_boxes(4);
            app_state = STATE_ROUND;
            Serial.printf("State -> ROUND (%s) [play-again]\n", current_round_id);
            break;
        }

        game_ui_draw();
        break;
    }

    }  // switch
}
