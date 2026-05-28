#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

struct sensorData {
    int16_t x;
    int16_t y;
    int16_t z;
    unsigned long timestamp;
};

class sensor {
    public:
        static sensor& getInstance();
        bool init(uint8_t i2c_sda = 5, uint8_t i2c_scl = 6);
        bool read(sensorData& data);
        bool isPresent() const;
        void sleep();
        void wake();

        unsigned long getStepCount() const;
        void resetSteps();

    private:
        sensor() {}
        ~sensor() {}
        sensor(const sensor&) = delete;
        sensor& operator=(const sensor&) = delete;

        bool present = false;
        bool sleeping = false;

        uint8_t readReg(uint8_t reg);
        void writeReg(uint8_t reg, uint8_t value);  // corrected name and void return
        int16_t readAxis(uint8_t lowReg);
        bool readRaw(sensorData& data);

        void calibrate();
        void updateStepDetection(const sensorData& data);
        void processWave(float val, unsigned long now);

        static const int FILTER_SIZE = 7;

        float gravity[3] = {0};
        float rest = 0;

        float buffer[FILTER_SIZE] = {0};
        int idx = 0;

        bool  rising = true;
        bool  armed  = false;
        float peak   = 0;
        float valley = 0;
        bool initialized = false;

        unsigned long lastStepTime = 0;
        unsigned long stepCount = 0;

        const float DEAD_BAND   = 200;
        const float MIN_PP      = 120;
        const int   CADENCE_MIN = 250;
        const int   CADENCE_MAX = 1200;
    };

#endif