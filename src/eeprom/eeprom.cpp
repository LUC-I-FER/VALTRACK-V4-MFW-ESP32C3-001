#include "eeprom.h"

eeprom::eeprom() {}
eeprom::~eeprom() {}

eeprom& eeprom::getInstance() {
    static eeprom instance;
    return instance;
}

void eeprom::init(size_t eeprom_size) {
    if (initialised) return;
    eepromSize = eeprom_size;
    EEPROM.begin(eepromSize);
    initialised = true;
    Serial.println("[EEPROM] Initialised..");
}

bool eeprom::hasSavedData() {
    if (!initialised) return false;
    uint8_t flag = EEPROM.read(EEPROM_FLAG);
    return (flag == 1);
}

void eeprom::load(int& breedFactor, unsigned long& stepCount) {
    if (!initialised) {
        Serial.println("[EEPROM] Not initialised, cannot load.");
        return;
    }
    if (!hasSavedData()) {
        Serial.println("[EEPROM] No saved data found.");
        return;
    }
    EEPROM.get(EEPROM_BREED, breedFactor);
    EEPROM.get(EEPROM_STEP, stepCount);
    Serial.printf("[EEPROM] Loaded: BreedFactor=%d, StepCount=%lu\n", breedFactor, stepCount);
}

void eeprom::save(int breedFactor, unsigned long stepCount) {
    if (!initialised) {
        Serial.println("[EEPROM] Not initialised, cannot save.");
        return;
    }
    EEPROM.write(EEPROM_FLAG, 1);
    EEPROM.put(EEPROM_BREED, breedFactor);
    EEPROM.put(EEPROM_STEP, stepCount);
    bool ok = EEPROM.commit();
    if (ok) {
        Serial.println("[EEPROM] Data saved.");
    } else {
        Serial.println("[EEPROM] Save failed.");
    }
}

void eeprom::clear() {
    if (!initialised) return;
    EEPROM.write(EEPROM_FLAG, 0);
    EEPROM.commit();
    Serial.println("[EEPROM] Cleared.");
}