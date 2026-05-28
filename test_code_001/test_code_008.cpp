#include <Arduino.h>
#include "modem/ModemManager.h"
#include "mqtt/MqttManager.h"
#include "ble/ble.h"
#include "eeprom/eeprom.h"
#include "indicator/indicator.h"
#include "sensor/sensor.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ============================================================================
// Configuration
// ============================================================================

const char APN[] = "vi";
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

const char* DEFAULT_MQTT_BROKER = "mqtt.flespi.io";
const int DEFAULT_MQTT_PORT = 1883;
const char* DEFAULT_MQTT_TOKEN = "P7fxlJQIsighjkZBqHwGGHIK3N9tIGk2WBeIipne8YYtepmTw8aL3XuATuFtNtBk";

const char* BLE_NAME = "ESP_PAW";

// Data Update Intervals
unsigned long liveInterval = 5000;
unsigned long normalInterval = 30000;
unsigned long safeInterval = 60000;
unsigned long DATA_UPDATE_INTERVAL = 30000;

const unsigned long LED_UPDATE_INTERVAL = 2000;

// EEPROM Write Reduction
const unsigned long EEPROM_WRITE_INTERVAL = 30000;
const unsigned int STEP_COUNT_THRESHOLD = 50;
const unsigned long FORCE_WRITE_INTERVAL = 300000;

// ============================================================================
// BLE Queue Configuration
// ============================================================================
#define MAX_BLE_MSG_LEN 256
#define BLE_RESPONSE_QUEUE_SIZE 10
#define BLE_CMD_QUEUE_SIZE 10

// ============================================================================
// Global Variables
// ============================================================================

unsigned long lastDataUpdate = 0;
unsigned long lastLedUpdate = 0;

String simNumber = "";
String bleMac = "";
float latitude = 0.0, longitude = 0.0, batteryLevel = 0.0;
unsigned long stepCount = 0;
int gprsStatus = 0, signalStrength = 0, sosFlag = 0, breedFactor = 5;
String mode = "normal";

bool bleConnected = false;
const int SOS_PIN = 9;
bool lastSosState = HIGH;

// BLE Response Queue - Using char arrays to avoid heap corruption
struct BleResponse { 
    char message[MAX_BLE_MSG_LEN]; 
};
QueueHandle_t bleResponseQueue = NULL;

// BLE Command Queue - Using char arrays to avoid heap corruption
struct BleCommand { 
    char command[MAX_BLE_MSG_LEN]; 
};
QueueHandle_t bleCommandQueue = NULL;

// EEPROM Write Reduction
unsigned long lastEepromWrite = 0, lastForceWriteCheck = 0;
unsigned int pendingSteps = 0;
bool stepsPending = false;

// MQTT Configuration
struct MQTTConfig {
    String token, broker;
    int port;
    bool valid;
} mqttConfig = {"", DEFAULT_MQTT_BROKER, DEFAULT_MQTT_PORT, false};

// Dynamic MQTT Topics
String mqttPublishTopic = "";   // Topic for publishing data: "pets/{MACID}/data"
String mqttCommandTopic = "device/command";  // Topic for receiving commands

// Forward declarations
void sendDataUpdate();
void setMode(const String& newMode);
void sendBleResponse(const String& message);
void flushStepCountToEeprom(bool force = false);
void checkAndWriteEeprom();
void resetToDefaultConfig();
void updateMqttTopics();
void processBleCommands();

bool initFileSystem() {
    if (!LittleFS.begin()) {
        Serial.println("[FS] Mount failed, formatting...");
        if (!LittleFS.format() || !LittleFS.begin()) return false;
    }
    Serial.println("[FS] Ready");
    return true;
}

bool loadMQTTConfig() {
    if (!LittleFS.begin()) return false;
    
    File file = LittleFS.open("/mqtt.json", "r");
    if (!file) {
        resetToDefaultConfig();
        return false;
    }
    
    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        resetToDefaultConfig();
        file.close();
        return false;
    }
    file.close();
    
    if (doc["mqtt_token"].is<String>()) mqttConfig.token = doc["mqtt_token"].as<String>();
    if (doc["mqtt_broker"].is<String>()) mqttConfig.broker = doc["mqtt_broker"].as<String>();
    if (doc["mqtt_port"].is<int>()) mqttConfig.port = doc["mqtt_port"].as<int>();
    
    mqttConfig.valid = mqttConfig.token.length() > 0;
    if (!mqttConfig.valid) resetToDefaultConfig();
    
    return mqttConfig.valid;
}

