#ifndef EEPROM_H
#define EEPROM_H

#include<Arduino.h>
#include<EEPROM.h>

class eeprom{
    public:
        static eeprom& getInstance();
        void init(size_t eeprom_size = 64);
        bool hasSavedData();
        void load(int& breedFactor, unsigned long& stepCount);
        void save(int breedFactor, unsigned long stepCount);
        void clear();
    private:
        eeprom();
        ~eeprom();
        eeprom(const eeprom&) = delete;
        eeprom& operator=(const eeprom&) = delete;

        static const int EEPROM_FLAG = 0;
        static const int EEPROM_BREED = 4;
        static const int EEPROM_STEP = 8;

        size_t eepromSize;
        bool initialised = false;
};

#endif