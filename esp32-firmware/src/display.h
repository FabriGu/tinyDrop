#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>

void display_init();
void display_boot_screen();
void display_text(int x, int y, const char* text, uint16_t color, uint8_t size);
void display_clear();

// Access the tft instance (for advanced use in other modules)
TFT_eSPI& display_get_tft();

#endif
