#include <Arduino.h>
#include "ble/ble.h"
#include "indicator/indicator.h"
#include "modem/ModemManager.h"

// System configuration
#define APN "vi"
#define APN_USER ""
#define APN_PASS ""

// GPS configuration
enum GPSStartMode {
    GPS_COLD_START,
    GPS_WARM_START,
    GPS_HOT_START
};

// Network configuration
struct NetworkConfig {
    String apn;
    String user;
    String pass;
    bool autoReconnect;
    unsigned long lastReconnectAttempt;
};

// System state
enum SystemState {
    STATE_INIT,
    STATE_POWER_ON,
    STATE_NETWORK_SEARCH,
    STATE_CONNECTING_GPRS,
    STATE_READY,
    STATE_ERROR,
    STATE_GPS_DISABLED,
    STATE_NETWORK_DISABLED
};

SystemState currentState = STATE_INIT;
bool bleConnected = false;
bool gpsEnabled = true;
bool networkEnabled = true;
unsigned long lastGPSUpdate = 0;
unsigned long lastStatusUpdate = 0;
unsigned long lastDebugUpdate = 0;
unsigned long lastNetworkCheck = 0;
unsigned long lastBLECommand = 0;

// GPS Debug levels
enum GPSDebugLevel {
    DEBUG_OFF,
    DEBUG_BASIC,
    DEBUG_VERBOSE,
    DEBUG_RAW
};

GPSDebugLevel currentDebugLevel = DEBUG_OFF;
bool continuousDebugMode = false;

// Debug history buffer
#define MAX_DEBUG_HISTORY 5  // Reduced from 10
struct DebugEntry {
    unsigned long timestamp;
    String data;
};
DebugEntry debugHistory[MAX_DEBUG_HISTORY];
int debugHistoryIndex = 0;
int debugHistoryCount = 0;

// Network configuration
NetworkConfig netConfig = {
    APN,
    APN_USER,
    APN_PASS,
    true,
    0
};

// Rate limiting for BLE notifications
const unsigned long BLE_NOTIFY_DELAY = 100; // milliseconds between notifications
unsigned long lastNotifyTime = 0;

// Function declarations
void onBleCommand(const String& command);
void onBleConnectionChanged(bool connected);
void updateSystemStatus();
void publishGPSData();
bool initializeModem();
void checkModemHealth();

// GPS Control functions
void handleGPSControl(const String& subCommand);
bool startGPS(GPSStartMode mode);
bool stopGPS();
void sendGPSStatus();
void sendSatelliteInfo();

// Network Control functions
void handleNetworkControl(const String& subCommand);
bool connectToNetwork();
bool disconnectFromNetwork();
void sendNetworkStatus();
void updateNetworkConfig(const String& apn, const String& user = "", const String& pass = "");

// GPS Debug functions
void handleGPSDebugCommand(const String& subCommand);
void sendDebugData(const String& data);
void sendGPSDebugInfo();
void sendGPSVerboseInfo();
void sendRawNMEAData();
void addToDebugHistory(const String& data);
void sendDebugHistory();
void startContinuousDebug();
void stopContinuousDebug();
void runGPSDiagnostics();