bool saveMQTTConfig(const String& token, const String& broker, int port) {
    if (!LittleFS.begin()) return false;
    
    JsonDocument doc;
    doc["mqtt_token"] = token;
    doc["mqtt_broker"] = broker;
    doc["mqtt_port"] = port;
    
    File file = LittleFS.open("/mqtt.json", "w");
    if (!file) return false;
    
    serializeJson(doc, file);
    file.close();
    
    mqttConfig = {token, broker, port, true};
    return true;
}

void resetToDefaultConfig() {
    mqttConfig = {DEFAULT_MQTT_TOKEN, DEFAULT_MQTT_BROKER, DEFAULT_MQTT_PORT, true};
}

void updateMqttTopics() {
    if (bleMac.length() > 0) {
        String macUpper = bleMac;
        macUpper.toUpperCase();
        macUpper.replace(":", "");
        mqttPublishTopic = "pets/" + macUpper + "/data";
        Serial.printf("[MQTT] Publish topic set to: %s\n", mqttPublishTopic.c_str());
        Serial.printf("[MQTT] Command topic: %s\n", mqttCommandTopic.c_str());
    }
}

void flushStepCountToEeprom(bool force) {
    if (stepsPending || force) {
        eeprom::getInstance().save(breedFactor, stepCount);
        lastEepromWrite = millis();
        pendingSteps = 0;
        stepsPending = false;
    }
}

void checkAndWriteEeprom() {
    unsigned long now = millis();
    if (stepsPending && 
        ((now - lastEepromWrite >= EEPROM_WRITE_INTERVAL) ||
         (pendingSteps >= STEP_COUNT_THRESHOLD) ||
         (now - lastForceWriteCheck >= FORCE_WRITE_INTERVAL))) {
        flushStepCountToEeprom(false);
        lastForceWriteCheck = now;
    }
}

void initBleQueues() {
    if (bleResponseQueue == NULL) {
        bleResponseQueue = xQueueCreate(BLE_RESPONSE_QUEUE_SIZE, sizeof(BleResponse));
        if (bleResponseQueue == NULL) {
            Serial.println("[ERROR] Failed to create response queue");
        }
    }
    if (bleCommandQueue == NULL) {
        bleCommandQueue = xQueueCreate(BLE_CMD_QUEUE_SIZE, sizeof(BleCommand));
        if (bleCommandQueue == NULL) {
            Serial.println("[ERROR] Failed to create command queue");
        }
    }
}

void sendBleResponse(const String& message) {
    if (bleResponseQueue && BLE::getInstance().isConnected()) {
        BleResponse response;
        // Safely copy with bounds checking
        strlcpy(response.message, message.c_str(), MAX_BLE_MSG_LEN);
        
        if (xPortInIsrContext()) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(bleResponseQueue, &response, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        } else {
            if (xQueueSend(bleResponseQueue, &response, pdMS_TO_TICKS(100)) != pdTRUE) {
                Serial.println("[BLE] Response queue full!");
            }
        }
    }
}

