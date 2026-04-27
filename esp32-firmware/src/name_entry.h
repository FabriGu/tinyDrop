#ifndef NAME_ENTRY_H
#define NAME_ENTRY_H

#include <Arduino.h>
#include "input.h"

void name_entry_init();
bool name_entry_update(JoyDirection dir, bool button);  // returns true when name complete
void name_entry_draw();
String name_entry_get_name();

#endif
