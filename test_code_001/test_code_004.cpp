#include <Arduino.h>
#include "ble/ble.h"

// Simulated data (replace with real sensor reads later)
static unsigned long stepCount = 0;
static float batteryVoltage = 3.85;
static unsigned long lastStep = 0;

// Build a JSON string from current state
String buildJsonData() {
    String json = "{";
    json += "\"steps\":" + String(stepCount) + ",";
    json += "\"battery\":" + String(batteryVoltage, 2) + ",";
    json += "\"connected\":" + String(BLE::getInstance().isConnected() ? "true" : "false") + ",";
    json += "\"timestamp\":" + String(millis());
    json += "}";
    return json;
}

// BLE command callback – processes incoming strings and responds with JSON
void onBleCommand(const String& command) {
    Serial.print("[BLE CMD] Raw: ");
    Serial.println(command);

    // Parse simple text commands
    String response;

    if (command == "ping") {
        response = "{\"response\":\"pong\",\"timestamp\":" + String(millis()) + "}";
    }
    else if (command == "status") {
        response = buildJsonData();
    }
    else if (command == "step") {
        stepCount++;
        response = "{\"status\":\"ok\",\"action\":\"step\",\"newCount\":" + String(stepCount) + "}";
    }
    else if (command.startsWith("battery=")) {
        float v = command.substring(8).toFloat();
        if (v > 0 && v < 4.5) {
            batteryVoltage = v;
            response = "{\"status\":\"ok\",\"battery\":" + String(batteryVoltage, 2) + "}";
        } else {
            response = "{\"error\":\"invalid voltage\",\"value\":\"" + command.substring(8) + "\"}";
        }
    }
    else if (command == "help") {
        response = "{\"commands\":[\"ping\",\"status\",\"step\",\"battery=X\",\"help\"]}";
    }
    else {
        response = "{\"error\":\"unknown command\",\"received\":\"" + command + "\"}";
    }

    // Send the JSON reply as a notification
    BLE::getInstance().notify(response);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[BLE JSON Test] Starting");

    // Initialize BLE with a recognizable name
    BLE::getInstance().setCommandCallback(onBleCommand);
    BLE::getInstance().init("ESP32_BLE_JSON");

    Serial.println("Device name: ESP32_BLE_JSON");
    Serial.println("Connect and write commands (ping, status, step, battery=3.9, help)");
    Serial.println("Periodic JSON notifications will be sent every 10 seconds when connected.");
}

void loop() {
    static unsigned long lastSend = 0;
    unsigned long now = millis();

    // Simulate step increment every 5 seconds to show changing data
    static unsigned long lastFakeStep = 0;
    if (now - lastFakeStep >= 5000) {
        lastFakeStep = now;
        stepCount++;
        Serial.println("[SIM] Step increased automatically (demo)");
    }

    // Send JSON data every 10 seconds only if a BLE client is connected
    if (BLE::getInstance().isConnected() && (now - lastSend >= 10000)) {
        lastSend = now;
        String jsonData = buildJsonData();
        BLE::getInstance().notify(jsonData);
        Serial.println("[BLE] Sent JSON notification: " + jsonData);
    }

    delay(10);   // allow BLE stack to process
}