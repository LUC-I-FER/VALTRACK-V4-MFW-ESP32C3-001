#include "indicator.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN 8
#define NUMPIXELS 3

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

void indicator_init() {
    pixels.begin();
    pixels.setBrightness(50);
    pixels.clear();
    pixels.show();
}

void indicator_set_color(uint8_t r, uint8_t g, uint8_t b, int led_num) {
    pixels.setPixelColor(led_num, pixels.Color(r, g, b)); 
    pixels.show();
}