void processBleCommands() {
    BleCommand cmd;
    while (bleCommandQueue && xQueueReceive(bleCommandQueue, &cmd, 0)) {
        String command = String(cmd.command);
        String cmdLower = command;
        cmdLower.toLowerCase();
        
        Serial.printf("[BLE CMD] Processing: %s\n", command.c_str());
        
        if (cmdLower.startsWith("set_token:")) {
            String token = command.substring(10);
            token.trim();
            if (token.length() && saveMQTTConfig(token, mqttConfig.broker, mqttConfig.port)) {
                sendBleResponse("Token saved, reconnecting...");
                MqttManager::getInstance().setCredentials(token.c_str(), token.c_str());
                MqttManager::getInstance().connect();
            }
        }
        else if (cmdLower.startsWith("set_broker:")) {
            String broker = command.substring(11);
            broker.trim();
            if (broker.length() && saveMQTTConfig(mqttConfig.token, broker, mqttConfig.port)) {
                sendBleResponse("Broker saved, reconnecting...");
                MqttManager::getInstance().setServer(broker.c_str(), mqttConfig.port);
                MqttManager::getInstance().connect();
            }
        }
        else if (cmdLower == "reset" || cmdLower == "reset_steps") {
            stepCount = 0;
            pendingSteps = 0;
            stepsPending = true;
            sensor::getInstance().resetStepCount();
            flushStepCountToEeprom(true);
            sendBleResponse("Steps reset");
        }
        else if (cmdLower == "clear") {
            stepCount = 0;
            pendingSteps = 0;
            breedFactor = 5;
            sosFlag = 0;
            stepsPending = true;
            sensor::getInstance().resetStepCount();
            sensor::getInstance().setBreedFactor(breedFactor);
            flushStepCountToEeprom(true);
            setMode("normal");
            sendBleResponse("Cleared to defaults");
        }
        else if (cmdLower.startsWith("breed:")) {
            int val = cmdLower.substring(6).toInt();
            if (val >= 1 && val <= 10) {
                breedFactor = val;
                sensor::getInstance().setBreedFactor(breedFactor);
                flushStepCountToEeprom(true);
                sendBleResponse("Breed: " + String(breedFactor));
            }
        }
        else if (cmdLower == "sos_on") { 
            sosFlag = 1; 
            setMode("safe"); 
            sendBleResponse("SOS ON"); 
        }
        else if (cmdLower == "sos_off") { 
            sosFlag = 0; 
            setMode("normal"); 
            sendBleResponse("SOS OFF"); 
        }
        else if (cmdLower == "get_position") {
            if (latitude != 0 || longitude != 0)
                sendBleResponse("Lat: " + String(latitude, 6) + ", Lon: " + String(longitude, 6));
            else 
                sendBleResponse("No GPS fix");
        }
        else if (cmdLower == "topic") {
            if (mqttPublishTopic.length() > 0) {
                sendBleResponse("Publish Topic: " + mqttPublishTopic + "\nCommand Topic: " + mqttCommandTopic);
            } else {
                sendBleResponse("Topics not set yet. Connect to BLE first.");
            }
        }
        else if (cmdLower == "set_live") {
            setMode("live");
            sendBleResponse("Mode set to LIVE");
        }
        else if (cmdLower == "set_normal") {
            setMode("normal");
            sendBleResponse("Mode set to NORMAL");
        }
        else if (cmdLower == "set_safe") {
            setMode("safe");
            sendBleResponse("Mode set to SAFE");
        }
        else if (cmdLower == "status") {
            sendDataUpdate();
        }
        else if (cmdLower == "help") {
            sendBleResponse("Commands: reset, clear, status, topic, sos_on/off, breed:X, get_position, set_live/normal/safe, set_token:TOKEN, set_broker:URL");
        }
        
        if (cmdLower != "status") {
            sendDataUpdate();
        }
    }
}

void processBleResponses() {
    BleResponse response;
    while (bleResponseQueue && xQueueReceive(bleResponseQueue, &response, 0)) {
        BLE::getInstance().notify(String(response.message));
        delay(10);
    }
}

void updateLedStatus() {
    if (batteryLevel > 70) updateLED(BATTERY_LED, GREEN);
    else if (batteryLevel > 30) updateLED(BATTERY_LED, YELLOW);
    else updateLED(BATTERY_LED, RED);
    
    if (mode == "live") updateLED(NETWORK_LED, gprsStatus ? BLUE : RED);
    else if (mode == "blegps") updateLED(NETWORK_LED, bleConnected ? PURPLE : YELLOW);
    else if (gprsStatus) updateLED(NETWORK_LED, GREEN);
    else if (bleConnected) updateLED(NETWORK_LED, BLUE);
    else updateLED(NETWORK_LED, RED);
    
    updateLED(LOCATION_LED, (latitude != 0 || longitude != 0) ? PURPLE : WHITE);
}

void setMode(const String& newMode) {
    if (stepsPending) flushStepCountToEeprom(true);
    
    String m = newMode;
    m.toLowerCase();
    
    if (m == "live") {
        mode = "live";
        DATA_UPDATE_INTERVAL = liveInterval;
        ModemManager& modem = ModemManager::getInstance();
        if (!modem.isGPRSConnected()) modem.connectGPRS(APN, GPRS_USER, GPRS_PASS);
        if (!modem.initGPS(false)) Serial.println("[MODE] GPS init failed");
    } 
    else if (m == "safe") {
        mode = "safe";
        DATA_UPDATE_INTERVAL = safeInterval;
    } 
    else if (m == "normal") {
        mode = "normal";
        DATA_UPDATE_INTERVAL = normalInterval;
    } 
    else if (m == "blegps") {
        mode = "blegps";
        DATA_UPDATE_INTERVAL = normalInterval;
    }
    Serial.printf("[MODE] %s Mode, Update interval: %lu ms\n", mode.c_str(), DATA_UPDATE_INTERVAL);
}

