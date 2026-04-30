#include "indicator.h"
#include <Arduino.h>

#define GPIO_LED_SIGNAL 8

Adafruit_NeoPixel pixels(3, GPIO_LED_SIGNAL, NEO_GRB + NEO_KHZ800);

void initLED() {
  pixels.begin();
  updateLED(BATTERY_LED, RED);
  updateLED(NETWORK_LED, GREEN);
  updateLED(LOCATION_LED, BLUE);
}

void updateLED(int ledIndex, int color, int brightness) {
  switch(color) {
    case RED:   pixels.setPixelColor(ledIndex, pixels.Color(brightness, 0, 0)); break;
    case GREEN: pixels.setPixelColor(ledIndex, pixels.Color(0, brightness, 0)); break;
    case BLUE:  pixels.setPixelColor(ledIndex, pixels.Color(0, 0, brightness)); break;
    default:    pixels.setPixelColor(ledIndex, pixels.Color(0, 0, 0)); break;
  }
  pixels.show();
}