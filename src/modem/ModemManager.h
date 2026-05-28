#ifndef MODEM_MANAGER_H
#define MODEM_MANAGER_H

#include <TinyGsmClient.h>
#include <Arduino.h>

#define SerialMon Serial

// Increase RX buffer
#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif

#define TINY_GSM_DEBUG SerialMon

struct GPSData {
    float latitude;
    float longitude;
    float altitude;
    float speed;
    float course;
    bool valid;
    String date;
    String time;
    
    GPSData() : latitude(0), longitude(0), altitude(0), 
                speed(0), course(0), valid(false) {}
};

class ModemManager {
private:
    static ModemManager* instance;
    
    // Hardware pins
    static constexpr int GPIO_PWRKEY = 7;
    static constexpr int GPIO_GSM_ENABLE = 10;
    static constexpr int GPIO_TPS_ENABLE = 4;
    static constexpr int GSM_RX_PIN = 1;
    static constexpr int GSM_TX_PIN = 0;
    
    TinyGsm* modem;
    TinyGsmClient* client;
    
    bool gprsConnected;
    bool gpsInitialized;
    bool networkAvailable;
    
    ModemManager();
    ~ModemManager();
    ModemManager(const ModemManager&) = delete;
    ModemManager& operator=(const ModemManager&) = delete;
    
    bool sendATCommand(const String& cmd, const String& expected, unsigned long timeout, bool verbose = false);
    String parseCGPSINFO(const String& response, GPSData& data);
    float parseBatteryVoltage(const String& response);
    
public:
    static ModemManager& getInstance();
    static void destroyInstance();
    
    // Core initialization
    bool begin(unsigned long serialBaud = 115200);
    void end();
    
    // Power management
    void enableGSM();
    void disableGSM();
    void powerOnModem();
    void powerOffModem();
    bool initModem();
    bool isModemAlive();
    
    // Network functions
    bool waitForNetwork(unsigned long timeout_ms = 30000L);
    bool isNetworkAvailable() const { return networkAvailable; }
    int getSignalStrength();
    String getModemInfo();
    
    // GPRS functions
    bool connectGPRS(const String& apn, const String& user = "", const String& pass = "");
    bool disconnectGPRS();
    bool isGPRSConnected() const { return gprsConnected; }
    String getLocalIP();
    
    // GPS functions
    bool initGPS(bool hotStart = false);
    bool stopGPS();
    bool getGPSInfo(GPSData& data);
    
    // Battery functions
    float getBatteryVoltage();
    
    // Accessors
    TinyGsmClient* getClient() { return client; }
    TinyGsm* getModem() { return modem; }
    
    // Debug
    void debugGPS();
};

#endif