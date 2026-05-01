#include<Arduino.h>
#include "ble/ble.h"
#include "eeprom/eeprom.h"
#include "modem/modem.h"
#include "mqtt/mqtt.h"
#include "sensor/sensor.h"
#include "config.h"
#include "indicator/indicator.h"

bool setup_info[] = {false, false, false, false, false, false};
//                   BLE    EEPROM INDI   MODEM  MQTT   SENSOR
volatile unsigned long stepCount = 0;
String mode = "live";

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

void setup_modem();
void setup_ble();
void setup_sensor();
void setup_eeprom();
void setup_mqtt();
void setup_indicator();
void handleBleCommand(const String& command);
void set_mode(String modeStr);


void setup(){
    
    setup_indicator();
    setup_sensor();
    setup_modem();
    setup_ble();
    setup_eeprom();
    setup_mqtt();
}

void loop(){
    BLE::getInstance().notify("Hello from esp32");
    delay(100);
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

void setup_eeprom(){
    eeprom::getInstance().init();
    updateLED(BATTERY_LED, PURPLE, 50);
}

void setup_mqtt(){
    updateLED(NETWORK_LED, YELLOW, 64);
    mqtt_set_broker(MQTT_BROKER, MQTT_TOKEN);
    mqtt_init();
    delay(1000);
    SerialMon.println("[MQTT] Initialised");
    updateLED(NETWORK_LED, BLUE, 64);
}

void setup_indicator(){
    initLED();
    setup_info[INDICATOR_SETUP_ID] = true;
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