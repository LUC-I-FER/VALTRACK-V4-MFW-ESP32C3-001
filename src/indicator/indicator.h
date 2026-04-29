#pragma once
#include <Arduino.h>

void indicator_init();
void indicator_set_color(uint8_t r, uint8_t g, uint8_t b, int led_num);
void indicator_blink(uint8_t r, uint8_t g, uint8_t b, int times);