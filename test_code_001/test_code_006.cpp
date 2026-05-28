#include <Arduino.h>
#include "modem/ModemManager.h"

// GPRS credentials (from reference)
const char apn[] = "vi";
const char gprsUser[] = "";
const char gprsPass[] = "";

void setup() {
    delay(5000);  // Reference delay
    
    // Get modem instance
    ModemManager& modem = ModemManager::getInstance();
    
    // Initialize hardware and modem
    if (!modem.begin(115200)) {
        SerialMon.println("Modem begin failed!");
        return;
    }
    
    // Enable GSM power
    modem.enableGSM();
    
    // Power on modem (matching reference timing)
    modem.powerOnModem();
    
    // Initialize modem (restart and get info)
    if (!modem.initModem()) {
        SerialMon.println("Modem init failed!");
        return;
    }
    
    // Wait for network (from reference)
    if (!modem.waitForNetwork(30000)) {
        SerialMon.println("Network timeout!");
        return;
    }
    
    // Get signal quality (from reference)
    SerialMon.print("Signal Quality: ");
    SerialMon.println(modem.getSignalQuality());
    
    // Connect to GPRS (from reference)
    if (modem.connectGPRS(apn, gprsUser, gprsPass)) {
        SerialMon.println("GPRS Connected!");
        SerialMon.print("IP: ");
        SerialMon.println(modem.getLocalIP());
    }
    
    // Initialize GPS
    if (modem.initGPS(false)) {  // Cold start
        SerialMon.println("GPS started, waiting for fix...");
        
        float lat, lon;
        unsigned long start = millis();
        while (millis() - start < 60000) {
            if (modem.getGPSPosition(lat, lon, 5000)) {
                SerialMon.printf("Position: %.6f, %.6f\n", lat, lon);
                break;
            }
            SerialMon.println("Waiting for GPS fix...");
            delay(5000);
        }
    }
    
    // Optional: HTTP GET example (from reference)
    String response;
    if (modem.httpGET("httpbin.org", 80, "/ip", response)) {
        SerialMon.println("HTTP Response: " + response);
    }
    
    // Read battery
    float voltage = modem.getBatteryVoltage();
    SerialMon.printf("Battery: %.2fV\n", voltage);
    
    int batteryPercent;
    if (modem.getBatteryPercent(batteryPercent)) {
        SerialMon.printf("Battery: %d%%\n", batteryPercent);
    }
}

void loop() {
    ModemManager& modem = ModemManager::getInstance();
    
    // Maintain GPRS connection
    if (modem.isGPRSConnected()) {
        // Do something with client
        TinyGsmClient* client = modem.getClient();
        // Use client for custom requests
    }
    
    // Optional: Periodic GPS updates
    static unsigned long lastGPS = 0;
    if (millis() - lastGPS > 30000) {  // Every 30 seconds
        GPSData gps;
        if (modem.getGPSInfo(gps) && gps.valid) {
            SerialMon.printf("GPS: %.6f, %.6f, Alt: %.1fm\n", 
                           gps.latitude, gps.longitude, gps.altitude);
        }
        lastGPS = millis();
    }
    
    delay(1000);
}