String getBleMacAddress() {
    String mac = BLE::getInstance().getMacAddress();
    if (mac.length() == 0) {
        uint64_t chipId = ESP.getEfuseMac();
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 (uint8_t)(chipId >> 40), (uint8_t)(chipId >> 32),
                 (uint8_t)(chipId >> 24), (uint8_t)(chipId >> 16),
                 (uint8_t)(chipId >> 8), (uint8_t)chipId);
        mac = String(macStr);
    }
    return mac;
}

String getSimNumber() {
    ModemManager& modem = ModemManager::getInstance();
    String response;
    modem.getModem()->sendAT(GF("+CGSN"));
    if (modem.getModem()->waitResponse(5000L, response)) {
        for (int i = 0; i < response.length(); i++) {
            if (isdigit(response.charAt(i))) {
                int start = i;
                while (i < response.length() && isdigit(response.charAt(i))) i++;
                if (i - start >= 15) return response.substring(start, start + 15);
            }
        }
    }
    return "000000000000000";
}

void updateSensorData() {
    ModemManager& modem = ModemManager::getInstance();
    
    simNumber = getSimNumber();
    bleMac = getBleMacAddress();
    
    if (bleMac.length() > 0 && mqttPublishTopic.length() == 0) {
        updateMqttTopics();
    }
    
    if (mode == "live" || mode == "blegps") {
        GPSData gps;
        if (modem.getGPSInfo(gps) && gps.valid) {
            latitude = gps.latitude;
            longitude = gps.longitude;
            Serial.printf("[GPS] Fix: %.6f, %.6f\n", latitude, longitude);
        } else {
            latitude = longitude = 0.0;
        }
    }
    
    float voltage = modem.getBatteryVoltage();
    if (voltage >= 4.2) batteryLevel = 100;
    else if (voltage <= 3.3) batteryLevel = 0;
    else batteryLevel = constrain((voltage - 3.3) * (100.0 / 0.9), 0, 100);
    
    gprsStatus = modem.isGPRSConnected() ? 1 : 0;
    signalStrength = modem.getSignalStrength();
    
    Serial.printf("[SENSOR] Batt: %.2fV (%.0f%%), Signal: %d, GPRS: %d\n", 
                  voltage, batteryLevel, signalStrength, gprsStatus);
}

void checkSosButton() {
    bool currentState = digitalRead(SOS_PIN);
    if (lastSosState == HIGH && currentState == LOW) {
        sosFlag = !sosFlag;
        if (stepsPending) flushStepCountToEeprom(true);
        setMode(sosFlag ? "safe" : "normal");
        if (sosFlag) updateLED(LOCATION_LED, RED);
        sendDataUpdate();
        Serial.printf("[SOS] Button pressed, SOS Flag: %d, Mode: %s\n", sosFlag, mode.c_str());
    }
    lastSosState = currentState;
}

void sendDataUpdate() {
    // Calculate required buffer size dynamically
    size_t neededSize = 256 + simNumber.length() + bleMac.length() + 
                        String(stepCount).length() + mode.length();
    
    char* payload = (char*)malloc(neededSize);
    if (!payload) {
        Serial.println("[ERROR] Failed to allocate payload buffer");
        return;
    }
    
    int len = snprintf(payload, neededSize,
             "{\"SIM\":\"%s\",\"MACID\":\"%s\",\"Latitude\":%.6f,\"Longitude\":%.6f,"
             "\"Battery\":%.2f,\"StepCount\":%lu,\"WiFi\":%d,\"Signal\":%d,\"SOS\":%d,"
             "\"Reset\":0,\"BLE\":%d,\"BreedFactor\":%d,\"Mode\":\"%s\"}",
             simNumber.c_str(), bleMac.c_str(), latitude, longitude, batteryLevel,
             stepCount, gprsStatus, signalStrength, sosFlag, bleConnected ? 1 : 0, 
             breedFactor, mode.c_str());
    
    if (len > 0 && len < neededSize) {
        String payloadStr(payload);
        
        if (MqttManager::getInstance().isConnected()) {
            if (mqttPublishTopic.length() == 0 && bleMac.length() > 0) {
                updateMqttTopics();
            }
            String topic = (mqttPublishTopic.length() > 0) ? mqttPublishTopic : "device/data";
            if (MqttManager::getInstance().publish(topic, payloadStr)) {
                updateLED(NETWORK_LED, WHITE);
                delay(50);
                updateLedStatus();
            }
        }
        
        if (BLE::getInstance().isConnected()) {
            BLE::getInstance().notify(payloadStr);
            if (!MqttManager::getInstance().isConnected()) {
                updateLED(NETWORK_LED, PURPLE);
                delay(50);
                updateLedStatus();
            }
        }
    }
    
    free(payload);
}

