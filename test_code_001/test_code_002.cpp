#include <Arduino.h>
#include "modem/modem.h"

// ---------- CONFIG ----------
#define PUBLISH_INTERVAL 5000   // 5 sec

unsigned long lastPublish = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n===== SYSTEM START =====");

    // 1. Init modem (retry)
    while (!modem_init()) {
        Serial.println("[MAIN] Init failed, retrying...");
        delay(3000);
    }

    // 2. Wait until SIM + network ready
    while (!modem_is_ready()) {
        Serial.println("[MAIN] Waiting modem ready...");
        delay(2000);
    }

    // 3. Connect GPRS
    while (!modem_connect_gprs()) {
        Serial.println("[MAIN] GPRS retry...");
        delay(3000);
    }

    // 4. Start MQTT
    while (!modem_mqtt_start()) {
        Serial.println("[MAIN] MQTT retry...");
        delay(3000);
    }

    Serial.println("[MAIN] System Ready ✅");
}

void loop() {

    // ---------- KEEP MQTT ALIVE ----------
    String msg = modem_mqtt_read();
    if (msg.length() > 0) {
        Serial.print("[MAIN] RX: ");
        Serial.println(msg);
    }

    // ---------- PERIODIC PUBLISH ----------
    if (millis() - lastPublish > PUBLISH_INTERVAL) {
        lastPublish = millis();

        String payload = "{ \"msg\": \"hello from ESP32\" }";

        if (modem_mqtt_publish("test/topic", payload)) {
            Serial.println("[MAIN] Publish OK");
        } else {
            Serial.println("[MAIN] Publish FAIL");
        }
    }

    // ---------- OPTIONAL: HEALTH CHECK ----------
    if (!modem_is_ready()) {
        Serial.println("[MAIN] Modem lost! Reinitializing...");

        modem_deinit();
        delay(2000);

        modem_init();
        modem_connect_gprs();
        modem_mqtt_start();
    }

    delay(100);  // small yield
}