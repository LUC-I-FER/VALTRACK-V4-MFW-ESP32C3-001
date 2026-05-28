#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>

#ifdef MQTT_KEEPALIVE
#undef MQTT_KEEPALIVE
#endif
#ifdef MQTT_SOCKET_TIMEOUT
#undef MQTT_SOCKET_TIMEOUT
#endif

// Default MQTT Settings
#define MQTT_KEEPALIVE 30
#define MQTT_SOCKET_TIMEOUT 10
#define MQTT_RECONNECT_DELAY 5000

// Callback types
using MqttMessageCallback = std::function<void(String topic, String payload)>;
using MqttConnectionCallback = std::function<void(bool connected)>;

class MqttManager {
private:
    // Singleton instance
    static MqttManager* instance;
    
    // Core components
    TinyGsmClient* gsmClient;
    PubSubClient* mqttClient;
    
    // Configuration
    String server;
    int port;
    String username;
    String password;
    String clientId;
    
    // State
    bool connected;
    bool autoReconnect;
    unsigned long lastReconnectAttempt;
    
    // Callbacks
    MqttMessageCallback messageCb;
    MqttConnectionCallback connectionCb;
    
    // Private methods
    MqttManager();
    ~MqttManager();
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;
    
    void callbackHandler(char* topic, byte* payload, unsigned int length);
    bool reconnect();
    String generateClientId();
    
public:
    // Singleton access
    static MqttManager& getInstance();
    static void destroyInstance();
    
    // Initialization
    bool begin(TinyGsmClient& client);
    void end();
    
    // Configuration
    void setServer(const String& broker, int brokerPort);
    void setCredentials(const String& user, const String& pass);
    void setClientId(const String& id);
    
    // Callbacks
    void onMessage(MqttMessageCallback callback);
    void onConnection(MqttConnectionCallback callback);
    
    // Connection management
    bool connect();
    bool disconnect();
    bool isConnected() const { return connected; }
    void loop();
    void setAutoReconnect(bool enable);
    
    // Publish (returns true if successful)
    bool publish(const String& topic, const String& payload, bool retained = false);
    bool publish(const String& topic, const JsonDocument& doc, bool retained = false);
    bool publish(const String& topic, int value, bool retained = false);
    bool publish(const String& topic, float value, bool retained = false);
    bool publish(const String& topic, double value, bool retained = false);
    
    // Subscribe
    bool subscribe(const String& topic, int qos = 0);
    bool unsubscribe(const String& topic);
    
    // Status
    String getClientId() const { return clientId; }
    String getServer() const { return server; }
    int getPort() const { return port; }
    int getState() const { return mqttClient ? mqttClient->state() : -999; }
    String getStateString();
};

#endif