void onBleCommand(const String& command) {
    if (bleCommandQueue) {
        // Validate command length
        if (command.length() >= MAX_BLE_MSG_LEN) {
            Serial.println("[BLE] Command too long, ignoring");
            sendBleResponse("Command too long (max " + String(MAX_BLE_MSG_LEN) + " chars)");
            return;
        }
        
        BleCommand cmd;
        strlcpy(cmd.command, command.c_str(), MAX_BLE_MSG_LEN);
        
        if (xPortInIsrContext()) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(bleCommandQueue, &cmd, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        } else {
            if (xQueueSend(bleCommandQueue, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
                Serial.println("[BLE] Command queue full!");
                sendBleResponse("Command queue full, try again");
            }
        }
    } else {
        Serial.println("[BLE] Command queue not initialized!");
    }
}

void onMqttMessage(String topic, String payload) {
    // Accept commands from our command topic
    if (topic != mqttCommandTopic) {
        return;
    }
    
    String cmd = payload;
    cmd.toLowerCase();
    
    Serial.printf("[MQTT CMD] Received on %s: %s\n", topic.c_str(), cmd.c_str());
    
    if (cmd == "reset_steps") {
        stepCount = 0;
        pendingSteps = 0;
        stepsPending = true;
        sensor::getInstance().resetStepCount();
        flushStepCountToEeprom(true);
        sendBleResponse("Steps reset via MQTT");
    }
    else if (cmd == "sos_on") { 
        sosFlag = 1; 
        setMode("safe"); 
        sendBleResponse("SOS ON via MQTT");
    }
    else if (cmd == "sos_off") { 
        sosFlag = 0; 
        setMode("normal"); 
        sendBleResponse("SOS OFF via MQTT");
    }
    else if (cmd.startsWith("breed:")) {
        int val = cmd.substring(6).toInt();
        if (val >= 1 && val <= 10) {
            breedFactor = val;
            sensor::getInstance().setBreedFactor(breedFactor);
            flushStepCountToEeprom(true);
            sendBleResponse("Breed factor set to: " + String(breedFactor));
        }
    }
    else if (cmd == "restart") {
        sendBleResponse("Restarting device...");
        delay(100);
        if (stepsPending) flushStepCountToEeprom(true);
        delay(100);
        ESP.restart();
    }
    else if (cmd == "set_live") {
        setMode("live");
        sendBleResponse("Mode set to LIVE via MQTT");
    }
    else if (cmd == "set_normal") {
        setMode("normal");
        sendBleResponse("Mode set to NORMAL via MQTT");
    }
    else if (cmd == "set_safe") {
        setMode("safe");
        sendBleResponse("Mode set to SAFE via MQTT");
    }
    else if (cmd == "status") { 
        sendDataUpdate(); 
        return;
    }
    else {
        Serial.printf("[MQTT] Unknown command: %s\n", cmd.c_str());
        return;
    }
    
    sendDataUpdate();
}

void onMqttConnection(bool connected) {
    if (connected) {
        // Subscribe to command topic
        if (MqttManager::getInstance().subscribe(mqttCommandTopic)) {
            Serial.printf("[MQTT] Subscribed to %s\n", mqttCommandTopic.c_str());
        } else {
            Serial.printf("[MQTT] Failed to subscribe to %s\n", mqttCommandTopic.c_str());
        }
        
        // Send initial data update
        sendDataUpdate();
        Serial.println("[MQTT] Connected and ready");
    } else {
        Serial.println("[MQTT] Connection lost");
    }
}

void onBleConnection(bool connected) {
    bleConnected = connected;
    
    if (connected && mqttPublishTopic.length() == 0 && bleMac.length() > 0) {
        updateMqttTopics();
    }
    
    if (!connected && stepsPending) flushStepCountToEeprom(true);
    updateLedStatus();
    if (connected) sendDataUpdate();
    Serial.printf("[BLE] Connection state: %s\n", connected ? "Connected" : "Disconnected");
}

void processMotion() {
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    
    if (now - lastUpdate >= 50) {
        lastUpdate = now;
        
        sensor& motion = sensor::getInstance();
        motion.updateStepCounter();
        
        unsigned long newCount = motion.getStepCount();
        
        if (newCount != stepCount) {
            unsigned long diff = newCount - stepCount;
            stepCount = newCount;
            pendingSteps += diff;
            stepsPending = true;
            
            checkAndWriteEeprom();
            
            static unsigned long lastSend = 0;
            if (stepCount % 10 == 0 && (now - lastSend) >= 1000) {
                lastSend = now;
                sendDataUpdate();
            }
        }
    }
}

void setup() {
    delay(5000);
    Serial.begin(115200);
    Serial.println("\n=== ValTrack GPS Tracker v2.0 ===\n");
    
    initFileSystem();
    loadMQTTConfig();
    initBleQueues();
    
    if (bleResponseQueue == NULL || bleCommandQueue == NULL) {
        Serial.println("[FATAL] Could not create queues!");
        while(1) delay(1000);
    }
    
    pinMode(SOS_PIN, INPUT_PULLUP);
    initLED();
    updateLED(BATTERY_LED, RED);
    updateLED(NETWORK_LED, RED);
    updateLED(LOCATION_LED, RED);
    delay(1000);
    
    eeprom::getInstance().init(512);
    eeprom::getInstance().load(breedFactor, stepCount);

    if (breedFactor < 1 || breedFactor > 10) {
        Serial.printf("[EEPROM] Invalid BreedFactor: %d, resetting to 5\n", breedFactor);
        breedFactor = 5;
        stepCount = 0;
        eeprom::getInstance().save(breedFactor, stepCount);
    }

    lastEepromWrite = lastForceWriteCheck = millis();
    
    sensor& motion = sensor::getInstance();
    motion.init(5, 6);
    motion.setBreedFactor(breedFactor);
    motion.resetStepCount();
    Serial.printf("[SENSOR] Step counter initialized, EEPROM steps: %lu\n", stepCount);
    
    BLE::getInstance().init(BLE_NAME, true);
    BLE::getInstance().setCommandCallback(onBleCommand);
    BLE::getInstance().setConnectionCallback(onBleConnection);
    
    ModemManager& modem = ModemManager::getInstance();
    if (modem.begin(115200)) {
        modem.enableGSM();
        modem.powerOnModem();
        
        if (modem.initModem() && modem.waitForNetwork(30000)) {
            if (modem.connectGPRS(APN, GPRS_USER, GPRS_PASS)) {
                updateLED(NETWORK_LED, BLUE);
                Serial.println("[GSM] GPRS Connected");
            }
        }
        if (modem.initGPS(true)) {
            // modem.debugGPS();
        }
    }
    
    MqttManager::getInstance().begin(*modem.getClient());
    MqttManager::getInstance().setServer(mqttConfig.broker.c_str(), mqttConfig.port);
    MqttManager::getInstance().setCredentials(mqttConfig.token.c_str(), mqttConfig.token.c_str());
    MqttManager::getInstance().setClientId("Valtrack_" + String(ESP.getEfuseMac(), HEX));
    MqttManager::getInstance().onMessage(onMqttMessage);
    MqttManager::getInstance().onConnection(onMqttConnection);
    MqttManager::getInstance().setAutoReconnect(true);
    MqttManager::getInstance().connect();
    
    setMode("live");
    updateSensorData();
    sendDataUpdate();
    updateLED(BATTERY_LED, GREEN);
    
    Serial.println("\n=== System Ready ===\n");
}

void loop() {
    processBleCommands();
    processBleResponses();
    MqttManager::getInstance().loop();
    checkSosButton();
    processMotion();
    
    unsigned long now = millis();
    
    if (now - lastDataUpdate >= DATA_UPDATE_INTERVAL) {
        lastDataUpdate = now;
        updateSensorData();
        sendDataUpdate();
        checkAndWriteEeprom();
    }
    
    if (now - lastLedUpdate >= LED_UPDATE_INTERVAL) {
        lastLedUpdate = now;
        updateLedStatus();
        if (sosFlag) {
            static bool ledState = false;
            ledState = !ledState;
            updateLED(LOCATION_LED, ledState ? RED : WHITE);
        }
    }
    
    checkAndWriteEeprom();
    delay(10);
}