#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

struct sensorData {
    int16_t x;
    int16_t y;
    int16_t z;
    unsigned long timestamp;
};

// Step detection configuration
struct StepConfig {
    uint8_t breedFactor;           // 1-10, affects sensitivity
    unsigned long minStepInterval; // Minimum ms between steps (default 300)
    unsigned long cadenceMinMs;    // Fastest plausible cadence (default 250)
    unsigned long cadenceMaxMs;    // Slowest plausible cadence (default 1200)
    unsigned long cadenceTimeoutMs; // Exit active cadence after idle (default 2500)
};

class sensor {
public:
    static sensor& getInstance();
    bool init(uint8_t i2c_sda = 5, uint8_t i2c_scl = 6);
    bool read(sensorData& data);
    bool isPresent() const;
    void sleep();
    void wake();
    
    // Step counting methods
    void updateStepCounter();  // Call periodically (every 50-100ms)
    unsigned long getStepCount() const { return stepCount; }
    void resetStepCount() { stepCount = 0; }
    void setBreedFactor(uint8_t factor) { breedFactor = constrain(factor, 1, 10); }
    void setStepConfig(const StepConfig& config);
    
    // Calibration
    void calibrateGravity();  // Call once when device is stationary

private:
    sensor() {}
    ~sensor() {}
    sensor(const sensor&) = delete;
    sensor& operator=(const sensor&) = delete;

    // Hardware
    bool present = false;
    bool sleeping = false;
    
    // Step detection state
    unsigned long stepCount = 0;
    uint8_t breedFactor = 5;
    StepConfig config;
    bool initialized = false;
    
    // Gravity calibration
    float gravityVec[3] = {0};
    float restProjectionMG = 0.0f;
    
    // Filtering
    int filteredBuffer[7] = {0};
    int filterIndex = 0;
    
    // Peak/valley detection
    bool directionUnknown = true;
    bool rising = true;
    bool stepArmed = false;
    int lastPeakMG = 0;
    int lastValleyMG = 0;
    int prevSwingMG = 0;
    
    // Cadence gating
    bool pendingStep = false;
    unsigned long pendingStepTime = 0;
    bool activeCadence = false;
    unsigned long lastStepTime = 0;
    
    // Shock suppression
    int shockIgnoreCounter = 0;
    
    // Helper methods
    uint8_t readReg(uint8_t reg);
    void writeReg(uint8_t reg, uint8_t value);
    int16_t readAxis(uint8_t lowReg);
    float getDorsoVentralMG(float ax_mg, float ay_mg, float az_mg) const;
    void handleStepCadence(unsigned long now);
    void resetWaveState(int currentMG);
};

#endif