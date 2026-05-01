#ifndef INDICATOR_H
#define INDICATOR_H

#include <Adafruit_NeoPixel.h>

extern Adafruit_NeoPixel pixels;

// LED indices
#define BATTERY_LED  0
#define NETWORK_LED  1
#define LOCATION_LED 2


// Colors
#define RED    0
#define GREEN  1
#define BLUE   2
#define PURPLE 3
#define YELLOW 4
#define WHITE  5

#define BRIGHTNESS 64

void initLED();
void updateLED(int ledIndex, int color, int brightness = BRIGHTNESS);

#endif