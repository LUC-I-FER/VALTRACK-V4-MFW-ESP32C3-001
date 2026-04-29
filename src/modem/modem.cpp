#include "modem.h"
#include <PubSubClient.h>

// ================= GLOBALS =================

static HardwareSerial SerialAT(1);
static TinyGsm modem(SerialAT);
static Stream* dbg = &Serial;

static TinyGsmClient gsmClient(modem);
static PubSubClient mqtt(gsmClient);

static uint8_t modem_state = MODEM_STATE_OFF;

// ================= DEBUG =================

#define DBG(x)   dbg->println(x)
#define DBG2(x,y){ dbg->print(x); dbg->println(y); }

// ================= MQTT BUFFERS =================

static String mqtt_last_topic = "";
static String mqtt_last_payload = "";
static bool mqtt_new_data = false;

// ================= FORWARD DECL =================
static void mqtt_callback(char* topic, byte* payload, unsigned int len);

// ================= LOW LEVEL =================

static void modem_gpio_init() {
    pinMode(MODEM_PWRKEY, OUTPUT);
    pinMode(MODEM_GSMKEY, OUTPUT);

    digitalWrite(MODEM_PWRKEY, HIGH);
    digitalWrite(MODEM_GSMKEY, LOW);
}

static void modem_uart_init() {
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(300);
}

static void modem_power_on() {
    digitalWrite(MODEM_GSMKEY, HIGH);
    delay(200);

    digitalWrite(MODEM_PWRKEY, LOW);
    delay(1000);
    digitalWrite(MODEM_PWRKEY, HIGH);

    DBG("[MODEM] Powering ON...");
    delay(8000);
}

static bool modem_wait_at(uint32_t timeout = 10000) {
    uint32_t start = millis();

    while (millis() - start < timeout) {
        if (modem.testAT()) {
            DBG("[MODEM] AT OK");
            return true;
        }
        delay(500);
    }

    DBG("[MODEM] AT FAILED");
    return false;
}

static bool modem_check_sim() {
    int sim = modem.getSimStatus();

    if (sim == 3) {
        DBG("[MODEM] SIM READY");
        return true;
    }

    // If SIM requires PIN (status 1), attempt to unlock
    if (sim == 1) {
        DBG("[MODEM] SIM requires PIN, attempting unlock...");
        // Use a PIN define; you need to define MODEM_SIM_PIN somewhere, e.g., in modem.h
        #ifdef MODEM_SIM_PIN
        if (modem.simUnlock(MODEM_SIM_PIN)) {
            DBG("[MODEM] SIM unlocked successfully");
            delay(1000); // Give time for SIM to become ready
            // Re-check status
            sim = modem.getSimStatus();
            if (sim == 3) {
                DBG("[MODEM] SIM READY after unlock");
                return true;
            } else {
                DBG2("[MODEM] SIM still not ready after unlock: ", sim);
                return false;
            }
        } else {
            DBG("[MODEM] SIM unlock failed - wrong PIN?");
            return false;
        }
        #else
        DBG("[MODEM] SIM PIN required but no PIN defined (MODEM_SIM_PIN)");
        return false;
        #endif
    }

    DBG2("[MODEM] SIM ERROR: ", sim);
    return false;
}

// ================= CORE =================

bool modem_init() {

    if (modem_state & MODEM_STATE_READY) {
        DBG("[MODEM] Already initialized");
        return true;
    }

    DBG("===== MODEM INIT START =====");

    modem_gpio_init();
    modem_uart_init();

    // 🔁 Try full init sequence twice
    for (int attempt = 0; attempt < 2; attempt++) {

        DBG2("[MODEM] INIT ATTEMPT: ", attempt + 1);

        modem_power_on();
        modem_state |= MODEM_STATE_POWERED;

        // ---------- AT CHECK ----------
        if (!modem_wait_at()) {
            DBG("[MODEM] AT failed, retrying power...");
            continue;
        }

        DBG2("[MODEM] INFO: ", modem.getModemInfo());

        // ---------- SIM CHECK (with retry) ----------
        bool sim_ok = false;
        for (int i = 0; i < 10; i++) {
            if (modem_check_sim()) {
                sim_ok = true;
                break;
            }
            DBG("[MODEM] Waiting SIM...");
            delay(1000);
        }

        if (!sim_ok) {
            DBG("[MODEM] SIM failed, retrying init...");
            continue;
        }

        // ---------- NETWORK CHECK (with retry) ----------
        bool net_ok = false;
        for (int i = 0; i < 15; i++) {
            if (modem_wait_for_network()) {
                net_ok = true;
                break;
            }
            DBG("[MODEM] Waiting network...");
            delay(2000);
        }

        if (!net_ok) {
            DBG("[MODEM] Network failed, retrying init...");
            continue;
        }

        // ✅ SUCCESS
        modem_state |= MODEM_STATE_READY;

        DBG("===== MODEM INIT DONE =====");
        return true;
    }

    // ❌ FAILED after retries
    DBG("[MODEM] INIT FAILED COMPLETELY");
    return false;
}

// ================= STATUS =================

bool modem_is_ready() {

    if (!(modem_state & MODEM_STATE_READY)) return false;
    if (!modem.testAT()) return false;
    if (modem.getSimStatus() != 3) return false;
    if (!modem.isNetworkConnected()) return false;

    return true;
}

// ================= NETWORK =================

