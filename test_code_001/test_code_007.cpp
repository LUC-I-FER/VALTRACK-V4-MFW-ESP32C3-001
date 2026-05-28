#include <Arduino.h>
#include "modem/ModemManager.h"
#include "mqtt/MqttManager.h"

// GPRS credentials
const char apn[] = "vi";
const char gprsUser[] = "";
const char gprsPass[] = "";

// MQTT Configuration - Change these for your broker
const char* MQTT_BROKER = "mqtt.flespi.io";      // or "test.mosquitto.org", "broker.emqx.io", etc.
const int MQTT_PORT = 1883;                       // or 8883 for TLS
const char* MQTT_USER = "HTLj3BNXh2SV6RPWEW9fMJE5h3lyL2EWTT8PcDpqaBk5blNn2TU1zczND8BRoV8P";                       // Leave empty if no auth
const char* MQTT_PASS = "HTLj3BNXh2SV6RPWEW9fMJE5h3lyL2EWTT8PcDpqaBk5blNn2TU1zczND8BRoV8P";                       // Leave empty if no auth
const char* MQTT_CLIENT_ID = "";                  // Empty = auto-generate

// Topics
const char* TOPIC_TELEMETRY = "device/telemetry";
const char* TOPIC_COMMAND = "device/command";
const char* TOPIC_STATUS = "device/status";

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 30000;

// MQTT Message Callback
void onMqttMessage(String topic, String payload) {
    Serial.printf("Command: %s = %s\n", topic.c_str(), payload.c_str());
    
    if (topic == TOPIC_COMMAND) {
        if (payload == "STATUS") {
            MqttManager::getInstance().publish(TOPIC_STATUS, "Online");
        } else if (payload == "RESTART") {
            ESP.restart();
        }
    }
}

// MQTT Connection Callback
void onMqttConnection(bool connected) {
    if (connected) {
        Serial.println("✓ MQTT Connected!");
        MqttManager::getInstance().subscribe(TOPIC_COMMAND);
        MqttManager::getInstance().publish(TOPIC_STATUS, "Online", true);
    } else {
        Serial.println("✗ MQTT Disconnected!");
    }
}

void setup() {
    delay(5000);
    Serial.begin(115200);
    Serial.println("\n=== Device Starting ===");
    
    // Initialize Modem
    ModemManager& modem = ModemManager::getInstance();
    
    if (!modem.begin(115200)) {
        Serial.println("Modem begin failed!");
        return;
    }
    
    modem.enableGSM();
    modem.powerOnModem();
    
    if (!modem.initModem()) {
        Serial.println("Modem init failed!");
        return;
    }
    
    if (!modem.waitForNetwork(30000)) {
        Serial.println("Network timeout!");
        return;
    }
    
    Serial.printf("Signal: %d\n", modem.getSignalStrength());
    
    if (!modem.connectGPRS(apn, gprsUser, gprsPass)) {
        Serial.println("GPRS failed!");
        return;
    }
    
    Serial.printf("IP: %s\n", modem.getLocalIP().c_str());
    
    // Initialize MQTT
    MqttManager& mqtt = MqttManager::getInstance();
    
    mqtt.begin(*modem.getClient());
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    
    if (strlen(MQTT_USER) > 0) {
        mqtt.setCredentials(MQTT_USER, MQTT_PASS);
    }
    
    if (strlen(MQTT_CLIENT_ID) > 0) {
        mqtt.setClientId(MQTT_CLIENT_ID);
    }
    
    mqtt.onMessage(onMqttMessage);
    mqtt.onConnection(onMqttConnection);
    mqtt.setAutoReconnect(true);
    
    // Connect
    if (mqtt.connect()) {
        Serial.println("MQTT Ready!");
    } else {
        Serial.printf("MQTT Failed! State: %s\n", mqtt.getStateString().c_str());
    }
}

void loop() {
    MqttManager& mqtt = MqttManager::getInstance();
    ModemManager& modem = ModemManager::getInstance();
    
    // Keep MQTT alive
    mqtt.loop();
    
    // Periodic publishing
    unsigned long now = millis();
    if (now - lastPublish >= PUBLISH_INTERVAL) {
        lastPublish = now;
        
        if (mqtt.isConnected()) {
            // Create JSON payload
            JsonDocument doc;
            doc["device"] = mqtt.getClientId();
            doc["uptime"] = now / 1000;
            doc["signal"] = modem.getSignalStrength();
            doc["heap"] = ESP.getFreeHeap();
            
            // Add battery if available
            float battery = modem.getBatteryVoltage();
            if (battery > 0) {
                doc["battery"] = battery;
            }
            
            // Add GPS if available
            GPSData gps;
            if (modem.getGPSInfo(gps) && gps.valid) {
                doc["lat"] = gps.latitude;
                doc["lng"] = gps.longitude;
                doc["alt"] = gps.altitude;
                doc["speed"] = gps.speed;
            }
            
            // Publish
            mqtt.publish(TOPIC_TELEMETRY, doc);
        }
    }
    
    delay(100);
}