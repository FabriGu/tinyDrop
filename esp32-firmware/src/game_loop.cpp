#include "game_loop.h"
#include "game_ui.h"
#include <ArduinoJson.h>

static char          gl_round_id[16];
static int           gl_score;
static int           gl_time_left;
static bool          gl_active;
static unsigned long gl_last_tick;

void game_loop_start(const char* round_id) {
    strncpy(gl_round_id, round_id, sizeof(gl_round_id) - 1);
    gl_round_id[sizeof(gl_round_id) - 1] = '\0';
    gl_score     = 0;
    gl_time_left = 60;
    gl_active    = true;
    gl_last_tick = millis();

    game_ui_set_score(0);
    game_ui_set_timer(60);
}

bool game_loop_tick() {
    if (!gl_active) return false;

    unsigned long now = millis();
    if (now - gl_last_tick >= 1000) {
        gl_last_tick += 1000;
        gl_time_left--;
        game_ui_set_timer(gl_time_left);

        if (gl_time_left <= 0) {
            gl_active = false;
            return true;
        }
    }
    return false;
}

void game_loop_on_result(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;

    const char* verb = doc["verb"] | "";
    int  status      = doc["status"] | 0;
    bool correct     = doc["correct"] | false;
    int  delta       = doc["points_delta"] | 0;
    int  total       = doc["score_total"] | 0;

    gl_score = total;
    game_ui_set_result(verb, status, correct, delta);
    game_ui_set_score(total);
}

int         game_loop_get_time_left() { return gl_time_left; }
bool        game_loop_is_active()     { return gl_active; }
int         game_loop_get_score()     { return gl_score; }
const char* game_loop_get_round_id()  { return gl_round_id; }
