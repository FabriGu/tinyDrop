#include "game_ui.h"
#include "display.h"
#include "input.h"
#include <math.h>

// ── Screen layout (320×240 landscape) ────────────────────────────────
//
//  y=0   STATUS BAR   name · score · timer        (h=25)
//  y=25  ─────────────────────────────────────────
//  y=28  TASK TEXT     centered, large             (h=46)
//  y=74  ─────────────────────────────────────────
//  y=78  [GET]                           [POST]   ← top-row boxes
//        │                                    │
//        │          ● sprite                  │   ← 2D play area
//        │                                    │
//  y=154 [PUT]                           [DEL]    ← bottom-row boxes
//  y=190  ─────────────────────────────────────────
//  y=194 RESPONSE ROW  verb · status · ✓/✗ · Δ   (h=24)
//  y=218  ─────────────────────────────────────────

#define SCR_W  320
#define SCR_H  240

// Status bar
#define STAT_Y      0
#define STAT_H     25

// Task area
#define TASK_Y     28
#define TASK_H     46
#define TASK_LINE  74

// Play area
#define PLAY_Y     78
#define PLAY_BOT  190
#define PLAY_H    (PLAY_BOT - PLAY_Y)   // 112

// Response row
#define RESP_LINE 190
#define RESP_Y    194
#define RESP_H     24

// Verb boxes — 2×2 in corners of play area
#define BOX_W      74
#define BOX_H      36

// Box positions (x, y for top-left corner of each box)
static const int bx_x[4] = { 4,       SCR_W - BOX_W - 4,   4,       SCR_W - BOX_W - 4 };
static const int bx_y[4] = { PLAY_Y,  PLAY_Y,               PLAY_BOT - BOX_H, PLAY_BOT - BOX_H };
// Box centers
static const int bx_cx[4] = { 4 + BOX_W/2,  SCR_W - 4 - BOX_W/2,  4 + BOX_W/2,  SCR_W - 4 - BOX_W/2 };
static const int bx_cy[4] = { PLAY_Y + BOX_H/2, PLAY_Y + BOX_H/2,
                               PLAY_BOT - BOX_H/2, PLAY_BOT - BOX_H/2 };

// Play-area center (quadrant boundary)
#define PLAY_CX  (SCR_W / 2)              // 160
#define PLAY_CY  (PLAY_Y + PLAY_H / 2)    // 134

// Sprite movement bounds — inset by sprite half-extents so erase rect
// (27×31 centered on sprite) never reaches outside the play area
#define SPR_MIN_X  16.0f
#define SPR_MAX_X  304.0f
#define SPR_MIN_Y  (PLAY_Y + 15.0f)       // 93  — erase top = 78 = PLAY_Y
#define SPR_MAX_Y  (PLAY_BOT - 15.0f)     // 175 — erase bot = 190 = PLAY_BOT

// Speed: pixels per frame at full joystick tilt (~30 fps)
#define SPR_SPEED  8.0f

// ── Colours (RGB565) ─────────────────────────────────────────────────
//  GET=blue  POST=green  PUT=orange  DELETE=red
static const uint16_t verb_col[4] = { 0x34DF, 0x07E0, 0xFD20, 0xF800 };

// Labels (short for box, full for MQTT)
static const char* verb_lbl[4]  = { "GET", "POST", "PUT",  "DEL" };
static const char* verb_full[4] = { "GET", "POST", "PUT",  "DELETE" };

// ── Module state ─────────────────────────────────────────────────────
static float  spr_x, spr_y;          // current position
static float  drw_x, drw_y;          // last drawn position
static int    drw_walk;               // last drawn walk frame
static bool   spr_moving;
static int    walk_fr;
static unsigned long walk_ms;

static int    sel;                    // selected box 0-3
static int    old_sel;                // previous (for dirty redraw)
static bool   sel_dirty;

static char   g_task[64];
static char   g_name[4];
static int    g_score;
static int    g_timer;

static char   g_rv[8];               // result verb
static int    g_rs;                   // result status code
static bool   g_rc;                   // result correct?
static int    g_rd;                   // result points delta
static bool   g_has_res;

static bool   d_task, d_stat, d_res;  // dirty flags
static bool   full_redraw;

static unsigned long score_chg_ms;
static int    score_chg_sign;         // +1 or -1 for flash colour

static int    visible_count;          // how many verb boxes to show (1-4)
static bool   boxes_dirty;
static bool   status_suppressed;

// ── Helpers ──────────────────────────────────────────────────────────

