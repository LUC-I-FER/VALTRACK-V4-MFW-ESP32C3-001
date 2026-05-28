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

// Step detection constants
#define DEAD_BAND_MG         200    // Ignore tiny fluctuations (< ~0.2g)
#define MIN_PP_MG_BASE       80     // Base peak-to-peak per BreedFactor
#define SHOCK_MG             700    // Single-sample spike threshold
#define MIN_STEP_INTERVAL_MS 300    // Debounce between steps
#define CADENCE_MIN_MS       250    // Fastest plausible cadence
#define CADENCE_MAX_MS       1200   // Slowest plausible cadence
#define CADENCE_TIMEOUT_MS   2500   // Exit active cadence after idle
#define FILTER_SIZE          7      // Moving average filter size

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
    return (int16_t)((high << 8) | low);
}

float sensor::getDorsoVentralMG(float ax_mg, float ay_mg, float az_mg) const {
    return ax_mg * gravityVec[0] + ay_mg * gravityVec[1] + az_mg * gravityVec[2];
}

void sensor::resetWaveState(int currentMG) {
    directionUnknown = true;
    rising = true;
    stepArmed = false;
    lastPeakMG = currentMG;
    lastValleyMG = currentMG;
}

void sensor::handleStepCadence(unsigned long now) {
    if (activeCadence) {
        unsigned long dt = now - lastStepTime;
        if (dt >= CADENCE_MIN_MS && dt <= CADENCE_MAX_MS) {
            stepCount++;
            lastStepTime = now;
            Serial.printf("[STEP] Cadence step: %lu (dt=%lu)\n", stepCount, dt);
        } else {
            activeCadence = false;
            pendingStep = true;
            pendingStepTime = now;
        }
        return;
    }

    if (pendingStep) {
        unsigned long dt = now - pendingStepTime;
        if (dt >= CADENCE_MIN_MS && dt <= CADENCE_MAX_MS) {
            stepCount += 2;
            lastStepTime = now;
            pendingStep = false;
            activeCadence = true;
            Serial.printf("[STEP] Cadence confirmed: %lu\n", stepCount);
        } else if (dt > CADENCE_MAX_MS) {
            pendingStepTime = now;
        }
    } else {
        pendingStep = true;
        pendingStepTime = now;
    }
}

void sensor::calibrateGravity() {
    if (!present) return;
    
    Serial.println("[SENSOR] Calibrating gravity...");
    float sumX = 0, sumY = 0, sumZ = 0;
    
    for (int i = 0; i < 200; i++) {
        sensorData data;
        if (read(data)) {
            sumX += data.x;
            sumY += data.y;
            sumZ += data.z;
        }
        delay(10);
    }
    
    float avgX = sumX / 200.0f;
    float avgY = sumY / 200.0f;
    float avgZ = sumZ / 200.0f;
    
    // Unit gravity vector
    float norm = sqrtf(avgX * avgX + avgY * avgY + avgZ * avgZ);
    if (norm < 1e-3f) norm = 1.0f;
    gravityVec[0] = avgX / norm;
    gravityVec[1] = avgY / norm;
    gravityVec[2] = avgZ / norm;
    
    // Rest projection in mg
    restProjectionMG = getDorsoVentralMG(avgX, avgY, avgZ);
    
    Serial.print("[SENSOR] Gravity unit: ");
    Serial.print(gravityVec[0], 4);
    Serial.print(", ");
    Serial.print(gravityVec[1], 4);
    Serial.print(", ");
    Serial.println(gravityVec[2], 4);
    Serial.print("[SENSOR] Rest projection (mg): ");
    Serial.println(restProjectionMG, 1);
}

void sensor::setStepConfig(const StepConfig& cfg) {
    config = cfg;
}

void sensor::updateStepCounter() {
    if (!present || sleeping) return;
    
    unsigned long now = millis();
    
    // Read raw data (in mg)
    sensorData raw;
    if (!read(raw)) return;
    
    float projMG = getDorsoVentralMG(raw.x, raw.y, raw.z);
    float swingMGf = fabsf(projMG - restProjectionMG);
    int swingMG = (int)(swingMGf + 0.5f);
    
    // Shock suppression
    if (shockIgnoreCounter > 0) {
        shockIgnoreCounter--;
        prevSwingMG = swingMG;
        return;
    }
    
    if (prevSwingMG < DEAD_BAND_MG && swingMG >= SHOCK_MG) {
        Serial.println("[STEP] Shock detected -> ignoring");
        shockIgnoreCounter = 3;
        prevSwingMG = swingMG;
        return;
    }
    
    // Dead-band filtering
    if (swingMG < DEAD_BAND_MG) swingMG = 0;
    
    // Moving average filter
    filteredBuffer[filterIndex] = swingMG;
    filterIndex = (filterIndex + 1) % FILTER_SIZE;
    int filteredSwingMG = 0;
    for (int i = 0; i < FILTER_SIZE; i++) filteredSwingMG += filteredBuffer[i];
    filteredSwingMG /= FILTER_SIZE;
    
    // Exit cadence if idle
    if (activeCadence && (now - lastStepTime) > CADENCE_TIMEOUT_MS) {
        activeCadence = false;
        pendingStep = false;
    }
    
    // Peak/valley detection
    int MIN_PP_MG = MIN_PP_MG_BASE * breedFactor;
    int current = filteredSwingMG;
    
    if (directionUnknown) {
        lastPeakMG = current;
        lastValleyMG = current;
        directionUnknown = false;
        rising = true;
    }
    
    if (rising) {
        if (current > lastPeakMG) lastPeakMG = current;
        else if ((lastPeakMG - current) > MIN_PP_MG) {
            rising = false;
            stepArmed = true;
            lastValleyMG = current;
        }
    } else {
        if (current < lastValleyMG) lastValleyMG = current;
        else if (stepArmed && (current - lastValleyMG) > MIN_PP_MG) {
            if ((now - lastStepTime) >= MIN_STEP_INTERVAL_MS) {
                handleStepCadence(now);
            }
            stepArmed = false;
            rising = true;
            lastPeakMG = current;
            lastValleyMG = current;
        }
    }
    
    prevSwingMG = swingMG;
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
    
    // Initialize LIS3DH
    writeReg(REG_CTRL_REG1, 0x57);  // 100Hz, XYZ enabled
    writeReg(REG_CTRL_REG4, 0x08);  // High resolution
    delay(10);
    writeReg(REG_CTRL_REG2, 0x05);  // High-pass filter
    writeReg(REG_CTRL_REG3, 0x40);  // INT1 interrupt
    writeReg(REG_CTRL_REG5, 0x08);  // Latch interrupt
    writeReg(REG_CTRL_REG6, 0x02);  // INT1 active high
    
    Serial.println("[SENSOR] LIS3DH initialised.");
    
    // Initialize step detection state
    config.breedFactor = breedFactor;
    config.minStepInterval = MIN_STEP_INTERVAL_MS;
    config.cadenceMinMs = CADENCE_MIN_MS;
    config.cadenceMaxMs = CADENCE_MAX_MS;
    config.cadenceTimeoutMs = CADENCE_TIMEOUT_MS;
    
    // Calibrate gravity
    delay(500);
    calibrateGravity();
    
    initialized = true;
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
    Serial.println("[SENSOR] Sleeping");
}

void sensor::wake() {
    if (!sleeping) return;
    writeReg(REG_CTRL_REG1, 0x57);
    sleeping = false;
    Serial.println("[SENSOR] Waking");
}