#include "input.h"

// Debounce state
static bool _btn_last = false;
static unsigned long _btn_last_time = 0;

static bool _joy_btn_last = false;
static unsigned long _joy_btn_last_time = 0;

static const unsigned long DEBOUNCE_MS = 50;

void input_init() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(JOY_SW, INPUT_PULLUP);
    // GPIO 34, 35 are input-only pins on ESP32 — no pinMode needed,
    // but explicit for clarity
    pinMode(JOY_VRX, INPUT);
    pinMode(JOY_VRY, INPUT);

    Serial.println("Input initialized (JOY: 34/35/13, BTN: 12)");
}

JoyDirection joy_direction() {
    int x = analogRead(JOY_VRX);
    int y = analogRead(JOY_VRY);

    // Check X-axis first (horizontal has priority for game UI navigation)
    if (x < JOY_CENTER - JOY_DEADZONE) return JOY_LEFT;
    if (x > JOY_CENTER + JOY_DEADZONE) return JOY_RIGHT;

    // Then Y-axis
    if (y < JOY_CENTER - JOY_DEADZONE) return JOY_UP;
    if (y > JOY_CENTER + JOY_DEADZONE) return JOY_DOWN;

    return JOY_NONE;
}

bool button_pressed() {
    bool current = (digitalRead(BUTTON_PIN) == LOW);  // Active LOW with pullup
    unsigned long now = millis();

    // Rising edge detection with debounce
    if (current && !_btn_last && (now - _btn_last_time > DEBOUNCE_MS)) {
        _btn_last = current;
        _btn_last_time = now;
        return true;
    }

    _btn_last = current;
    return false;
}

bool joy_button_pressed() {
    bool current = (digitalRead(JOY_SW) == LOW);  // Active LOW with pullup
    unsigned long now = millis();

    // Rising edge detection with debounce
    if (current && !_joy_btn_last && (now - _joy_btn_last_time > DEBOUNCE_MS)) {
        _joy_btn_last = current;
        _joy_btn_last_time = now;
        return true;
    }

    _joy_btn_last = current;
    return false;
}

int joy_raw_x() {
    return analogRead(JOY_VRX);
}

int joy_raw_y() {
    return analogRead(JOY_VRY);
}
