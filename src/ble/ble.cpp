#include "ble.h"

namespace {
    BLEServer* pServer = nullptr;
    BLECharacteristic* pCharacteristic = nullptr;
    bool deviceConnected = false;
    CommandCallback globalCallback = nullptr;

    class MyServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer* pServer) override {
            deviceConnected = true;
            Serial.println("[BLE] Device connected.");
        }
        void onDisconnect(BLEServer* pServer) override {
            deviceConnected = false;
            Serial.println("[BLE] Device disconnected. Restart advertising.");
            pServer->startAdvertising();
        }
    };

    class MyCharCallbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic* pCharacteristic) override {
            String incoming = String((char*)pCharacteristic->getData());
            incoming.trim();
            if (incoming.length() > 0 && globalCallback) {
                globalCallback(incoming);
            }
        }
    };
}

BLE& BLE::getInstance() {
    static BLE instance;
    return instance;
}

void BLE::init(const char* deviceName, bool startAdv) {
    BLEDevice::init(deviceName);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // FIX: use a different variable name; do not shadow the global pServer
    BLEService* pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTICS_UUID,
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharacteristic->addDescriptor(new BLE2902());      // FIX: addDescriptor (was addDiscriptor)
    pCharacteristic->setCallbacks(new MyCharCallbacks());

    pService->start();

    if (startAdv){
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMaxPreferred(0x12);
        BLEDevice::startAdvertising();
        Serial.println("[BLE] Initialised and advertising....");
    } else {
        Serial.println("[BLE] Initialised but advertising...");
    }

}

void BLE::notify(const String& data) {
    if (!deviceConnected) {
        Serial.println("[BLE] Device not connected - cannot notify.");
        return;
    }
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();                         // FIX: -> not -
    Serial.print("[BLE] Notified: ");
    Serial.println(data);
}

bool BLE::isConnected() const {
    return deviceConnected;
}

void BLE::setCommandCallback(CommandCallback callback) {
    globalCallback = callback;
    userCallback = callback;   // (optional, not used elsewhere)
}

void BLE::startAdvertising() {
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising started.");
}