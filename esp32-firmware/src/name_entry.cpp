#include "name_entry.h"
#include "display.h"

// ── Layout (320×240 landscape) ──────────────────────────────────────
#define SCR_W  320
#define SCR_H  240

#define TITLE_Y       30
#define SLOT_Y        90      // top of letter area
#define SLOT_SPACING  60      // horizontal distance between slot centres
#define SLOT_START_X  ((SCR_W - (2 * SLOT_SPACING)) / 2)   // centre 3 slots

#define CURSOR_Y     (SLOT_Y + 50)   // underline position
#define CURSOR_W      30
#define CURSOR_H       4

#define HINT_Y       180

// Repeat delay for held joystick
#define MOVE_DELAY_MS 150

// ── State ───────────────────────────────────────────────────────────
static char   letters[3];       // 0-25 mapped to 'A'-'Z'
static int    slot;             // current slot 0-2
static bool   dirty;            // needs full redraw
static unsigned long last_move; // for joystick repeat rate

// ── Public API ──────────────────────────────────────────────────────

void name_entry_init() {
    letters[0] = 0;
    letters[1] = 0;
    letters[2] = 0;
    slot  = 0;
    dirty = true;
    last_move = 0;
}

bool name_entry_update(JoyDirection dir, bool button) {
    unsigned long now = millis();

    // joystick up/down — cycle letter with repeat rate
    if ((dir == JOY_UP || dir == JOY_DOWN) && (now - last_move >= MOVE_DELAY_MS)) {
        last_move = now;
        if (dir == JOY_UP) {
            letters[slot] = (letters[slot] + 1) % 26;
        } else {
            letters[slot] = (letters[slot] + 25) % 26;   // wrap backwards
        }
        dirty = true;
    }

    // button — lock current letter, advance slot
    if (button) {
        slot++;
        dirty = true;
        if (slot >= 3) {
            return true;   // name complete
        }
    }

    return false;
}

void name_entry_draw() {
    if (!dirty) return;
    dirty = false;

    TFT_eSPI& t = display_get_tft();

    t.fillScreen(TFT_BLACK);

    // title
    t.setTextDatum(TC_DATUM);
    t.setTextSize(3);
    t.setTextColor(TFT_CYAN, TFT_BLACK);
    t.drawString("ENTER NAME", SCR_W / 2, TITLE_Y);

    // draw the three letter slots
    for (int i = 0; i < 3; i++) {
        int cx = SLOT_START_X + i * SLOT_SPACING;
        char ch[2] = { (char)('A' + letters[i]), '\0' };

        // letter colour: locked slots white, active slot yellow
        bool active = (i == slot && slot < 3);
        uint16_t col = active ? TFT_YELLOW : TFT_WHITE;

        t.setTextDatum(TC_DATUM);
        t.setTextSize(5);
        t.setTextColor(col, TFT_BLACK);
        t.drawString(ch, cx, SLOT_Y);

        // cursor underline on active slot
        if (active) {
            int lx = cx - CURSOR_W / 2;
            t.fillRect(lx, CURSOR_Y, CURSOR_W, CURSOR_H, TFT_YELLOW);
        }
    }

    // hint text
    t.setTextDatum(TC_DATUM);
    t.setTextSize(1);
    t.setTextColor(0x7BEF, TFT_BLACK);   // grey
    if (slot < 3) {
        t.drawString("UP/DOWN = letter   BUTTON = lock", SCR_W / 2, HINT_Y);
    }
}

String name_entry_get_name() {
    char buf[4];
    buf[0] = 'A' + letters[0];
    buf[1] = 'A' + letters[1];
    buf[2] = 'A' + letters[2];
    buf[3] = '\0';
    return String(buf);
}
