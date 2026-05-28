#include "sensor.h"
#include <Wire.h>
#include <math.h>

#define LIS3DH_ADDRESS       0x19
#define REG_WHO_AM_I         0x0F
#define REG_CTRL_REG1        0x20
#define REG_CTRL_REG2        0x21
#define REG_CTRL_REG3        0x22
#define REG_CTRL_REG4        0x23
#define REG_CTRL_REG5        0x24
#define REG_CTRL_REG6        0x25
#define REG_OUT_X_L          0x28

#define LIS3DH_SCALE         0.061f

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
    calibrate();
    return true;
}

bool sensor::readRaw(sensorData& data) {
    if (!present) return false;

    data.x = (int16_t)(readAxis(REG_OUT_X_L)     * LIS3DH_SCALE);
    data.y = (int16_t)(readAxis(REG_OUT_X_L + 2) * LIS3DH_SCALE);
    data.z = (int16_t)(readAxis(REG_OUT_X_L + 4) * LIS3DH_SCALE);
    data.timestamp = millis();

    return true;
}

bool sensor::read(sensorData& data) {
    if (!present) return false;
    if (sleeping) wake();

    data.x = (int16_t)(readAxis(REG_OUT_X_L)     *  LIS3DH_SCALE);
    data.y = (int16_t)(readAxis(REG_OUT_X_L + 2) *  LIS3DH_SCALE);
    data.z = (int16_t)(readAxis(REG_OUT_X_L + 4) *  LIS3DH_SCALE);
    data.timestamp = millis();

    updateStepDetection(data);

    return true;
}

void sensor::calibrate() {
    sensorData d;
    float sx = 0, sy = 0, sz = 0;
    Serial.println("[SENSOR] Calibrating ,keep device still..");

    for (int i = 0; i < 100; i++){
        if(readRaw(d)){
            sx += d.x;
            sy += d.y;
            sz += d.z;
        };
        delay(10);
    }

    float ax = sx / 100.0f;
    float ay = sy / 100.0f;
    float az = sz / 100.0f;

    float norm = sqrt(ax*ax + ay*ay + az*az);
    if (norm < 1e-3f) norm = 1;

    gravity[0] = ax / norm;
    gravity[1] = ay / norm;
    gravity[2] = az / norm;

    rest = ax*gravity[0] + ay*gravity[1] + az*gravity[2];
    Serial.println("[SENSOR] Calibration complete...");
}

void sensor::updateStepDetection(const sensorData& d){
    float proj = d.x * gravity[0] + d.y * gravity[1] + d.z * gravity[2];
    float swing = fabs(proj - rest);

    if (swing < DEAD_BAND) swing = 0;
    buffer[idx] = swing;
    idx = (idx + 1) % FILTER_SIZE;

    float filtered = 0;
    for (int i = 0; i < FILTER_SIZE; i++) filtered += buffer[i];
    filtered /= FILTER_SIZE;

    processWave(filtered, d.timestamp);
}

void sensor::processWave(float val, unsigned long now){
    if (peak == 0 && valley == 0){
        peak = val;
        valley = val;
        return;
    }

    if (rising){
        if (val > peak) peak = val;
        else if ((peak - val) > MIN_PP) {
            rising = false;
            armed  = true;
            valley = val;
        }
    } else {
        if (val < valley) valley = val;
        else if (armed && (val - valley) > MIN_PP){
            unsigned long dt = now - lastStepTime;
            if (dt > CADENCE_MIN && dt < CADENCE_MAX){
                stepCount++;
                lastStepTime = now;
            }
            rising = true;
            armed  = false;
            peak   = val;
            valley = val;
        }
    }
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

unsigned long sensor::getStepCount() const {
    return stepCount;
}

void sensor::resetSteps(){
    stepCount = 0;
}