// Helper function for rate-limited BLE notifications
void safeBleNotify(const String& message) {
    BLE& ble = BLE::getInstance();
    if (ble.isConnected()) {
        unsigned long now = millis();
        if (now - lastNotifyTime >= BLE_NOTIFY_DELAY) {
            lastNotifyTime = now;
            ble.notify(message);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== ESP32 Paw System Starting ===");
    
    // Initialize LED indicators
    initLED();
    updateLED(BATTERY_LED, RED, 32);
    updateLED(NETWORK_LED, RED, 32);
    updateLED(LOCATION_LED, RED, 32);
    delay(500);
    
    // Boot animation
    updateLED(BATTERY_LED, GREEN, 64);
    delay(200);
    updateLED(NETWORK_LED, GREEN, 64);
    delay(200);
    updateLED(LOCATION_LED, GREEN, 64);
    delay(500);
    
    // Initialize BLE
    BLE& ble = BLE::getInstance();
    ble.setConnectionCallback(onBleConnectionChanged);
    ble.setCommandCallback(onBleCommand);
    ble.init("ESP32_Paw_Tracker", true);
    
    String mac = ble.getMacAddress();
    Serial.print("BLE MAC: ");
    Serial.println(mac);
    
    // Initialize Modem
    if (!initializeModem()) {
        Serial.println("Modem initialization failed!");
        updateLED(NETWORK_LED, RED, BRIGHTNESS);
    }
    
    currentState = STATE_READY;
    updateSystemStatus();
    
    Serial.println("System Ready!");
}

void loop() {
    static unsigned long lastHealthCheck = 0;
    
    // Update GPS data if enabled - with longer interval
    if (gpsEnabled && currentState == STATE_READY && !continuousDebugMode) {
        if (millis() - lastGPSUpdate > 15000) {  // Increased to 15 seconds
            lastGPSUpdate = millis();
            
            GPSData gps;
            if (ModemManager::getInstance().getGPSInfo(gps)) {
                if (gps.valid) {
                    publishGPSData();
                    updateLED(LOCATION_LED, BLUE, BRIGHTNESS);
                } else {
                    updateLED(LOCATION_LED, YELLOW, 32);
                }
            }
        }
    } else if (!gpsEnabled) {
        updateLED(LOCATION_LED, RED, 32);
    }
    
    // Continuous debug mode updates - with longer interval
    if (continuousDebugMode && gpsEnabled && millis() - lastDebugUpdate > 10000) {  // Increased to 10 seconds
        lastDebugUpdate = millis();
        
        switch(currentDebugLevel) {
            case DEBUG_BASIC:
                sendGPSDebugInfo();
                break;
            case DEBUG_VERBOSE:
                sendGPSVerboseInfo();
                break;
            case DEBUG_RAW:
                sendRawNMEAData();
                break;
            default:
                break;
        }
    }
    
    // Auto-reconnect network if needed - with longer interval
    if (networkEnabled && netConfig.autoReconnect) {
        ModemManager& modem = ModemManager::getInstance();
        
        if (millis() - lastNetworkCheck > 60000) {  // Increased to 60 seconds
            lastNetworkCheck = millis();
            
            if (!modem.isGPRSConnected() && modem.isNetworkAvailable()) {
                Serial.println("Auto-reconnecting GPRS...");
                if (connectToNetwork()) {
                    safeBleNotify("Network auto-reconnected");
                }
            }
        }
    }
    
    // Send periodic status updates if BLE connected - less frequent
    if (bleConnected && millis() - lastStatusUpdate > 60000) {  // Changed to 60 seconds
        lastStatusUpdate = millis();
        updateSystemStatus();
    }
    
    // Check modem health every 2 minutes
    if (millis() - lastHealthCheck > 120000) {  // Increased to 2 minutes
        lastHealthCheck = millis();
        checkModemHealth();
    }
    
    delay(100);  // Increased delay to reduce CPU usage
}

//=============================================================================
// Modem Initialization
//=============================================================================

bool initializeModem() {
    Serial.println("\n--- Initializing Modem ---");
    updateLED(NETWORK_LED, YELLOW, BRIGHTNESS);
    
    ModemManager& modem = ModemManager::getInstance();
    
    // Start modem manager
    if (!modem.begin(115200)) {
        Serial.println("Failed to start ModemManager");
        return false;
    }
    
    // Power on GSM
    modem.enableGSM();
    delay(100);
    modem.powerOnModem();
    delay(3000);
    
    // Initialize modem
    if (!modem.initModem()) {
        Serial.println("Modem init failed");
        return false;
    }
    
    // Get modem info
    String info = modem.getModemInfo();
    Serial.println("Modem Info: " + info);
    
    // Wait for network
    currentState = STATE_NETWORK_SEARCH;
    updateSystemStatus();
    
    if (!modem.waitForNetwork(60000)) {
        Serial.println("Network not found!");
        updateLED(NETWORK_LED, RED, BRIGHTNESS);
        return false;
    }
    
    int signalStrength = modem.getSignalStrength();
    Serial.print("Signal strength: ");
    Serial.println(signalStrength);
    
    // Connect to GPRS if network is enabled
    if (networkEnabled) {
        if (!connectToNetwork()) {
            Serial.println("GPRS connection failed!");
            return false;
        }
    }
    
    // Initialize GPS if enabled
    if (gpsEnabled) {
        if (!startGPS(GPS_HOT_START)) {
            Serial.println("GPS initialization failed!");
            return false;
        }
    }
    
    updateLED(NETWORK_LED, GREEN, BRIGHTNESS);
    Serial.println("Modem fully initialized!");
    return true;
}

//=============================================================================
// GPS Control Functions
//=============================================================================

void handleGPSControl(const String& subCommand) {
    if (subCommand == "START" || subCommand == "ON") {
        if (startGPS(GPS_HOT_START)) {
            safeBleNotify("GPS started (HOT start)");
            gpsEnabled = true;
        } else {
            safeBleNotify("GPS start failed");
        }
    }
    else if (subCommand == "START_COLD") {
        if (startGPS(GPS_COLD_START)) {
            safeBleNotify("GPS started (COLD start - may take 1-2 minutes)");
            gpsEnabled = true;
        } else {
            safeBleNotify("GPS cold start failed");
        }
    }
    else if (subCommand == "START_WARM") {
        if (startGPS(GPS_WARM_START)) {
            safeBleNotify("GPS started (WARM start)");
            gpsEnabled = true;
        } else {
            safeBleNotify("GPS warm start failed");
        }
    }
    else if (subCommand == "START_HOT") {
        if (startGPS(GPS_HOT_START)) {
            safeBleNotify("GPS started (HOT start)");
            gpsEnabled = true;
        } else {
            safeBleNotify("GPS hot start failed");
        }
    }
    else if (subCommand == "STOP" || subCommand == "OFF") {
        if (stopGPS()) {
            safeBleNotify("GPS stopped");
            gpsEnabled = false;
        } else {
            safeBleNotify("GPS stop failed");
        }
    }
    else if (subCommand == "STATUS") {
        sendGPSStatus();
    }
    else if (subCommand == "SAT") {
        sendSatelliteInfo();
    }
    else if (subCommand == "RESTART") {
        safeBleNotify("Restarting GPS...");
        stopGPS();
        delay(1000);
        if (startGPS(GPS_COLD_START)) {
            safeBleNotify("GPS restarted (COLD start)");
            gpsEnabled = true;
        } else {
            safeBleNotify("GPS restart failed");
        }
    }
    else {
        safeBleNotify("GPS commands: START, START_COLD, START_WARM, START_HOT, STOP, STATUS, SAT, RESTART");
    }
}

bool startGPS(GPSStartMode mode) {
    ModemManager& modem = ModemManager::getInstance();
    
    String modeStr;
    bool success = false;
    
    // First, ensure GPS is properly powered off
    TinyGsm* modemDevice = modem.getModem();
    if (modemDevice) {
        modemDevice->sendAT(GF("+CGNSSPWR=0"));
        delay(500);
        modemDevice->waitResponse(2000);
    }
    
    switch(mode) {
        case GPS_COLD_START:
            modeStr = "COLD";
            success = modem.initGPS(false);
            break;
        case GPS_WARM_START:
            modeStr = "WARM";
            modem.stopGPS();
            delay(500);
            success = modem.initGPS(true);
            break;
        case GPS_HOT_START:
            modeStr = "HOT";
            success = modem.initGPS(true);
            break;
    }
    
    if (success) {
        Serial.println("GPS " + modeStr + " start successful");
        gpsEnabled = true;
        updateLED(LOCATION_LED, YELLOW, 32);
    } else {
        Serial.println("GPS " + modeStr + " start failed");
    }
    
    return success;
}

bool stopGPS() {
    ModemManager& modem = ModemManager::getInstance();
    TinyGsm* modemDevice = modem.getModem();
    
    if (!modemDevice) return false;
    
    modemDevice->sendAT(GF("+CGNSSPWR=0"));
    bool success = modemDevice->waitResponse(5000L);
    
    if (success) {
        Serial.println("GPS stopped successfully");
        gpsEnabled = false;
        updateLED(LOCATION_LED, RED, 32);
    } else {
        Serial.println("GPS stop failed");
    }
    
    return success;
}

void sendGPSStatus() {
    ModemManager& modem = ModemManager::getInstance();
    
    String status = "GPS Enabled: " + String(gpsEnabled ? "YES" : "NO");
    safeBleNotify(status);
    
    if (gpsEnabled) {
        GPSData gps;
        if (modem.getGPSInfo(gps)) {
            if (gps.valid) {
                safeBleNotify("Fix: YES, Lat: " + String(gps.latitude, 6) + ", Lon: " + String(gps.longitude, 6));
            } else {
                safeBleNotify("Fix: NO - Searching for satellites...");
            }
        } else {
            safeBleNotify("Error reading GPS data");
        }
    }
}

void sendSatelliteInfo() {
    ModemManager& modem = ModemManager::getInstance();
    TinyGsm* modemDevice = modem.getModem();
    
    if (modemDevice && gpsEnabled) {
        modemDevice->sendAT(GF("+CGNSSINFO"));
        String response;
        
        if (modemDevice->waitResponse(5000L, response)) {
            safeBleNotify("SAT Info: " + response);
        } else {
            safeBleNotify("No satellite data");
        }
    } else {
        safeBleNotify("GPS not available");
    }
}

//=============================================================================
// Network Control Functions
//=============================================================================

void handleNetworkControl(const String& subCommand) {
    if (subCommand == "CONNECT" || subCommand == "ON") {
        if (connectToNetwork()) {
            safeBleNotify("Network connected");
            networkEnabled = true;
        } else {
            safeBleNotify("Network connection failed");
        }
    }
    else if (subCommand == "DISCONNECT" || subCommand == "OFF") {
        if (disconnectFromNetwork()) {
            safeBleNotify("Network disconnected");
            networkEnabled = false;
        } else {
            safeBleNotify("Network disconnect failed");
        }
    }
    else if (subCommand == "STATUS") {
        sendNetworkStatus();
    }
    else if (subCommand == "RECONNECT") {
        safeBleNotify("Manual reconnecting...");
        disconnectFromNetwork();
        delay(1000);
        if (connectToNetwork()) {
            safeBleNotify("Network reconnected");
        } else {
            safeBleNotify("Network reconnection failed");
        }
    }
    else if (subCommand == "AUTO_ON") {
        netConfig.autoReconnect = true;
        safeBleNotify("Auto-reconnect ENABLED");
    }
    else if (subCommand == "AUTO_OFF") {
        netConfig.autoReconnect = false;
        safeBleNotify("Auto-reconnect DISABLED");
    }
    else if (subCommand.startsWith("SET_APN:")) {
        String apnPart = subCommand.substring(9);
        int firstColon = apnPart.indexOf(':');
        
        if (firstColon != -1) {
            String newApn = apnPart.substring(0, firstColon);
            String newUser = apnPart.substring(firstColon + 1);
            updateNetworkConfig(newApn, newUser, "");
            safeBleNotify("APN updated to: " + newApn);
        }
    }
    else {
        safeBleNotify("Network: CONNECT, DISCONNECT, STATUS, RECONNECT, AUTO_ON, AUTO_OFF");
    }
}

bool connectToNetwork() {
    ModemManager& modem = ModemManager::getInstance();
    
    if (!networkEnabled) {
        Serial.println("Network is disabled");
        return false;
    }
    
    Serial.print("Connecting to APN: ");
    Serial.println(netConfig.apn);
    
    updateLED(NETWORK_LED, YELLOW, BRIGHTNESS);
    
    bool success = modem.connectGPRS(netConfig.apn, netConfig.user, netConfig.pass);
    
    if (success) {
        String ip = modem.getLocalIP();
        Serial.println("Connected! IP: " + ip);
        updateLED(NETWORK_LED, GREEN, BRIGHTNESS);
        safeBleNotify("Network connected");
        currentState = STATE_READY;
    } else {
        Serial.println("Connection failed!");
        updateLED(NETWORK_LED, RED, BRIGHTNESS);
        safeBleNotify("Network connection failed");
    }
    
    return success;
}

bool disconnectFromNetwork() {
    ModemManager& modem = ModemManager::getInstance();
    
    if (modem.disconnectGPRS()) {
        Serial.println("Network disconnected");
        updateLED(NETWORK_LED, YELLOW, 32);
        return true;
    }
    
    return false;
}

void sendNetworkStatus() {
    ModemManager& modem = ModemManager::getInstance();
    
    String status = "Network: " + String(modem.isGPRSConnected() ? "Connected" : "Disconnected");
    status += ", Signal: " + String(modem.getSignalStrength());
    status += ", APN: " + netConfig.apn;
    safeBleNotify(status);
}

void updateNetworkConfig(const String& apn, const String& user, const String& pass) {
    netConfig.apn = apn;
    netConfig.user = user;
    netConfig.pass = pass;
    
    Serial.println("Network config updated: " + netConfig.apn);
}

//=============================================================================
// GPS Debug Functions
//=============================================================================

void handleGPSDebugCommand(const String& subCommand) {
    if (!gpsEnabled && subCommand != "ON" && subCommand != "START") {
        safeBleNotify("GPS is disabled. Use 'GPS START' first");
        return;
    }
    
    if (subCommand == "HELP" || subCommand == "?") {
        safeBleNotify("GPS Debug: ON, VERBOSE, RAW, OFF, ONCE, HISTORY");
    }
    else if (subCommand == "ON") {
        currentDebugLevel = DEBUG_BASIC;
        if (!continuousDebugMode) startContinuousDebug();
        safeBleNotify("GPS Debug: BASIC mode enabled");
        sendGPSDebugInfo();
    }
    else if (subCommand == "VERBOSE") {
        currentDebugLevel = DEBUG_VERBOSE;
        if (!continuousDebugMode) startContinuousDebug();
        safeBleNotify("GPS Debug: VERBOSE mode enabled");
        sendGPSVerboseInfo();
    }
    else if (subCommand == "RAW") {
        currentDebugLevel = DEBUG_RAW;
        if (!continuousDebugMode) startContinuousDebug();
        safeBleNotify("GPS Debug: RAW mode enabled");
        sendRawNMEAData();
    }
    else if (subCommand == "OFF") {
        stopContinuousDebug();
        safeBleNotify("GPS Debug: DISABLED");
    }
    else if (subCommand == "ONCE") {
        sendGPSVerboseInfo();
    }
    else if (subCommand == "HISTORY") {
        sendDebugHistory();
    }
    else if (subCommand == "CONTINUOUS") {
        if (continuousDebugMode) {
            stopContinuousDebug();
        } else {
            startContinuousDebug();
        }
    }
    else {
        safeBleNotify("Unknown debug command");
    }
}

void sendDebugData(const String& data) {
    safeBleNotify("DEBUG: " + data);
    addToDebugHistory(data);
}

void sendGPSDebugInfo() {
    ModemManager& modem = ModemManager::getInstance();
    GPSData gps;
    
    if (modem.getGPSInfo(gps)) {
        String debug = "Fix:" + String(gps.valid ? "YES" : "NO") +
                      ",Lat:" + String(gps.latitude, 6) +
                      ",Lon:" + String(gps.longitude, 6);
        sendDebugData(debug);
    } else {
        sendDebugData("Failed to read GPS");
    }
}

void sendGPSVerboseInfo() {
    ModemManager& modem = ModemManager::getInstance();
    GPSData gps;
    
    if (modem.getGPSInfo(gps)) {
        if (gps.valid) {
            sendDebugData("GPS Fix: Lat=" + String(gps.latitude, 6) + 
                         " Lon=" + String(gps.longitude, 6) +
                         " Alt=" + String(gps.altitude, 1) + "m");
        } else {
            sendDebugData("GPS: No fix, searching...");
        }
    } else {
        sendDebugData("ERROR: Cannot read GPS data");
    }
}

void sendRawNMEAData() {
    ModemManager& modem = ModemManager::getInstance();
    TinyGsm* modemDevice = modem.getModem();
    
    if (modemDevice) {
        modemDevice->sendAT(GF("+CGPSINFO"));
        String response;
        
        if (modemDevice->waitResponse(5000L, response)) {
            sendDebugData("RAW: " + response);
        } else {
            sendDebugData("No GPS response");
        }
    } else {
        sendDebugData("Modem not available");
    }
}

void addToDebugHistory(const String& data) {
    debugHistory[debugHistoryIndex].timestamp = millis();
    debugHistory[debugHistoryIndex].data = data;
    debugHistoryIndex = (debugHistoryIndex + 1) % MAX_DEBUG_HISTORY;
    if (debugHistoryCount < MAX_DEBUG_HISTORY) debugHistoryCount++;
}

void sendDebugHistory() {
    if (debugHistoryCount == 0) {
        safeBleNotify("No debug history");
        return;
    }
    
    for (int i = 0; i < debugHistoryCount; i++) {
        int idx = (debugHistoryIndex - debugHistoryCount + i + MAX_DEBUG_HISTORY) % MAX_DEBUG_HISTORY;
        String entry = "[" + String(debugHistory[idx].timestamp / 1000) + "s] " + debugHistory[idx].data;
        safeBleNotify(entry);
        delay(200);  // Delay between history entries
    }
}

void startContinuousDebug() {
    continuousDebugMode = true;
    lastDebugUpdate = millis();
    sendDebugData("Continuous debug STARTED");
}

void stopContinuousDebug() {
    continuousDebugMode = false;
    currentDebugLevel = DEBUG_OFF;
    sendDebugData("Continuous debug STOPPED");
}

void runGPSDiagnostics() {
    ModemManager& modem = ModemManager::getInstance();
    
    safeBleNotify("=== GPS DIAGNOSTICS ===");
    safeBleNotify("GPS Enabled: " + String(gpsEnabled ? "YES" : "NO"));
    
    if (modem.isModemAlive()) {
        safeBleNotify("Modem: OK");
    } else {
        safeBleNotify("Modem: ERROR");
    }
    
    GPSData gps;
    if (modem.getGPSInfo(gps)) {
        if (gps.valid) {
            safeBleNotify("GPS Fix: YES");
            safeBleNotify("Lat: " + String(gps.latitude, 6));
            safeBleNotify("Lon: " + String(gps.longitude, 6));
        } else {
            safeBleNotify("GPS Fix: NO - No satellites");
        }
    } else {
        safeBleNotify("GPS: Communication error");
    }
    
    safeBleNotify("=== END DIAGNOSTICS ===");
}

//=============================================================================
// System Status
//=============================================================================

void updateSystemStatus() {
    ModemManager& modem = ModemManager::getInstance();
    
    String status = "Status: BLE=" + String(bleConnected ? "1" : "0") +
                   " GPS=" + String(gpsEnabled ? "1" : "0") +
                   " Net=" + String(modem.isGPRSConnected() ? "1" : "0") +
                   " Sig=" + String(modem.getSignalStrength());
    
    float voltage = modem.getBatteryVoltage();
    if (voltage > 0) {
        status += " Batt=" + String(voltage, 1) + "V";
        
        if (voltage < 3.5) {
            updateLED(BATTERY_LED, RED, BRIGHTNESS);
        } else if (voltage < 3.7) {
            updateLED(BATTERY_LED, YELLOW, BRIGHTNESS);
        } else {
            updateLED(BATTERY_LED, GREEN, BRIGHTNESS);
        }
    }
    
    safeBleNotify(status);
    Serial.println(status);
}

void checkModemHealth() {
    ModemManager& modem = ModemManager::getInstance();
    
    if (!modem.isModemAlive()) {
        Serial.println("Modem not responding!");
        updateLED(NETWORK_LED, RED, BRIGHTNESS);
        safeBleNotify("WARNING: Modem not responding");
    } else if (networkEnabled && !modem.isGPRSConnected() && modem.isNetworkAvailable()) {
        Serial.println("GPRS lost, reconnecting...");
        updateLED(NETWORK_LED, YELLOW, BRIGHTNESS);
        
        if (connectToNetwork()) {
            updateLED(NETWORK_LED, GREEN, BRIGHTNESS);
            safeBleNotify("GPRS reconnected");
        }
    }
}

//=============================================================================
// GPS Functions
//=============================================================================

void publishGPSData() {
    GPSData gps;
    if (!ModemManager::getInstance().getGPSInfo(gps)) {
        Serial.println("Failed to get GPS data");
        return;
    }
    
    if (!gps.valid) {
        Serial.println("No GPS fix yet");
        return;
    }
    
    String gpsString = String(gps.latitude, 6) + "," +
                      String(gps.longitude, 6) + "," +
                      String(gps.altitude, 1) + "," +
                      String(gps.speed, 1);
    
    safeBleNotify("GPS:" + gpsString);
    
    Serial.println("GPS: Lat=" + String(gps.latitude, 6) + 
                   " Lon=" + String(gps.longitude, 6) +
                   " Alt=" + String(gps.altitude, 1) + "m");
}

//=============================================================================
// BLE Callbacks
//=============================================================================

void onBleCommand(const String& command) {
    // Rate limit command processing
    unsigned long now = millis();
    if (now - lastBLECommand < 500) {
        return;  // Ignore commands that come too fast
    }
    lastBLECommand = now;
    
    Serial.println("BLE Command: " + command);
    BLE& ble = BLE::getInstance();
    ModemManager& modem = ModemManager::getInstance();
    
    if (command == "HELP") {
        safeBleNotify("Commands: STATUS, GPS, GPS_CONTROL, NETWORK, GPS_DEBUG, GPS_DIAG, BATTERY, RESET_MODEM");
    }
    else if (command == "STATUS") {
        updateSystemStatus();
    }
    else if (command == "GPS") {
        GPSData gps;
        if (modem.getGPSInfo(gps) && gps.valid) {
            safeBleNotify("GPS: " + String(gps.latitude, 6) + "," + String(gps.longitude, 6));
        } else {
            safeBleNotify("GPS: No fix");
        }
    }
    else if (command.startsWith("GPS_CONTROL")) {
        String subCommand = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex != -1) {
            subCommand = command.substring(spaceIndex + 1);
            subCommand.toUpperCase();
        }
        handleGPSControl(subCommand);
    }
    else if (command.startsWith("NETWORK")) {
        String subCommand = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex != -1) {
            subCommand = command.substring(spaceIndex + 1);
            subCommand.toUpperCase();
        }
        handleNetworkControl(subCommand);
    }
    else if (command.startsWith("GPS_DEBUG")) {
        String subCommand = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex != -1) {
            subCommand = command.substring(spaceIndex + 1);
            subCommand.toUpperCase();
        }
        handleGPSDebugCommand(subCommand);
    }
    else if (command == "GPS_DIAG") {
        runGPSDiagnostics();
    }
    else if (command == "BATTERY") {
        float voltage = modem.getBatteryVoltage();
        safeBleNotify("Battery: " + String(voltage, 2) + "V");
    }
    else if (command == "RESET_MODEM") {
        safeBleNotify("Resetting modem...");
        modem.end();
        delay(1000);
        if (initializeModem()) {
            safeBleNotify("Modem reset complete");
        } else {
            safeBleNotify("Modem reset failed");
        }
    }
    else {
        safeBleNotify("Unknown command. Send HELP");
    }
}

void onBleConnectionChanged(bool connected) {
    bleConnected = connected;
    
    if (connected) {
        Serial.println("BLE Client Connected");
        updateLED(NETWORK_LED, GREEN, BRIGHTNESS);
        
        safeBleNotify("Welcome to ESP32 Tracker");
        safeBleNotify("Send HELP for commands");
    } else {
        Serial.println("BLE Client Disconnected");
        updateLED(NETWORK_LED, GREEN, 32);
        
        if (continuousDebugMode) {
            stopContinuousDebug();
        }
    }
    
    updateSystemStatus();
}