// ==================== FORCE GPRS MODE ====================
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>

#include "modem/modem.h"
#include "indicator/indicator.h"
#include "mqtt/mqtt.h"

// -------------------- Configuration --------------------
const char apn[] = "vi";
const char mqtt_token[] = "Hz8ZN1ZvlOH5yZcvgKJyaC3dOrdqILdsMQkdmFUKkqUabNcVIQprNgf7Fd1Vb3ZV";
const char mqtt_topic[] = "esp32c3/accelerometer";

// -------------------- Accelerometer (LIS3DH) --------------------
#define GPIO_IIC_DATA   5
#define GPIO_IIC_CLOCK  6
#define ACCLEROMETER_I2C_ADDRESS 0x19

#define REG_CTRL_REG1  0x20
#define REG_CTRL_REG2  0x21
#define REG_CTRL_REG3  0x22
#define REG_CTRL_REG4  0x23
#define REG_CTRL_REG5  0x24
#define REG_CTRL_REG6  0x25
#define REG_INT1_CFG   0x30
#define REG_INT1_SRC   0x31
#define REG_INT1_THS   0x32
#define REG_INT1_DURATION 0x33

#define OUT_X_L        0x28
#define OUT_X_H        0x29
#define OUT_Y_L        0x2A
#define OUT_Y_H        0x2B
#define OUT_Z_L        0x2C
#define OUT_Z_H        0x2D

uint8_t I2C_RdReg(uint8_t RegisterAddress) {
    Wire.beginTransmission(ACCLEROMETER_I2C_ADDRESS);
    Wire.write(RegisterAddress);
    Wire.endTransmission();
    Wire.requestFrom(ACCLEROMETER_I2C_ADDRESS, 1);
    delay(2);
    return (uint8_t)Wire.read();
}

void I2C_WrReg(uint8_t RegisterAddress, uint8_t Data) {
    Wire.beginTransmission(ACCLEROMETER_I2C_ADDRESS);
    Wire.write(RegisterAddress);
    Wire.write(Data);
    Wire.endTransmission();
}

void initAccelerometer() {
    uint8_t VALREAD = 0;
    Wire.begin(GPIO_IIC_DATA, GPIO_IIC_CLOCK);
    VALREAD = I2C_RdReg(0x26);
    VALREAD = I2C_RdReg(0x0F);
    Serial.print("Motion Sensor = ");
    if (VALREAD == 0x33) {
        Serial.println("LIS3DH Found");
    } else {
        Serial.println("LIS3DH Not Found");
    }
    I2C_WrReg(REG_CTRL_REG1, 0x57);   // 100 Hz, XYZ enabled
    I2C_WrReg(REG_CTRL_REG4, 0x08);   // ±2g, high-res
    delay(200);
    I2C_WrReg(REG_CTRL_REG2, 0x05);
    I2C_WrReg(REG_CTRL_REG3, 0x40);
    I2C_WrReg(REG_CTRL_REG5, 0x08);
    I2C_WrReg(REG_CTRL_REG6, 0x02);
    I2C_WrReg(REG_INT1_THS, 0x18);
    I2C_WrReg(REG_INT1_DURATION, 0x00);
    I2C_WrReg(REG_INT1_CFG, 0x2A);
    for (uint8_t i = 0x07; i <= 0x3F; i++) {
        VALREAD = I2C_RdReg(i);
    }
}

int16_t readAccelAxis(uint8_t lowReg) {
    uint8_t low = I2C_RdReg(lowReg);
    uint8_t high = I2C_RdReg(lowReg + 1);
    return (int16_t)((high << 8) | low);
}

void readAccelerometer(int16_t &x, int16_t &y, int16_t &z) {
    x = readAccelAxis(OUT_X_L);
    y = readAccelAxis(OUT_Y_L);
    z = readAccelAxis(OUT_Z_L);
}

// -------------------- Setup --------------------
void setup() {
    delay(5000);
    initGPIO();
    initUARTs();
    enableGSM();
    delay(2000);
    initLED();
    initAccelerometer();
    initGSMpower();

    SerialMon.println("Starting...");
    delay(1000);

    restartModem();

    SerialMon.print("Waiting for network...");
    if (!waitForNetwork(30000L)) {
        SerialMon.println(" FAIL");
        updateLED(NETWORK_LED, RED);
        while (true) delay(1000);
    }
    SerialMon.println(" OK");
    delay(2000);

    SerialMon.print("Connecting to GPRS...");
    if (!connectGPRS(apn, "", "")) {
        SerialMon.println(" FAIL");
        updateLED(NETWORK_LED, RED);
        while (true) delay(1000);
    }
    SerialMon.println(" SUCCESS");
    delay(3000);
    updateLED(NETWORK_LED, GREEN);

    // Optional: start GPS
    modem_gnss_power();

    // Setup MQTT
    mqtt_set_broker("tcp://mqtt.flespi.io:1883", mqtt_token);
    mqtt_init();
}

// -------------------- Loop --------------------
void loop() {
    // Check GPRS
    if (!isGPRSConnected()) {
        SerialMon.println("GPRS lost. Reconnecting...");
        updateLED(NETWORK_LED, RED);
        if (!connectGPRS(apn, "", "")) {
            SerialMon.println("GPRS reconnect failed");
            delay(10000);
            return;
        }
        delay(3000);
        updateLED(NETWORK_LED, GREEN);
    }

    // Read accelerometer
    int16_t ax, ay, az;
    readAccelerometer(ax, ay, az);

    // Build JSON payload
    StaticJsonDocument<128> doc;
    doc["x"] = ax;
    doc["y"] = ay;
    doc["z"] = az;
    doc["uptime"] = millis() / 1000;
    char buffer[128];
    serializeJson(doc, buffer);
    String payload = String(buffer);

    // Publish via native MQTT
    if (mqtt_publish(mqtt_topic, payload) == 0) {
        SerialMon.println("Published: " + payload);
    } else {
        SerialMon.println("Publish failed");
        updateLED(NETWORK_LED, RED);
        delay(1000);
        updateLED(NETWORK_LED, GREEN);
    }

    // Check for incoming MQTT messages
    String incoming = mqtt_read_incoming();
    if (incoming.length() > 0) {
        // handle command (e.g., change LED color)
        if (incoming == "LED_RED") updateLED(NETWORK_LED, RED);
        else if (incoming == "LED_GREEN") updateLED(NETWORK_LED, GREEN);
    }

    // Periodically read GPS and battery
    static unsigned long lastExtra = 0;
    if (millis() - lastExtra > 60000) {
        float lat, lon;
        modem_get_gps(&lat, &lon);
        float vbat;
        modem_battery_get_status(&vbat);
        SerialMon.printf("GPS: %.6f, %.6f  Batt: %.2fV\n", lat, lon, vbat);
        lastExtra = millis();
    }

    delay(30000);
}