#include "MqttManager.h"
#include <Arduino.h>

// Static member initialization
MqttManager* MqttManager::instance = nullptr;

//=============================================================================
// Constructor / Destructor
//=============================================================================

MqttManager::MqttManager()
    : gsmClient(nullptr)
    , mqttClient(nullptr)
    , port(1883)
    , connected(false)
    , autoReconnect(true)
    , lastReconnectAttempt(0)
    , messageCb(nullptr)
    , connectionCb(nullptr)
{
}

MqttManager::~MqttManager() {
    end();
}

//=============================================================================
// Singleton Methods
//=============================================================================

MqttManager& MqttManager::getInstance() {
    if (instance == nullptr) {
        instance = new MqttManager();
    }
    return *instance;
}

void MqttManager::destroyInstance() {
    if (instance != nullptr) {
        delete instance;
        instance = nullptr;
    }
}

//=============================================================================
// Initialization
//=============================================================================

bool MqttManager::begin(TinyGsmClient& client) {
    gsmClient = &client;
    mqttClient = new PubSubClient(*gsmClient);
    
    mqttClient->setKeepAlive(MQTT_KEEPALIVE);
    mqttClient->setSocketTimeout(MQTT_SOCKET_TIMEOUT);
    mqttClient->setCallback([this](char* topic, byte* payload, unsigned int len) {
        this->callbackHandler(topic, payload, len);
    });
    
    if (clientId.isEmpty()) {
        clientId = generateClientId();
    }
    
    Serial.println("[MQTT] Manager initialized");
    return true;
}

void MqttManager::end() {
    if (connected) {
        disconnect();
    }
    
    delete mqttClient;
    mqttClient = nullptr;
    gsmClient = nullptr;
    connected = false;
}

//=============================================================================
// Configuration
//=============================================================================

void MqttManager::setServer(const String& broker, int brokerPort) {
    server = broker;
    port = brokerPort;
    mqttClient->setServer(server.c_str(), port);
    Serial.printf("[MQTT] Server set to %s:%d\n", server.c_str(), port);
}

void MqttManager::setCredentials(const String& user, const String& pass) {
    username = user;
    password = pass;
    Serial.println("[MQTT] Credentials set");
}

void MqttManager::setClientId(const String& id) {
    clientId = id;
    Serial.printf("[MQTT] Client ID: %s\n", clientId.c_str());
}

//=============================================================================
// Callbacks
//=============================================================================

void MqttManager::onMessage(MqttMessageCallback callback) {
    messageCb = callback;
}

void MqttManager::onConnection(MqttConnectionCallback callback) {
    connectionCb = callback;
}

//=============================================================================
// Connection Management
//=============================================================================

bool MqttManager::connect() {
    if (!gsmClient) {
        Serial.println("[MQTT] Error: No GSM client");
        return false;
    }
    
    if (server.isEmpty()) {
        Serial.println("[MQTT] Error: Server not set");
        return false;
    }
    
    if (connected) {
        Serial.println("[MQTT] Already connected");
        return true;
    }
    
    Serial.printf("[MQTT] Connecting to %s:%d as %s\n", 
                  server.c_str(), port, clientId.c_str());
    
    bool success;
    if (username.length() > 0) {
        // With authentication
        success = mqttClient->connect(clientId.c_str(), 
                                      username.c_str(), 
                                      password.c_str());
    } else {
        // Without authentication
        success = mqttClient->connect(clientId.c_str());
    }
    
    if (success) {
        connected = true;
        Serial.println("[MQTT] ✓ Connected successfully!");
        
        if (connectionCb) {
            connectionCb(true);
        }
    } else {
        connected = false;
        Serial.printf("[MQTT] ✗ Connection failed! State: %d\n", mqttClient->state());
        
        if (connectionCb) {
            connectionCb(false);
        }
    }
    
    return connected;
}

bool MqttManager::disconnect() {
    if (!mqttClient) return false;
    
    Serial.println("[MQTT] Disconnecting...");
    mqttClient->disconnect();
    connected = false;
    Serial.println("[MQTT] Disconnected");
    
    if (connectionCb) {
        connectionCb(false);
    }
    
    return true;
}

