#ifndef BLE_H
#define BLE_H

#include <Arduino.h> 
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define SERVICE_UUID         "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTICS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

typedef void (*CommandCallback)(const String& command);
typedef void (*ConnectionCallback)(bool connected);

class BLE {
    public:
        static BLE& getInstance();
        void init(const char* deviceName = "ESP32_Paw", bool startAdv = true);
        void notify(const String& data);
        bool isConnected() const;
        void setCommandCallback(CommandCallback callback);
        void setConnectionCallback(ConnectionCallback callback);
        void startAdvertising();
        String getMacAddress() const;

    private:
        BLE(){}
        ~BLE(){}
        BLE(const BLE&) = delete;
        BLE& operator=(const BLE&) = delete;

        bool connected = false;
        CommandCallback userCallback = nullptr;
        ConnectionCallback connectionCallback = nullptr;
};

#endif