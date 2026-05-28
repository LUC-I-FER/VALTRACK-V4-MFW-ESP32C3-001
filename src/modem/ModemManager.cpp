#include "ModemManager.h"
#include <ArduinoHttpClient.h>

// Static member initialization
ModemManager* ModemManager::instance = nullptr;
extern HardwareSerial Serial1;

//=============================================================================
// Constructor / Destructor
//=============================================================================

ModemManager::ModemManager() 
    : modem(nullptr)
    , client(nullptr)
    , gprsConnected(false)
    , gpsInitialized(false)
    , networkAvailable(false)
{
}

ModemManager::~ModemManager() {
    end();
}

ModemManager& ModemManager::getInstance() {
    if (instance == nullptr) {
        instance = new ModemManager();
    }
    return *instance;
}

void ModemManager::destroyInstance() {
    if (instance != nullptr) {
        delete instance;
        instance = nullptr;
    }
}

//=============================================================================
// Initialization
//=============================================================================

bool ModemManager::begin(unsigned long serialBaud) {
    SerialMon.begin(115200);
    delay(1000);
    SerialMon.println("\n=== Modem Manager Initializing ===");
    
    // Initialize UART
    Serial1.begin(serialBaud, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(100);
    
    // Initialize GPIO pins
    pinMode(GPIO_PWRKEY, OUTPUT);
    pinMode(GPIO_GSM_ENABLE, OUTPUT);
    pinMode(GPIO_TPS_ENABLE, OUTPUT);
    
    digitalWrite(GPIO_PWRKEY, HIGH);
    digitalWrite(GPIO_GSM_ENABLE, LOW);
    digitalWrite(GPIO_TPS_ENABLE, HIGH);
    
    // Create modem instance
    modem = new TinyGsm(Serial1);
    client = new TinyGsmClient(*modem);
    
    return true;
}

void ModemManager::end() {
    if (gprsConnected) disconnectGPRS();
    if (gpsInitialized) stopGPS();
    disableGSM();
    
    delete client;
    delete modem;
    client = nullptr;
    modem = nullptr;
}

//=============================================================================
// Power Management
//=============================================================================

void ModemManager::enableGSM() {
    SerialMon.println("Enabling GSM...");
    digitalWrite(GPIO_GSM_ENABLE, HIGH);
    delay(100);
}

void ModemManager::disableGSM() {
    SerialMon.println("Disabling GSM...");
    digitalWrite(GPIO_GSM_ENABLE, LOW);
    gprsConnected = false;
    networkAvailable = false;
    delay(100);
}

void ModemManager::powerOnModem() {
    SerialMon.println("Powering on modem...");
    digitalWrite(GPIO_PWRKEY, LOW);
    delay(500);
    digitalWrite(GPIO_PWRKEY, HIGH);
    delay(1000);
    digitalWrite(GPIO_PWRKEY, LOW);
    SerialMon.println("Power pulse sent, waiting for modem to boot...");
}

void ModemManager::powerOffModem() {
    SerialMon.println("Powering off modem...");
    if (modem) modem->poweroff();
}

bool ModemManager::initModem() {
    if (!modem) return false;
    
    SerialMon.println("Initializing modem...");
    delay(6000);  // Wait for modem boot
    
    modem->restart();
    delay(2000);
    
    // Test AT communication
    for (int i = 0; i < 5; i++) {
        if (modem->testAT()) {
            SerialMon.println("✓ Modem responding");
            SerialMon.print("Modem Info: ");
            SerialMon.println(modem->getModemInfo());
            return true;
        }
        SerialMon.printf("Waiting for modem... attempt %d/5\n", i + 1);
        delay(2000);
    }
    
    SerialMon.println("✗ Modem initialization failed!");
    return false;
}

bool ModemManager::isModemAlive() {
    return modem && modem->testAT();
}

//=============================================================================
// Network Functions
//=============================================================================

bool ModemManager::waitForNetwork(unsigned long timeout_ms) {
    if (!modem) return false;
    
    SerialMon.print("Waiting for network...");
    networkAvailable = modem->waitForNetwork(timeout_ms);
    SerialMon.println(networkAvailable ? " success" : " fail");
    
    return networkAvailable;
}

int ModemManager::getSignalStrength() {
    if (!modem || !networkAvailable) return 0;
    return modem->getSignalQuality();
}

String ModemManager::getModemInfo() {
    if (!modem) return "";
    
    String info = modem->getModemInfo();
    if (info.length() == 0) {
        String response;
        modem->sendAT(GF("+CGSN"));
        if (modem->waitResponse(5000L, response)) {
            int start = response.indexOf("+CGSN:") + 6;
            if (start > 6) {
                int end = response.indexOf("\r\n", start);
                if (end == -1) end = response.length();
                info = "IMEI: " + response.substring(start, end);
            }
        }
    }
    return info;
}

//=============================================================================
// GPRS Functions
//=============================================================================

bool ModemManager::connectGPRS(const String& apn, const String& user, const String& pass) {
    if (!modem || !networkAvailable) return false;
    
    SerialMon.print("Connecting to APN: ");
    SerialMon.println(apn);
    
    gprsConnected = modem->gprsConnect(apn.c_str(), user.c_str(), pass.c_str());
    SerialMon.println(gprsConnected ? "GPRS connected" : "GPRS connection failed");
    
    return gprsConnected;
}

bool ModemManager::disconnectGPRS() {
    if (!modem || !gprsConnected) return false;
    
    modem->gprsDisconnect();
    gprsConnected = false;
    SerialMon.println("GPRS disconnected");
    return true;
}

String ModemManager::getLocalIP() {
    if (!modem || !gprsConnected) return "0.0.0.0";
    return modem->getLocalIP();
}

//=============================================================================
// GPS Functions
//=============================================================================

bool ModemManager::initGPS(bool hotStart) {
    if (!modem) return false;
    
    SerialMon.println("Initializing GPS...");
    
    // Power off then on GNSS
    modem->sendAT(GF("+CGNSSPWR=0"));
    modem->waitResponse(2000);
    
    modem->sendAT(GF("+CGNSSPWR=1"));
    delay(1000);
    
    String response;
    bool powerOnOk = false;
    unsigned long startTime = millis();
    
    while (millis() - startTime < 15000) {
        if (modem->waitResponse(1000, response)) {
            if (response.indexOf("OK") != -1) {
                powerOnOk = true;
                break;
            }
        }
    }
    
    if (!powerOnOk) {
        SerialMon.println("GPS power on failed");
        return false;
    }
    
    // Set GPS mode
    modem->sendAT(GF("+CGNSSMODE=3"));
    modem->waitResponse(5000);
    
    // Start GPS fix
    if (hotStart) {
        SerialMon.println("GPS hot start...");
        modem->sendAT(GF("+CGPSHOT"));
    } else {
        SerialMon.println("GPS cold start (may take 45-60 seconds)...");
        modem->sendAT(GF("+CGPSCOLD"));
    }
    modem->waitResponse(20000);
    
    gpsInitialized = true;
    SerialMon.println("GPS initialized");
    return true;
}

bool ModemManager::stopGPS() {
    if (!modem) return false;
    
    bool success = sendATCommand("+CGPS=0", "OK", 5000);
    gpsInitialized = !success;
    return success;
}

bool ModemManager::getGPSInfo(GPSData& data) {
    if (!modem) {
        Serial.println("[GPS] Modem not initialized");
        return false;
    }
    
    // Check if GPS is powered on
    String powerResponse;
    modem->sendAT(GF("+CGNSSPWR?"));
    if (!modem->waitResponse(2000L, powerResponse)) {
        Serial.println("[GPS] Cannot check GPS power status");
        data.valid = false;
        return false;
    }
    
    // If GPS is not powered, return false
    if (powerResponse.indexOf("+CGNSSPWR: 0") != -1) {
        Serial.println("[GPS] GPS is powered off");
        data.valid = false;
        return false;
    }
    
    // Try multiple times to get GPS data
    for (int attempt = 0; attempt < 3; attempt++) {
        String response;
        
        // Use the correct AT command for this modem
        modem->sendAT(GF("+CGPSINFO"));  // Without extra "AT"
        
        if (modem->waitResponse(8000L, response)) {
            Serial.print("[GPS] Response received (attempt ");
            Serial.print(attempt + 1);
            Serial.println("):");
            Serial.println(response);
            
            if (parseCGPSINFO(response, data)) {
                return true;
            }
        }
        
        // If no data, wait a bit before retry
        if (attempt < 2) delay(1000);
    }
    
    data.valid = false;
    return false;
}

String ModemManager::parseCGPSINFO(const String& response, GPSData& data) {
    int startIdx = response.indexOf(':');
    if (startIdx == -1) return "INVALID";
    startIdx += 2;
    
    int comma1 = response.indexOf(',', startIdx);
    int comma2 = response.indexOf(',', comma1 + 1);
    int comma3 = response.indexOf(',', comma2 + 1);
    int comma4 = response.indexOf(',', comma3 + 1);
    int comma5 = response.indexOf(',', comma4 + 1);
    int comma6 = response.indexOf(',', comma5 + 1);
    int comma7 = response.indexOf(',', comma6 + 1);
    int comma8 = response.indexOf(',', comma7 + 1);
    
    if (comma1 == -1 || comma2 == -1) return "INVALID";
    
    // Latitude
    String latStr = response.substring(startIdx, comma1);
    String latDir = response.substring(comma1 + 1, comma2);
    if (latStr.length() > 0 && latStr.toFloat() != 0) {
        float latVal = latStr.toFloat();
        int degrees = (int)(latVal / 100);
        float minutes = latVal - (degrees * 100);
        data.latitude = degrees + (minutes / 60.0);
        if (latDir == "S") data.latitude = -data.latitude;
    }
    
    // Longitude
    if (comma3 != -1 && comma4 != -1) {
        String lonStr = response.substring(comma2 + 1, comma3);
        String lonDir = response.substring(comma3 + 1, comma4);
        if (lonStr.length() > 0 && lonStr.toFloat() != 0) {
            float lonVal = lonStr.toFloat();
            int degrees = (int)(lonVal / 100);
            float minutes = lonVal - (degrees * 100);
            data.longitude = degrees + (minutes / 60.0);
            if (lonDir == "W") data.longitude = -data.longitude;
        }
    }
    
    // Date & Time
    if (comma5 != -1 && comma6 != -1) {
        data.date = response.substring(comma4 + 1, comma5);
        data.time = response.substring(comma5 + 1, comma6);
    }
    
    // Altitude, Speed, Course
    if (comma7 != -1) data.altitude = response.substring(comma6 + 1, comma7).toFloat();
    if (comma8 != -1) {
        data.speed = response.substring(comma7 + 1, comma8).toFloat();
        data.course = response.substring(comma8 + 1).toFloat();
    }
    
    data.valid = (data.latitude != 0 || data.longitude != 0);
    return data.valid ? "OK" : "NO_FIX";
}

//=============================================================================
// Battery Functions
//=============================================================================

float ModemManager::getBatteryVoltage() {
    if (!modem) return 0.0;
    
    String response;
    modem->sendAT(GF("+CBC"));
    
    if (!modem->waitResponse(5000L, response)) return 0.0;
    if (response.length() == 0 || response.indexOf("ERROR") != -1) return 0.0;
    
    int lastComma = response.lastIndexOf(',');
    if (lastComma != -1) {
        String voltageStr = response.substring(lastComma + 1);
        voltageStr.trim();
        voltageStr.replace("V", "");
        voltageStr.replace("\r", "");
        voltageStr.replace("\n", "");
        
        if (voltageStr.length() > 0) {
            float voltage = voltageStr.toFloat();
            if (voltage > 100) voltage /= 1000.0;
            return voltage;
        }
    }
    
    return 0.0;
}

//=============================================================================
// Helper Functions
//=============================================================================

bool ModemManager::sendATCommand(const String& cmd, const String& expected, unsigned long timeout, bool verbose) {
    if (!modem) return false;
    
    if (cmd.startsWith("AT")) {
        modem->sendAT(cmd.substring(2).c_str());
    } else {
        modem->sendAT(cmd.c_str());
    }
    
    String response;
    unsigned long start = millis();
    
    while (millis() - start < timeout) {
        if (modem->waitResponse(500, response)) {
            if (response.indexOf(expected) != -1) return true;
            if (response.indexOf("OK") != -1 && expected == "OK") return true;
        }
        delay(10);
        if (verbose) {SerialMon.printf("debug: "); SerialMon.println(response);};
    }
    return false;
}

//=============================================================================
// Debug Functions
//=============================================================================

void ModemManager::debugGPS() {
    if (!modem) return;
    
    SerialMon.println("\n=== GPS Debug Info ===");
    
    String response;
    modem->sendAT(GF("+CGNSSPWR?"));
    if (modem->waitResponse(2000, response)) {
        SerialMon.println("Power state: " + response);
    }
    
    modem->sendAT(GF("+CGNSSINFO"));
    if (modem->waitResponse(5000, response)) {
        SerialMon.println("Satellites: " + response);
    }
}