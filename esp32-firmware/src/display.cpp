#include "display.h"

static TFT_eSPI tft = TFT_eSPI();

void display_init() {
    tft.init();
    tft.setRotation(1);  // Landscape 320x240
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    Serial.println("Display initialized (320x240 landscape)");
}

void display_boot_screen() {
    tft.fillScreen(TFT_BLACK);

    // "APIgame" title — large, centered
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(4);
    tft.setCursor(40, 40);
    tft.print("APIgame");

    // Version text
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(80, 100);
    tft.print("v0.1 - Hardware Test");

    // Bottom message
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(50, 200);
    tft.print("Hardware test...");
}

void display_text(int x, int y, const char* text, uint16_t color, uint8_t size) {
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

void display_clear() {
    tft.fillScreen(TFT_BLACK);
}

TFT_eSPI& display_get_tft() {
    return tft;
}
