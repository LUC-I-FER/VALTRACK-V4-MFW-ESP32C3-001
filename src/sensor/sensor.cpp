#include "sensor.h"
#include <Wire.h>

#define LIS3DH_ADDRESS       0x19
#define REG_WHO_AM_I         0x0F
#define REG_CTRL_REG1        0x20
#define REG_CTRL_REG2        0x21
#define REG_CTRL_REG3        0x22
#define REG_CTRL_REG4        0x23
#define REG_CTRL_REG5        0x24
#define REG_CTRL_REG6        0x25
#define REG_OUT_X_L          0x28

sensor& sensor::getInstance() {
    static sensor instance;
    return instance;
}

uint8_t sensor::readReg(uint8_t reg) {
    Wire.beginTransmission(LIS3DH_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(LIS3DH_ADDRESS, (uint8_t)1);
    return Wire.read();
}

void sensor::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(LIS3DH_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

int16_t sensor::readAxis(uint8_t lowReg) {
    uint8_t low = readReg(lowReg);
    uint8_t high = readReg(lowReg + 1);
    return (int16_t)((high << 8) | low);  // corrected shift from <<1 to <<8
}

bool sensor::init(uint8_t i2c_sda, uint8_t i2c_scl) {
    Wire.begin(i2c_sda, i2c_scl);
    delay(10);

    uint8_t who = readReg(REG_WHO_AM_I);
    present = (who == 0x33);

    if (!present) {
        Serial.println("[SENSOR] LIS3DH not detected!");
        return false;
    }

    writeReg(REG_CTRL_REG1, 0x57);
    writeReg(REG_CTRL_REG4, 0x08);
    delay(10);
    writeReg(REG_CTRL_REG2, 0x05);
    writeReg(REG_CTRL_REG3, 0x40);
    writeReg(REG_CTRL_REG5, 0x08);
    writeReg(REG_CTRL_REG6, 0x02);

    Serial.println("[SENSOR] LIS3DH initialised.");
    return true;
}

bool sensor::read(sensorData& data) {
    if (!present) return false;
    if (sleeping) wake();

    data.x = readAxis(REG_OUT_X_L);
    data.y = readAxis(REG_OUT_X_L + 2);
    data.z = readAxis(REG_OUT_X_L + 4);
    data.timestamp = millis();
    return true;
}

bool sensor::isPresent() const {
    return present;
}

void sensor::sleep() {
    if (sleeping) return;
    writeReg(REG_CTRL_REG1, 0x00);
    sleeping = true;
}

void sensor::wake() {
    if (!sleeping) return;
    writeReg(REG_CTRL_REG1, 0x57);
    sleeping = false;
}