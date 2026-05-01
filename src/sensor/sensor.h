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
};

#endif