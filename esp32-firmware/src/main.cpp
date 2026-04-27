#include <Arduino.h>
#include "display.h"
#include "input.h"

// Display area for live input status (bottom portion of screen)
static const int STATUS_Y = 150;

static const char* dir_name(JoyDirection d) {
    switch (d) {
        case JOY_LEFT:  return "LEFT ";
        case JOY_RIGHT: return "RIGHT";
        case JOY_UP:    return "UP   ";
        case JOY_DOWN:  return "DOWN ";
        default:        return "NONE ";
    }
}

static JoyDirection last_dir = JOY_NONE;
static bool need_redraw = true;

void draw_status(JoyDirection dir, int raw_x, int raw_y) {
    TFT_eSPI& tft = display_get_tft();

    // Clear the status area
    tft.fillRect(0, STATUS_Y, 320, 90, TFT_BLACK);

    // Joystick direction — large text
    tft.setTextDatum(TL_DATUM);
    display_text(10, STATUS_Y, "JOY:", TFT_CYAN, 2);

    uint16_t dir_color = (dir == JOY_NONE) ? TFT_DARKGREY : TFT_GREEN;
    display_text(80, STATUS_Y, dir_name(dir), dir_color, 2);

    // Raw analog values
    char buf[32];
    snprintf(buf, sizeof(buf), "X:%4d  Y:%4d", raw_x, raw_y);
    display_text(10, STATUS_Y + 25, buf, TFT_DARKGREY, 1);

    // Button states — show labels, will highlight when pressed
    display_text(10, STATUS_Y + 45, "BTN:", TFT_CYAN, 2);
    display_text(80, STATUS_Y + 45, "---", TFT_DARKGREY, 2);

    display_text(10, STATUS_Y + 70, "JOY_BTN:", TFT_CYAN, 2);
    display_text(120, STATUS_Y + 70, "---", TFT_DARKGREY, 2);
}

void flash_button(const char* label, int y, int x_offset) {
    display_text(x_offset, y, "PRESSED!", TFT_YELLOW, 2);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("=== APIgame Hardware Test ===");

    display_init();
    input_init();
    display_boot_screen();

    // Hold boot screen for 1.5s so it's visible
    delay(1500);

    // Draw initial status
    draw_status(JOY_NONE, JOY_CENTER, JOY_CENTER);

    Serial.println("Ready. Move joystick or press buttons.");
}

void loop() {
    // Read inputs
    JoyDirection dir = joy_direction();
    int raw_x = joy_raw_x();
    int raw_y = joy_raw_y();
    bool btn = button_pressed();
    bool joy_btn = joy_button_pressed();

    // Print joystick direction to serial (only on change)
    if (dir != last_dir) {
        Serial.print("JOY: ");
        Serial.println(dir_name(dir));
        last_dir = dir;
        need_redraw = true;
    }

    // Print button presses to serial
    if (btn) {
        Serial.println("BTN: pressed");
        need_redraw = true;
    }
    if (joy_btn) {
        Serial.println("JOY_BTN: pressed");
        need_redraw = true;
    }

    // Update display (only when something changed)
    if (need_redraw) {
        draw_status(dir, raw_x, raw_y);

        if (btn) {
            flash_button("BTN", STATUS_Y + 45, 80);
        }
        if (joy_btn) {
            flash_button("JOY_BTN", STATUS_Y + 70, 120);
        }

        need_redraw = false;
    }

    delay(50);  // 20Hz polling
}
