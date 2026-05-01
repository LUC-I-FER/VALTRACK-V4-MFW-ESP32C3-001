#include <Arduino.h>
#include "sensor/sensor.h"

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[Sensor Test] Starting LIS3DH test");

    // Initialize sensor with default I2C pins (SDA=5, SCL=6)
    if (sensor::getInstance().init()) {
        Serial.println("[Sensor Test] Sensor initialized successfully");
    } else {
        Serial.println("[Sensor Test] ERROR: Sensor not found! Check wiring.");
        while (1) { delay(1000); }  // halt
    }

    Serial.println("\nCommands:");
    Serial.println("  's' - sleep mode");
    Serial.println("  'w' - wake mode");
    Serial.println("  'r' - read one sample");
    Serial.println("  (any other key) - continuous reading every 200ms");
}

void loop() {
    // Check for user input
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == 's') {
            sensor::getInstance().sleep();
            Serial.println("[Sensor] Sleep command sent");
        } else if (ch == 'w') {
            sensor::getInstance().wake();
            Serial.println("[Sensor] Wake command sent");
        } else if (ch == 'r') {
            sensorData data;
            if (sensor::getInstance().read(data)) {
                Serial.printf("X=%6d  Y=%6d  Z=%6d  time=%lu\n", data.x, data.y, data.z, data.timestamp);
            } else {
                Serial.println("Read failed");
            }
        } else {
            // continuous reading mode
            Serial.println("Continuous reading (200ms). Press any key to stop.");
            while (!Serial.available()) {
                sensorData data;
                if (sensor::getInstance().read(data)) {
                    Serial.printf("X=%6d  Y=%6d  Z=%6d\n", data.x, data.y, data.z);
                } else {
                    Serial.println("Read error");
                }
                delay(200);
            }
            // flush the key that stopped it
            while (Serial.available()) Serial.read();
            Serial.println("Continuous reading stopped.");
        }
    } else {
        // Default: read and print once per second
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 1000) {
            lastPrint = millis();
            sensorData data;
            if (sensor::getInstance().read(data)) {
                Serial.printf("X=%6d  Y=%6d  Z=%6d\n", data.x, data.y, data.z);
            } else {
                Serial.println("Read error");
            }
        }
        delay(10);
    }
}