void MqttManager::loop() {
    if (!mqttClient) return;
    
    if (connected) {
        if (!mqttClient->loop()) {
            Serial.println("[MQTT] Connection lost!");
            connected = false;
            
            if (connectionCb) {
                connectionCb(false);
            }
        }
    }
    
    // Auto reconnect
    if (autoReconnect && !connected && gsmClient) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= MQTT_RECONNECT_DELAY) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] Attempting reconnect...");
            connect();
        }
    }
}

void MqttManager::setAutoReconnect(bool enable) {
    autoReconnect = enable;
    Serial.printf("[MQTT] Auto-reconnect: %s\n", enable ? "ON" : "OFF");
}

bool MqttManager::reconnect() {
    return connect();
}

//=============================================================================
// Publish Methods
//=============================================================================

bool MqttManager::publish(const String& topic, const String& payload, bool retained) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot publish: Not connected");
        return false;
    }
    
    bool success = mqttClient->publish(topic.c_str(), 
                                       payload.c_str(), 
                                       retained);
    
    if (success) {
        Serial.printf("[MQTT] Published to %s: %s\n", 
                     topic.c_str(), 
                     payload.length() > 50 ? payload.substring(0, 50) + "..." : payload.c_str());
    } else {
        Serial.printf("[MQTT] Publish failed to %s\n", topic.c_str());
    }
    
    return success;
}

bool MqttManager::publish(const String& topic, const JsonDocument& doc, bool retained) {
    String payload;
    serializeJson(doc, payload);
    return publish(topic, payload, retained);
}

bool MqttManager::publish(const String& topic, int value, bool retained) {
    return publish(topic, String(value), retained);
}

bool MqttManager::publish(const String& topic, float value, bool retained) {
    char buffer[16];
    dtostrf(value, 6, 2, buffer);
    return publish(topic, String(buffer), retained);
}

bool MqttManager::publish(const String& topic, double value, bool retained) {
    char buffer[16];
    dtostrf(value, 8, 3, buffer);
    return publish(topic, String(buffer), retained);
}

//=============================================================================
// Subscribe Methods
//=============================================================================

bool MqttManager::subscribe(const String& topic, int qos) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot subscribe: Not connected");
        return false;
    }
    
    bool success = mqttClient->subscribe(topic.c_str(), qos);
    
    if (success) {
        Serial.printf("[MQTT] Subscribed to %s (QoS %d)\n", topic.c_str(), qos);
    } else {
        Serial.printf("[MQTT] Subscribe failed for %s\n", topic.c_str());
    }
    
    return success;
}

bool MqttManager::unsubscribe(const String& topic) {
    if (!connected || !mqttClient) {
        return false;
    }
    
    bool success = mqttClient->unsubscribe(topic.c_str());
    
    if (success) {
        Serial.printf("[MQTT] Unsubscribed from %s\n", topic.c_str());
    }
    
    return success;
}

//=============================================================================
// Status Methods
//=============================================================================

String MqttManager::getStateString() {
    if (!mqttClient) return "Not initialized";
    if (connected) return "Connected";
    
    switch (mqttClient->state()) {
        case -4: return "Connection lost";
        case -3: return "Connection failed";
        case -2: return "Connect failed";
        case -1: return "Disconnected";
        case 0: return "Connected";
        case 1: return "Bad protocol";
        case 2: return "ID rejected";
        case 3: return "Server unavailable";
        case 4: return "Bad credentials";
        case 5: return "Not authorized";
        default: return "Unknown";
    }
}

String MqttManager::generateClientId() {
    uint32_t chipId = ESP.getEfuseMac();
    char id[32];
    snprintf(id, sizeof(id), "esp32c3_%08X", chipId);
    return String(id);
}

//=============================================================================
// Internal Callback Handler
//=============================================================================

void MqttManager::callbackHandler(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("[MQTT] Message: %s -> %s\n", topic, message.c_str());
    
    if (messageCb) {
        messageCb(String(topic), message);
    }
}