bool modem_wait_for_network(uint32_t timeout) {

    DBG("[MODEM] Waiting network...");

    uint32_t start = millis();

    while (millis() - start < timeout) {

        int reg = modem.getRegistrationStatus();
        DBG2("[MODEM] REG: ", reg);

        if (reg == 1 || reg == 5) {
            DBG("[MODEM] Network OK");
            modem_state |= MODEM_STATE_NETWORK;
            return true;
        }

        delay(1000);
    }

    DBG("[MODEM] Network FAIL");
    modem_state &= ~MODEM_STATE_NETWORK;
    return false;
}

// ================= GPRS =================

bool modem_connect_gprs(uint32_t timeout) {

    if (!(modem_state & MODEM_STATE_NETWORK)) {
        if (!modem_wait_for_network(timeout)) return false;
    }

    if (modem.isGprsConnected()) {
        modem_state |= MODEM_STATE_GPRS;
        return true;
    }

    DBG2("[MODEM] APN: ", MODEM_APN);

    if (!modem.gprsConnect(MODEM_APN, "", "")) {
        modem_state &= ~MODEM_STATE_GPRS;
        return false;
    }

    uint32_t start = millis();

    while (millis() - start < timeout) {
        if (modem.isGprsConnected()) {
            DBG("[MODEM] GPRS OK");
            modem_state |= MODEM_STATE_GPRS;
            return true;
        }
        delay(1000);
    }

    modem_state &= ~MODEM_STATE_GPRS;
    return false;
}

void modem_disconnect_gprs() {

    if (!modem.isGprsConnected()) return;

    modem.gprsDisconnect();
    modem_state &= ~MODEM_STATE_GPRS;
}

// ================= MQTT =================

bool modem_mqtt_start() {

    if (!(modem_state & MODEM_STATE_GPRS)) {
        if (!modem_connect_gprs()) return false;
    }

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqtt_callback);

    if (mqtt.connected()) {
        modem_state |= MODEM_STATE_MQTT;
        return true;
    }

    if (!mqtt.connect(MQTT_CLIENT_ID, MQTT_TOKEN, "")) {
        modem_state &= ~MODEM_STATE_MQTT;
        return false;
    }

    DBG("[MODEM] MQTT OK");

    modem_state |= MODEM_STATE_MQTT;
    return true;
}

bool modem_mqtt_publish(const String& topic, const String& payload) {

    if (!(modem_state & MODEM_STATE_MQTT) || !mqtt.connected()) {
        if (!modem_mqtt_start()) return false;
    }

    if (!mqtt.publish(topic.c_str(), payload.c_str())) {
        modem_state &= ~MODEM_STATE_MQTT;
        return false;
    }

    return true;
}

static void mqtt_callback(char* topic, byte* payload, unsigned int len) {

    mqtt_last_topic = String(topic);
    mqtt_last_payload = "";

    for (unsigned int i = 0; i < len; i++) {
        mqtt_last_payload += (char)payload[i];
    }

    mqtt_new_data = true;
}

String modem_mqtt_read() {

    mqtt.loop();

    if (!mqtt_new_data) return "";

    mqtt_new_data = false;
    return mqtt_last_payload;
}

// ================= GNSS =================

bool modem_gnss_enable() {

    if (!(modem_state & MODEM_STATE_READY)) return false;

    modem.sendAT("+CGNSPWR=1");
    if (modem.waitResponse(5000) != 1) return false;

    modem_state |= MODEM_STATE_GNSS;
    return true;
}

bool modem_get_location(float* lat, float* lon) {

    if (!(modem_state & MODEM_STATE_GNSS)) return false;

    modem.sendAT("+CGNSINF");

    String res;
    if (modem.waitResponse(5000, res) != 1) return false;

    int colon = res.indexOf(":");
    if (colon == -1) return false;

    String data = res.substring(colon + 1);
    data.trim();

    String tokens[10];
    int i = 0;

    while (data.length() && i < 10) {
        int c = data.indexOf(",");
        if (c == -1) { tokens[i++] = data; break; }
        tokens[i++] = data.substring(0, c);
        data = data.substring(c + 1);
    }

    if (tokens[1] != "1") return false;

    *lat = tokens[3].toFloat();
    *lon = tokens[4].toFloat();

    return true;
}

// ================= BATTERY =================

float modem_get_battery() {

    modem.sendAT("+CBC");

    String res;
    if (modem.waitResponse(3000, res) != 1) return -1;

    int colon = res.indexOf(":");
    if (colon == -1) return -1;

    String data = res.substring(colon + 1);
    data.trim();

    int comma1 = data.indexOf(",");
    int comma2 = data.indexOf(",", comma1 + 1);

    if (comma1 == -1 || comma2 == -1) return -1;

    int percent = data.substring(comma1 + 1, comma2).toInt();

    return (float)percent;
}

// ================= DEINIT =================

void modem_deinit() {

    if (modem_state & MODEM_STATE_MQTT) {
        mqtt.disconnect();
        modem_state &= ~MODEM_STATE_MQTT;
    }

    if (modem_state & MODEM_STATE_GPRS) {
        modem.gprsDisconnect();
        modem_state &= ~MODEM_STATE_GPRS;
    }

    if (modem_state & MODEM_STATE_GNSS) {
        modem.sendAT("+CGNSPWR=0");
        modem.waitResponse(3000);
        modem_state &= ~MODEM_STATE_GNSS;
    }

    modem.sendAT("+CPOWD=1");
    modem.waitResponse(10000);

    digitalWrite(MODEM_GSMKEY, LOW);

    modem_state = MODEM_STATE_OFF;

    DBG("[MODEM] DEINIT DONE");
}