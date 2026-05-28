#include<Arduino.h>
#include "ble/ble.h"
#include "eeprom/eeprom.h"
#include "modem/modem.h"
#include "mqtt/mqtt.h"
#include "sensor/sensor.h"
#include "config.h"
#include "indicator/indicator.h"
#include <PubSubClient.h>
#include <TinyGsmClient.h>

PubSubClient  mqtt(client);

bool setup_info[] = {false, false, false, false, false, false};
//                   BLE    EEPROM INDI   MODEM  MQTT   SENSOR
String mode = "live";
int stepCount = 0;
String simNumber   = "+910000000000";
String bleMac;
float latitude     = 0.0;
float longitude    = 0.0;
float batterylevel = 0;
int BreedFactor    = 4;
int gprsStatus     = 0;
int sosFlag        = 0;
int resetFlag      = 0;
int bleActive      = 0;
int signalStrength = 0;
String topic       = "";

char payload[256] = "";

void setup_modem();
void setup_ble();
void setup_sensor();
void setup_eeprom();
void setup_mqtt();
void setup_indicator();
void handleBleCommand(const String& command);
void set_mode(String modeStr);
void setup_GSM_MQTT(void* pr);

void update_values();
void update_step_count_globally(void* pr);
void sendData();


void setup(){
    
    setup_indicator();
    delay(1000);
    setup_sensor();
    setup_modem();
    setup_eeprom();
    setup_ble();
    setup_mqtt();

    // xTaskCreate(setup_GSM_MQTT,             "modemTask",    8192, NULL, 1, NULL);
    // xTaskCreate(update_step_count_globally, "updateStep",   8192, NULL, 2, NULL);
    // xTaskCreate(update_values,              "updateValues", 8192, NULL, 3, NULL);
}

void loop(){
    // main idea
    // 1. check for incoming MQTT response
    // 2. send data
    // 3. store data
    // sendData();

    // delay(1000);

    // if (setup_info[MODEM_SETUP_ID] && setup_info[MQTT_SETUP_ID]){
    mqtt_publish("esp32c3/test", "hello");
    // }

    // if (setup_info[BLE_SETUP_ID]){
    //     BLE::getInstance().notify(payload);
    // }

    update_values();
    
    delay(1000);
}

// helper functions to initialize the gsm modem
void setup_modem(){
    initUARTs();
    initGPIO();

    enableGSM();
    delay(100);
    initGSMpower();
    delay(1000);

    SerialMon.println("[MODEM] Waiting for modem to repsond");
    delay(3000);

    restartModem();
    delay(1000);

    updateLED(NETWORK_LED, BLUE, 50);

    delay(1000);

    if (!waitForNetwork(30000L)){
        SerialMon.println("[MODEM] Error No Network..");
        updateLED(NETWORK_LED, YELLOW, 50);
    } else {
        SerialMon.println("[MODEM] Network Register..");
        updateLED(NETWORK_LED, GREEN, 50);
    }

    updateLED(LOCATION_LED, BLUE, 50);
    delay(1000);

    if (!connectGPRS(APN, USER, PASS)){
        SerialMon.println("[MODEM] Error GPRS Connection failed..");
        updateLED(LOCATION_LED, RED, 50);
    } else{
        SerialMon.println("[MODEM] GPRS Connected successfully...");
        updateLED(LOCATION_LED, GREEN, 50);
    }

    setup_info[MODEM_SETUP_ID] = true;
}

void setup_ble(){
    BLE::getInstance().init("ESP32_PAW", false);
    BLE::getInstance().setCommandCallback(handleBleCommand);
    BLE::getInstance().startAdvertising();

    setup_info[BLE_SETUP_ID] = true;
}

void setup_sensor(){
    if (sensor::getInstance().init(5,6)){
        Serial.println("[SENSOR] Init success...");
        updateLED(BATTERY_LED, BLUE, 50);
        setup_info[SENSOR_SETUP_ID] = true;
    }

}

void update_step_count_globally(void* pr){
    if (!setup_info[SENSOR_SETUP_ID]){
        return;
    }

    // dp something
}

void setup_eeprom(){
    eeprom::getInstance().init();
    // updateLED(BATTERY_LED, PURPLE, 50); 
}

void setup_mqtt(){
    updateLED(NETWORK_LED, YELLOW, 64);
    delay(1000);
    updateLED(NETWORK_LED, BLUE , 0);
    // updateLED(NETWORK_LED)
    mqtt_init();
    mqtt_set_broker(MQTT_BROKER, MQTT_TOKEN);
    delay(1000);
    // SerialMon.println("[MQTT] Initialised");
    updateLED(NETWORK_LED, BLUE, 64);
}

void setup_indicator(){
    initLED();
    setup_info[INDICATOR_SETUP_ID] = true;
}

void setup_GSM_MQTT(void* pv){
    setup_modem();
    vTaskDelay(pdMS_TO_TICKS(500));
    setup_mqtt();
    vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelete(NULL);
}

void handleBleCommand(const String& command) {
    SerialMon.print("[BLE] Command received: ");
    SerialMon.println(command);

    if (command == "reset") {
        stepCount = 0;
        SerialMon.print("[BLE] Step count reset to 0");
    } else if (command == "clear") {
        stepCount = 0;
        BreedFactor = 1;
        set_mode("normal");
        SerialMon.println("[BLE] All the settings cleared to default.");
    } else {
        int commaIndex = command.indexOf(',');
        if (commaIndex > 0){
            String bfStr = command.substring(0, commaIndex);
            String modeStr = command.substring(commaIndex + 1);
            bfStr.trim();
            modeStr.trim();

            int val = bfStr.toInt();
            if( val >= 1 && val <= 10){
                BreedFactor = (int) val;
                SerialMon.print("[BLE] BreedFactor set to: ");
                SerialMon.println(BreedFactor);
            }
            set_mode(modeStr);
        } else {
            Serial.println("[BLE] Invalid input, expected format: <number>,<mode>");
        }
    }
}

void set_mode(String modeStr){
    modeStr.toLowerCase();
    if (modeStr == "live") {mode = modeStr;}
    else if (modeStr == "safe") {mode = modeStr;}
    else if (modeStr == "normal") {mode = modeStr;}
    else if (modeStr == "blegps") {mode = modeStr;}
    else {SerialMon.println("[SYSTEM] Error only : live safe normal blegps (mode supported)");}
}

void update_values(){
    // main aim of thsis function is to update all the parameters we need to send
    if (topic == ""){
        bleMac.toUpperCase();
        topic = "pets/" + bleMac + "/data";
    }

    modem_get_gps(&latitude , &longitude);
    modem_battery_get_status(&batterylevel);
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
           batterylevel,
           stepCount,
           gprsStatus,
           signalStrength,
           sosFlag,
           resetFlag,
           bleActive,
           BreedFactor,
           mode.c_str());
}

void sendData(){


    if (BLE::getInstance().isConnected()){
            BLE::getInstance().notify(payload);
        
    } else if (mode == "live"){
        // bleActivate = 0;
        if (payload){
            if (setup_info[MODEM_SETUP_ID] && setup_info[MQTT_SETUP_ID]){
            mqtt_publish(topic, payload);
            } else {
                SerialMon.println("[SYSTEM] Check MODEM MQTT");
            }
        }
    }
}