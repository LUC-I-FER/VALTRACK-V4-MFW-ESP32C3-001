#ifndef MODEM_H
#define MODEM_H

#include <TinyGsmClient.h>

#define SerialMon Serial

extern TinyGsm modem;
extern TinyGsmClient client;

void initUARTs();
void initGPIO();
void enableGSM();
void disableGSM();
void initGSMpower();
bool connectGPRS(const char* apn, const char* user, const char* pass);
bool isGPRSConnected();
void restartModem();
bool waitForNetwork(unsigned long timeout_ms = 30000L);

// New GPS & battery
void modem_gnss_power();
void modem_get_gps(float *lat, float *lon);
void modem_battery_get_status(float *vbat);

#endif