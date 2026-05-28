#include "indicator.h"
#include <Arduino.h>

#define GPIO_LED_SIGNAL 8

Adafruit_NeoPixel pixels(3, GPIO_LED_SIGNAL, NEO_GRB + NEO_KHZ800);

void initLED() {
  pixels.begin();
  updateLED(BATTERY_LED,  GREEN, 64);
  updateLED(NETWORK_LED,  RED, 64);
  updateLED(LOCATION_LED, RED, 64);
}

void updateLED(int ledIndex, int color, int brightness) {
  switch(color) {
    case RED:      pixels.setPixelColor(ledIndex, pixels.Color(brightness, 0         , 0         )); break;
    case GREEN:    pixels.setPixelColor(ledIndex, pixels.Color(0         , brightness, 0         )); break;
    case BLUE:     pixels.setPixelColor(ledIndex, pixels.Color(0         , 0         , brightness)); break;
    case PURPLE:   pixels.setPixelColor(ledIndex, pixels.Color(brightness, 0         , brightness)); break;
    case YELLOW:   pixels.setPixelColor(ledIndex, pixels.Color(brightness, brightness, 0         )); break;
    case WHITE:    pixels.setPixelColor(ledIndex, pixels.Color(brightness, brightness, brightness)); break;
    default:       pixels.setPixelColor(ledIndex, pixels.Color(0         , 0         , 0         )); break;
  }
  pixels.show();
}