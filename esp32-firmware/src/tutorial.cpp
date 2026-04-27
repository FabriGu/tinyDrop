#include "tutorial.h"
#include "display.h"

// ── Screen layout constants (must match game_ui.cpp) ────────────────
#define SCR_W     320
#define STAT_H     25
#define BOX_W      74
#define BOX_H      36
#define PLAY_Y     78
#define PLAY_BOT  190

// Box top-left positions (same geometry as game_ui.cpp)
static const int bx_x[4] = { 4, SCR_W - BOX_W - 4, 4, SCR_W - BOX_W - 4 };
static const int bx_y[4] = { PLAY_Y, PLAY_Y, PLAY_BOT - BOX_H, PLAY_BOT - BOX_H };

// ── Tutorial step definitions ───────────────────────────────────────

struct TutStep {
    const char* prompt;
    const char* verb;
    int         box;      // 0=GET  1=POST  2=PUT  3=DELETE
};

static const TutStep steps[4] = {
    { "Refresh calendar", "GET",    0 },
    { "Add an event",     "POST",   1 },
    { "Update an event",  "PUT",    2 },
    { "Remove an event",  "DELETE", 3 },
};

// ── State ───────────────────────────────────────────────────────────

static int           cur_step;
static bool          banner_dirty;
static bool          hint_active;
static unsigned long hint_start;
static int           hint_box;       // which box to highlight (-1 = none)
static bool          hint_cleanup;   // need to erase hint border next frame

// ── Public API ──────────────────────────────────────────────────────

void tutorial_init() {
    cur_step     = 0;
    banner_dirty = true;
    hint_active  = false;
    hint_start   = 0;
    hint_box     = -1;
    hint_cleanup = false;
}

int tutorial_current_step() {
    return cur_step;
}

const char* tutorial_get_prompt() {
    return (cur_step < 4) ? steps[cur_step].prompt : "";
}

const char* tutorial_get_expected_verb() {
    return (cur_step < 4) ? steps[cur_step].verb : "";
}

bool tutorial_check_verb(const char* verb) {
    if (cur_step >= 4) return false;
    return strcmp(verb, steps[cur_step].verb) == 0;
}

void tutorial_show_hint() {
    hint_active = true;
    hint_start  = millis();
    hint_box    = (cur_step < 4) ? steps[cur_step].box : -1;
}

void tutorial_advance() {
    if (cur_step >= 4) return;

    // Clean up any active hint border immediately
    if (hint_active && hint_box >= 0) {
        TFT_eSPI& t = display_get_tft();
        int x = bx_x[hint_box], y = bx_y[hint_box];
        t.drawRoundRect(x - 2, y - 2, BOX_W + 4, BOX_H + 4, 6, TFT_BLACK);
        t.drawRoundRect(x - 3, y - 3, BOX_W + 6, BOX_H + 6, 7, TFT_BLACK);
    }

    hint_active  = false;
    hint_cleanup = false;
    hint_box     = -1;
    cur_step++;
    banner_dirty = true;
}

// ── Drawing (call after game_ui_draw) ───────────────────────────────

void tutorial_draw() {
    TFT_eSPI& t = display_get_tft();
    unsigned long now = millis();

    // ── Check hint expiry (1 second) ──
    if (hint_active && now - hint_start > 1000) {
        hint_active  = false;
        hint_cleanup = true;
    }

    // ── Banner (only when step changes) ──
    if (banner_dirty) {
        uint16_t bg = 0x0010;   // very dark blue
        t.fillRect(0, 0, SCR_W, STAT_H, bg);

        if (cur_step < 4) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Tutorial %d/4", cur_step + 1);
            t.setTextDatum(TL_DATUM);
            t.setTextSize(2);
            t.setTextColor(TFT_CYAN, bg);
            t.drawString(buf, 8, 5);

            snprintf(buf, sizeof(buf), "Press %s", steps[cur_step].verb);
            t.setTextDatum(TR_DATUM);
            t.setTextColor(TFT_YELLOW, bg);
            t.drawString(buf, SCR_W - 8, 5);
        } else {
            t.setTextDatum(MC_DATUM);
            t.setTextSize(2);
            t.setTextColor(TFT_GREEN, bg);
            t.drawString("Tutorial Complete!", SCR_W / 2, STAT_H / 2 + 2);
        }

        t.drawFastHLine(0, STAT_H, SCR_W, 0x4208);   // divider
        banner_dirty = false;
    }

    // ── Hint: pulsing border around correct box ──
    if (hint_active && hint_box >= 0) {
        int x = bx_x[hint_box], y = bx_y[hint_box];
        bool flash = ((now - hint_start) / 150) % 2 == 0;
        uint16_t c = flash ? TFT_WHITE : TFT_YELLOW;

        t.drawRoundRect(x - 2, y - 2, BOX_W + 4, BOX_H + 4, 6, c);
        t.drawRoundRect(x - 3, y - 3, BOX_W + 6, BOX_H + 6, 7, c);
    }

    // ── Erase hint border after natural expiry ──
    if (hint_cleanup && hint_box >= 0) {
        int x = bx_x[hint_box], y = bx_y[hint_box];
        t.drawRoundRect(x - 2, y - 2, BOX_W + 4, BOX_H + 4, 6, TFT_BLACK);
        t.drawRoundRect(x - 3, y - 3, BOX_W + 6, BOX_H + 6, 7, TFT_BLACK);
        hint_cleanup = false;
        hint_box     = -1;
    }
}
