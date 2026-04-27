#include "display.h"

static TFT_eSPI tft = TFT_eSPI();

void display_init() {
    tft.init();
    tft.setRotation(1);  // Landscape 320x240
    tft.fillScreen(TFT_BLACK);
    Serial.println("Display initialized (320x240 landscape)");
}

void display_boot_screen() {
    tft.fillScreen(TFT_BLACK);

    // "APIgame" title — large, centered
    tft.setTextDatum(TC_DATUM);  // Top-center
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(4);
    tft.drawString("APIgame", 160, 40);

    // Version text
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("v0.1 — Hardware Test", 160, 100);

    // Bottom message
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Hardware test...", 160, 200);

    tft.setTextDatum(TL_DATUM);  // Reset to top-left
}

void display_text(int x, int y, const char* text, uint16_t color, uint8_t size) {
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(size);
    tft.drawString(text, x, y);
}

void display_clear() {
    tft.fillScreen(TFT_BLACK);
}

TFT_eSPI& display_get_tft() {
    return tft;
}
