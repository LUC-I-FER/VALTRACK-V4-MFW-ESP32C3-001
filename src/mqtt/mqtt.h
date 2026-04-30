#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

void mqtt_init();
int mqtt_publish(const String& topic, const String& payload);
String mqtt_read_incoming();
void mqtt_set_broker(const String& broker_url, const String& token);

#endif