static int nearest_box() {
    bool left = (spr_x < PLAY_CX);
    bool top  = (spr_y < PLAY_CY);
    int c;
    if (left && top)        c = 0;   // GET
    else if (!left && top)  c = 1;   // POST
    else if (left && !top)  c = 2;   // PUT
    else                    c = 3;   // DELETE

    if (c < visible_count) return c;

    // Snap to nearest visible box
    float best_d = 1e9f;
    int best = 0;
    for (int i = 0; i < visible_count; i++) {
        float dx = spr_x - bx_cx[i];
        float dy = spr_y - bx_cy[i];
        float d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static bool spr_overlaps_box(float sx, float sy, int i) {
    float sl = sx - 13, sr = sx + 13;
    float st = sy - 15, sb = sy + 15;
    float bl = bx_x[i],  br = bl + BOX_W;
    float bt = bx_y[i],  bb = bt + BOX_H;
    return (sl < br && sr > bl && st < bb && sb > bt);
}

// ── Drawing primitives ───────────────────────────────────────────────

static void draw_dividers(TFT_eSPI& t) {
    uint16_t lc = 0x4208;
    t.drawFastHLine(0, STAT_H, SCR_W, lc);
    t.drawFastHLine(0, TASK_LINE, SCR_W, lc);
    t.drawFastHLine(0, RESP_LINE, SCR_W, lc);

    // subtle dotted quadrant cross inside play area
    uint16_t dc = 0x2104;
    for (int y = PLAY_Y + 4; y < PLAY_BOT; y += 4) t.drawPixel(PLAY_CX, y, dc);
    for (int x = 4; x < SCR_W - 4; x += 4)        t.drawPixel(x, PLAY_CY, dc);
}

static void draw_box(TFT_eSPI& t, int i, bool is_sel) {
    int x = bx_x[i], y = bx_y[i];
    uint16_t c = verb_col[i];

    t.fillRoundRect(x, y, BOX_W, BOX_H, 5, c);

    if (is_sel) {
        t.drawRoundRect(x,   y,   BOX_W,     BOX_H,     5, TFT_WHITE);
        t.drawRoundRect(x+1, y+1, BOX_W - 2, BOX_H - 2, 4, TFT_WHITE);
        t.drawRoundRect(x+2, y+2, BOX_W - 4, BOX_H - 4, 3, TFT_WHITE);
    }

    t.setTextDatum(MC_DATUM);
    t.setTextSize(2);
    t.setTextColor(TFT_WHITE);
    t.drawString(verb_lbl[i], x + BOX_W / 2, y + BOX_H / 2);
}

static void erase_spr(TFT_eSPI& t, float sx, float sy) {
    t.fillRect((int)sx - 13, (int)sy - 15, 27, 31, TFT_BLACK);
}

static void draw_spr(TFT_eSPI& t, float sx, float sy) {
    int cx = (int)sx, cy = (int)sy;
    uint16_t bc = (sel >= 0 && sel < 4) ? verb_col[sel] : TFT_WHITE;

    // head
    t.fillCircle(cx, cy - 8, 4, TFT_WHITE);
    // body triangle
    t.fillTriangle(cx - 7, cy + 2, cx + 7, cy + 2, cx, cy - 3, bc);
    t.drawTriangle(cx - 7, cy + 2, cx + 7, cy + 2, cx, cy - 3, TFT_WHITE);
    // legs (walking animation)
    if (spr_moving) {
        if (walk_fr % 2 == 0) {
            t.drawLine(cx - 2, cy + 3, cx - 6, cy + 11, TFT_WHITE);
            t.drawLine(cx + 2, cy + 3, cx + 1, cy + 11, TFT_WHITE);
        } else {
            t.drawLine(cx - 2, cy + 3, cx - 1, cy + 11, TFT_WHITE);
            t.drawLine(cx + 2, cy + 3, cx + 6, cy + 11, TFT_WHITE);
        }
    } else {
        t.drawLine(cx - 2, cy + 3, cx - 4, cy + 11, TFT_WHITE);
        t.drawLine(cx + 2, cy + 3, cx + 4, cy + 11, TFT_WHITE);
    }
}

static void draw_status(TFT_eSPI& t) {
    t.fillRect(0, STAT_Y, SCR_W, STAT_H, TFT_BLACK);
    t.setTextSize(2);

    // name (left, cyan)
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_CYAN, TFT_BLACK);
    t.drawString(g_name, 8, 5);

    // score (centre — flash green/red on change)
    uint16_t sc = TFT_WHITE;
    if (score_chg_sign != 0 && millis() - score_chg_ms < 400)
        sc = (score_chg_sign > 0) ? TFT_GREEN : TFT_RED;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_score);
    t.setTextDatum(TC_DATUM);
    t.setTextColor(sc, TFT_BLACK);
    t.drawString(buf, SCR_W / 2, 5);

    // timer (right)
    snprintf(buf, sizeof(buf), "%d:%02d", g_timer / 60, g_timer % 60);
    t.setTextDatum(TR_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.drawString(buf, SCR_W - 8, 5);

    // restore divider cleared by fillRect
    t.drawFastHLine(0, STAT_H, SCR_W, 0x4208);
}

static void draw_task_area(TFT_eSPI& t) {
    t.fillRect(0, TASK_Y, SCR_W, TASK_H, TFT_BLACK);
    if (g_task[0]) {
        t.setTextDatum(MC_DATUM);
        t.setTextSize(3);
        t.setTextColor(TFT_YELLOW, TFT_BLACK);
        t.drawString(g_task, SCR_W / 2, TASK_Y + TASK_H / 2);
    }
    t.drawFastHLine(0, TASK_LINE, SCR_W, 0x4208);
}

static void draw_result(TFT_eSPI& t) {
    t.fillRect(0, RESP_Y, SCR_W, RESP_H, TFT_BLACK);
    if (!g_has_res) return;

    int x = 20, ty = RESP_Y + 4;

    // verb in its colour
    int vi = 0;
    for (int i = 0; i < 4; i++) {
        if (strcmp(g_rv, verb_full[i]) == 0) { vi = i; break; }
    }
    t.setTextDatum(TL_DATUM);
    t.setTextSize(2);
    t.setTextColor(verb_col[vi], TFT_BLACK);
    x += t.drawString(g_rv, x, ty) + 8;

    // status code in white
    char sc[8];
    snprintf(sc, sizeof(sc), "%d", g_rs);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    x += t.drawString(sc, x, ty) + 10;

    // checkmark or cross
    int iy = ty + 2;
    if (g_rc) {
        t.drawLine(x, iy + 5, x + 4, iy + 9, TFT_GREEN);
        t.drawLine(x + 4, iy + 9, x + 10, iy + 1, TFT_GREEN);
        t.drawLine(x, iy + 6, x + 4, iy + 10, TFT_GREEN);
        t.drawLine(x + 4, iy + 10, x + 10, iy + 2, TFT_GREEN);
    } else {
        t.drawLine(x, iy, x + 9, iy + 9, TFT_RED);
        t.drawLine(x + 9, iy, x, iy + 9, TFT_RED);
        t.drawLine(x + 1, iy, x + 10, iy + 9, TFT_RED);
        t.drawLine(x + 10, iy, x + 1, iy + 9, TFT_RED);
    }
    x += 18;

    // delta
    char ds[8];
    snprintf(ds, sizeof(ds), "%+d", g_rd);
    t.setTextColor(g_rc ? TFT_GREEN : TFT_RED, TFT_BLACK);
    t.drawString(ds, x, ty);
}

// ── Public API ───────────────────────────────────────────────────────

void game_ui_init() {
    spr_x = PLAY_CX;  spr_y = PLAY_CY;
    drw_x = spr_x;    drw_y = spr_y;  drw_walk = 0;
    spr_moving = false;
    walk_fr = 0;  walk_ms = 0;
    sel = 0;  old_sel = -1;  sel_dirty = false;

    memset(g_task, 0, sizeof(g_task));
    strncpy(g_name, "---", 3);  g_name[3] = '\0';
    g_score = 0;  g_timer = 0;
    g_has_res = false;
    score_chg_sign = 0;  score_chg_ms = 0;

    visible_count = 4;
    boxes_dirty = false;
    status_suppressed = false;

    d_task = d_stat = d_res = true;
    full_redraw = true;
}

void game_ui_update(int joy_x, int joy_y) {
    // normalise joystick to -1..+1
    float rx = (float)(joy_x - JOY_CENTER) / (float)JOY_CENTER;
    float ry = (float)(joy_y - JOY_CENTER) / (float)JOY_CENTER;
    float dz = (float)JOY_DEADZONE / (float)JOY_CENTER;

    float vx = 0, vy = 0;
    if (fabsf(rx) > dz) {
        float s = (rx > 0) ? 1.0f : -1.0f;
        float a = (fabsf(rx) - dz) / (1.0f - dz);   // 0..1
        vx = s * a * SPR_SPEED;
    }
    if (fabsf(ry) > dz) {
        float s = (ry > 0) ? 1.0f : -1.0f;
        float a = (fabsf(ry) - dz) / (1.0f - dz);
        vy = s * a * SPR_SPEED;
    }

    spr_x += vx;
    spr_y += vy;
    if (spr_x < SPR_MIN_X) spr_x = SPR_MIN_X;
    if (spr_x > SPR_MAX_X) spr_x = SPR_MAX_X;
    if (spr_y < SPR_MIN_Y) spr_y = SPR_MIN_Y;
    if (spr_y > SPR_MAX_Y) spr_y = SPR_MAX_Y;

    spr_moving = (fabsf(vx) > 0.5f || fabsf(vy) > 0.5f);

    // walk animation
    if (spr_moving) {
        unsigned long now = millis();
        if (now - walk_ms > 150) { walk_fr = (walk_fr + 1) % 2; walk_ms = now; }
    }

    // selection = quadrant the sprite is in
    int ns = nearest_box();
    if (ns != sel) { old_sel = sel; sel = ns; sel_dirty = true; }
}

void game_ui_set_task(const char* t) {
    strncpy(g_task, t, sizeof(g_task) - 1);
    g_task[sizeof(g_task) - 1] = '\0';
    d_task = true;
}

void game_ui_set_result(const char* v, int st, bool c, int d) {
    strncpy(g_rv, v, sizeof(g_rv) - 1);
    g_rv[sizeof(g_rv) - 1] = '\0';
    g_rs = st;  g_rc = c;  g_rd = d;
    g_has_res = true;
    d_res = true;
}

void game_ui_set_score(int s) {
    if (s != g_score) {
        score_chg_sign = (s > g_score) ? 1 : -1;
        score_chg_ms = millis();
        g_score = s;
        d_stat = true;
    }
}

void game_ui_set_timer(int s) {
    if (s != g_timer) { g_timer = s; d_stat = true; }
}

void game_ui_set_name(const char* n) {
    strncpy(g_name, n, 3);  g_name[3] = '\0';
    d_stat = true;
}

void game_ui_set_visible_boxes(int n) {
    int clamped = (n < 1) ? 1 : (n > 4) ? 4 : n;
    if (clamped == visible_count) return;
    visible_count = clamped;
    boxes_dirty = true;
}

void game_ui_suppress_status(bool s) {
    status_suppressed = s;
}

int game_ui_get_selected_index() { return sel; }

const char* game_ui_get_selected_verb() {
    return (sel >= 0 && sel < 4) ? verb_full[sel] : "GET";
}

// ── Main draw (called every frame ≈ 30 fps) ─────────────────────────

void game_ui_draw() {
    TFT_eSPI& t = display_get_tft();

    // ── 1. Full redraw (first frame or reset) ──
    if (full_redraw) {
        t.fillScreen(TFT_BLACK);
        draw_dividers(t);
        for (int i = 0; i < visible_count; i++) draw_box(t, i, i == sel);
        draw_spr(t, spr_x, spr_y);
        drw_x = spr_x;  drw_y = spr_y;  drw_walk = walk_fr;
        full_redraw = false;
        sel_dirty = false;
        boxes_dirty = false;
        d_stat = d_task = d_res = true;
    }

    // ── 1b. Newly visible boxes ──
    if (boxes_dirty) {
        for (int i = 0; i < visible_count; i++) draw_box(t, i, i == sel);
        boxes_dirty = false;
    }

    // ── 2. Score flash timeout (check before status draw) ──
    if (score_chg_sign != 0 && millis() - score_chg_ms > 400) {
        score_chg_sign = 0;
        d_stat = true;
    }

    // ── 3. Status bar (skipped when tutorial banner is active) ──
    if (d_stat) { if (!status_suppressed) draw_status(t); d_stat = false; }

    // ── 4. Task text ──
    if (d_task) { draw_task_area(t); d_task = false; }

    // ── 5. Sprite movement ──
    float ddx = spr_x - drw_x, ddy = spr_y - drw_y;
    bool need_spr = (ddx * ddx + ddy * ddy > 0.25f) ||
                    (spr_moving && walk_fr != drw_walk);

    if (need_spr) {
        // erase old position
        erase_spr(t, drw_x, drw_y);
        // if selection also changed this frame, redraw departed box unselected
        if (sel_dirty && old_sel >= 0 && old_sel < visible_count)
            draw_box(t, old_sel, false);
        // repair any box the erase may have damaged
        for (int i = 0; i < visible_count; i++) {
            if (spr_overlaps_box(drw_x, drw_y, i))
                draw_box(t, i, i == sel);
        }
        // ensure new selected box has highlight
        if (sel_dirty && sel >= 0 && sel < visible_count)
            draw_box(t, sel, true);
        // draw sprite at new position (on top of everything)
        draw_spr(t, spr_x, spr_y);
        drw_x = spr_x;  drw_y = spr_y;  drw_walk = walk_fr;
        sel_dirty = false;
    }

    // ── 6. Box selection change (when sprite didn't move) ──
    if (sel_dirty) {
        if (old_sel >= 0 && old_sel < visible_count) draw_box(t, old_sel, false);
        if (sel >= 0 && sel < visible_count)          draw_box(t, sel, true);
        // if sprite overlaps redrawn box, put sprite back on top
        if ((sel >= 0 && sel < visible_count && spr_overlaps_box(spr_x, spr_y, sel)) ||
            (old_sel >= 0 && old_sel < visible_count && spr_overlaps_box(spr_x, spr_y, old_sel)))
            draw_spr(t, spr_x, spr_y);
        sel_dirty = false;
    }

    // ── 7. Response row ──
    if (d_res) { draw_result(t); d_res = false; }
}
