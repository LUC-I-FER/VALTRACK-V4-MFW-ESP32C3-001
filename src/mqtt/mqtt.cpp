#include "mqtt.h"
#include "../modem/modem.h"   // for 'modem' and SerialMon
#include <Arduino.h>

static String mqttBroker = "tcp://mqtt.flespi.io:1883";
static String mqttToken = "Hz8ZN1ZvlOH5yZcvgKJyaC3dOrdqILdsMQkdmFUKkqUabNcVIQprNgf7Fd1Vb3ZV";
static String mqttClientId = "ESP32C3_GSM";

static void mqtt_acquire(uint8_t idx, const String& client_name) {
    String cmd = "+CMQTTACCQ=" + String(idx) + ",\"" + client_name + "\",0";
    modem.sendAT(GF(cmd));
    modem.waitResponse(1000UL);
}

static bool mqtt_connect(uint8_t idx, const String& url, const String& token) {
    String cmd = "+CMQTTCONNECT=" + String(idx) + ",\"" + url + "\",20,0,\"" + token + "\"";
    modem.sendAT(GF(cmd));
    return modem.waitResponse(5000UL) == 1;
}

void mqtt_set_broker(const String& broker_url, const String& token) {
    mqttBroker = broker_url;
    mqttToken = token;
}

void mqtt_init() {
    if (mqttToken.length() == 0) {
        SerialMon.println("MQTT Error: token not set. Call mqtt_set_broker() first.");
        return;
    }

    SerialMon.println("Starting MQTT service on modem...");
    modem.sendAT(GF("+CMQTTSTART"));
    if (modem.waitResponse(10000UL, "+CMQTTSTART: 0") != 1) {
        SerialMon.println("Failed to start MQTT service");
        return;
    }
    mqtt_acquire(0, mqttClientId);
    if (mqtt_connect(0, mqttBroker, mqttToken)) {
        SerialMon.println("MQTT connected to " + mqttBroker);
    } else {
        SerialMon.println("MQTT connection failed");
    }
}

// Wait for '>' prompt from modem, returns true if found within timeout
static bool waitForPrompt(unsigned long timeout_ms = 3000) {
    unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        if (modem.stream.available()) {
            String line = modem.stream.readStringUntil('\n');
            SerialMon.print("DEBUG: got line: ");
            SerialMon.println(line);
            if (line.indexOf('>') >= 0) {
                return true;
            }
        }
        delay(10);
    }
    return false;
}

static bool mqtt_set_topic(uint8_t idx, const String& topic) {
    String cmd = "+CMQTTTOPIC=" + String(idx) + "," + String(topic.length());
    SerialMon.println("Sending: AT" + cmd);
    modem.sendAT(GF(cmd));
    
    if (!waitForPrompt(3000)) {
        SerialMon.println("ERROR: No '>' prompt for topic");
        return false;
    }
    
    SerialMon.print("Sending topic: ");
    SerialMon.println(topic);
    modem.stream.write(topic.c_str(), topic.length());
    modem.stream.flush();
    
    return modem.waitResponse(5000, "OK") == 1;
}

static bool mqtt_set_payload(uint8_t idx, const String& payload) {
    String cmd = "+CMQTTPAYLOAD=" + String(idx) + "," + String(payload.length());
    SerialMon.println("Sending: AT" + cmd);
    modem.sendAT(GF(cmd));
    
    if (!waitForPrompt(3000)) {
        SerialMon.println("ERROR: No '>' prompt for payload");
        return false;
    }
    
    SerialMon.print("Sending payload: ");
    SerialMon.println(payload);
    modem.stream.write(payload.c_str(), payload.length());
    modem.stream.flush();
    
    return modem.waitResponse(5000, "OK") == 1;
}

static bool mqtt_publish_cmd(uint8_t idx) {
    String cmd = "+CMQTTPUB=" + String(idx) + ",0,100";
    SerialMon.println("Sending: AT" + cmd);
    modem.sendAT(GF(cmd));
    return modem.waitResponse(5000UL) == 1;
}

int mqtt_publish(const String& topic, const String& payload) {
    if (!mqtt_set_topic(0, topic)) {
        SerialMon.println("MQTT: set topic failed");
        return -1;
    }
    if (!mqtt_set_payload(0, payload)) {
        SerialMon.println("MQTT: set payload failed");
        return -1;
    }
    if (!mqtt_publish_cmd(0)) {
        SerialMon.println("MQTT: publish failed");
        return -1;
    }
    SerialMon.println("MQTT published: " + topic);
    return 0;
}

String mqtt_read_incoming() {
    String line;
    if (modem.stream.available()) {
        line = modem.stream.readStringUntil('\n');
        if (line.startsWith("+CMQTTRXSTART:")) {
            // Read and discard topic line
            while (!modem.stream.available()) delay(10);
            String topicLine = modem.stream.readStringUntil('\n');
            // Read payload line
            while (!modem.stream.available()) delay(10);
            String payloadLine = modem.stream.readStringUntil('\n');
            int start = payloadLine.indexOf('"') + 1;
            int end = payloadLine.lastIndexOf('"');
            if (start > 0 && end > start) {
                String payload = payloadLine.substring(start, end);
                SerialMon.print("MQTT received: ");
                SerialMon.println(payload);
                return payload;
            }
        }
    }
    return "";
}