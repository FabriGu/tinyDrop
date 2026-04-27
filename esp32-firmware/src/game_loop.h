#ifndef GAME_LOOP_H
#define GAME_LOOP_H

#include <Arduino.h>

void        game_loop_start(const char* round_id);   // start 60s countdown, score = 0
bool        game_loop_tick();                         // true when time expires
void        game_loop_on_result(const char* json);    // parse result, update score + display
int         game_loop_get_time_left();
bool        game_loop_is_active();
int         game_loop_get_score();
const char* game_loop_get_round_id();

#endif
