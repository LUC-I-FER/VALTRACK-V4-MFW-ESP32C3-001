// SPDX-License-Identifier: GPL-2.0-or-later
#include <TinyGsmClient.h>
#include <HardwareSerial.h>
#include <Arduino.h>

#ifndef MODEM_H
#define MODEM_H

// Modem params
#define MODEM_PWRKEY 7
#define MODEM_GSMKEY 10
#define MODEM_RX 1
#define MODEM_TX 0

//MQTT PARAMS
// format tcp://<broken server url>:<port>
#define MQTT_SERVER_NAME "tcp://mqtt.flespi.io:1883"
#define MQTT_TOKEN "SRSPTxVFFDM1d2ycpqlvlEJ0wpCUn1qfSJ41yHlVv3x5B4nUXJ7DudlzADZekQHv"
#define MQTT_DEV_NAME "PET"
#define MODEM_APN "www"

#define MODEM_STATE_READY (1 << 1)
#define MODEM_GNSS_READY (1 << 2)
#define MODEM_MQTT_READY (1 << 3)

bool modem_init(void);
void modem_deinit(void);
void modem_mqtt_start(void);
int  modem_mqtt_send(const uint8_t idx, const String topic, const String Payload);
String modem_mqtt_read_payload(const uint8_t idx);
int modem_get_status();
void modem_battery_get_status(float *vbat);
void modem_gnss_power();
void modem_get_gps(float *lat, float *lon);
void modem_set_gsm_state(uint8_t enable, int delay_ms);
#endif
