#ifndef GAME_UI_H
#define GAME_UI_H

#include <Arduino.h>

void game_ui_init();
void game_ui_draw();
void game_ui_update(int joy_x, int joy_y);   // raw analog values (0-4095)
void game_ui_set_task(const char* text);
void game_ui_set_result(const char* verb, int status, bool correct, int delta);
void game_ui_set_score(int score);
void game_ui_set_timer(int seconds);
void game_ui_set_name(const char* name);
int  game_ui_get_selected_index();            // 0=GET 1=POST 2=PUT 3=DEL
const char* game_ui_get_selected_verb();      // full name for MQTT

#endif
