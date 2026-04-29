#ifndef MODEM_H
#define MODEM_H

#include<Arduino.h>
#include <TinyGsmClient.h>

#define MODEM_PWRKEY 7
#define MODEM_GSMKEY 10
#define MODEM_RX 1
#define MODEM_TX 0

#define MODEM_APN "vi"

#define MQTT_HOST "mqtt.flespi.io"
#define MQTT_PORT 1883
#define MQTT_TOKEN "SRSPTxVFFDM1d2ycpqlvlEJ0wpCUn1qfSJ41yHlVv3x5B4nUXJ7DudlzADZekQHv"
#define MQTT_CLIENT_ID "PET"

#define MODEM_SIM_PIN "0000"   // replace with your actual PIN

typedef enum {
    MODEM_STATE_OFF        = 0,
    MODEM_STATE_POWERED    = (1 << 0),
    MODEM_STATE_READY      = (1 << 1),
    MODEM_STATE_NETWORK    = (1 << 2),
    MODEM_STATE_GPRS       = (1 << 3),
    MODEM_STATE_MQTT       = (1 << 4),
    MODEM_STATE_GNSS       = (1 << 5)
} modem_state_t;

// Core -->
bool modem_init();
void modem_deinit();
bool modem_is_ready();

// Network -->
bool modem_wait_for_network(uint32_t timeout = 30000);
bool modem_connect_gprs(uint32_t timeout = 30000);
void modem_disconnect_gprs();

// MQTT -->
bool modem_mqtt_start();
bool modem_mqtt_publish(const String& topic, const String& payload);
String modem_mqtt_read();

// GNSS -->
bool modem_gnss_enable();
bool modem_get_location(float* lat, float* lon);

// Utils -->
float modem_get_battery();

#endif