#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// Pin assignments from PIN_REFERNCE.md
#define JOY_VRX    34   // Joystick X-axis (analog)
#define JOY_VRY    35   // Joystick Y-axis (analog)
#define JOY_SW     13   // Joystick button (digital, active LOW)
#define BUTTON_PIN 12   // Action button (digital, active LOW)

// Joystick config
#define JOY_CENTER   2048
#define JOY_DEADZONE 500

enum JoyDirection {
    JOY_NONE,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_UP,
    JOY_DOWN
};

void input_init();
JoyDirection joy_direction();
bool button_pressed();
bool joy_button_pressed();

// Raw analog reads (for debugging)
int joy_raw_x();
int joy_raw_y();

#endif
