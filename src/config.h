#ifndef CONFIG_H
#define CONFIG_H

#define APN "jionet"
#define USER ""
#define PASS ""

#define BLE_SETUP_ID       0
#define EEPROM_SETUP_ID    1
#define INDICATOR_SETUP_ID 2
#define MODEM_SETUP_ID     3
#define MQTT_SETUP_ID      4
#define SENSOR_SETUP_ID    5

#define MQTT_BROKER    "tcp://mqtt.flespi.io:1883"
#define MQTT_TOKEN     "Hz8ZN1ZvlOH5yZcvgKJyaC3dOrdqILdsMQkdmFUKkqUabNcVIQprNgf7Fd1Vb3ZV"
#define MQTT_CLIENT_ID "ESP32C3_GSM"

#define LIVE_MODE   0
#define SAFE_MODE   1
#define NORMAL_MODE 2
#define BLEGPS_MODE 3

/*
snprintf(payload, sizeof(payload),
           "{"
           "\"SIM\":\"%s\","
           "\"MACID\":\"%s\","
           "\"Latitude\":%.6f,"
           "\"Longitude\":%.6f,"
           "\"Battery\":%.2f,"
           "\"StepCount\":%lu,"  // Use %lu for unsigned long
           "\"WiFi\":%d,"
           "\"Signal\":%d,"
           "\"SOS\":%d,"
           "\"Reset\":%d,"
           "\"BLE\":%d,"
           "\"BreedFactor\":%d,"     // Comma added here
           "\"Mode\":\"%s\""         // normal, safe, live
           "}",
           simNumber.c_str(),
           bleMac.c_str(),
           latitude,
           longitude,
           batteryLevel,
           stepCount,
           gprsStatus,
           signalStrength,
           sosFlag,
           resetFlag,
           bleActive,
           BreedFactor,
           mode.c_str()); 
*/

#endif