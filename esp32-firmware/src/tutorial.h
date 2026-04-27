#ifndef TUTORIAL_H
#define TUTORIAL_H

#include <Arduino.h>

void        tutorial_init();
int         tutorial_current_step();       // 0-3, or 4 = complete
const char* tutorial_get_prompt();         // current step's task text
const char* tutorial_get_expected_verb();  // "GET", "POST", "PUT", or "DELETE"
bool        tutorial_check_verb(const char* verb);   // true if matches expected
void        tutorial_show_hint();          // trigger wrong-verb hint animation
void        tutorial_advance();            // move to next step
void        tutorial_draw();               // render overlay — call after game_ui_draw()

#endif
