// SPDX-License-Identifier: GPL-2.0-or-later

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "modem.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>  // You MUST install this library via the Arduino IDE's Library Manager
#include <esp_sleep.h>
#include "driver/gpio.h"

#define LED_SIGNAL 8
#define NUMPIXELS 3
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_SIGNAL, NEO_GRB + NEO_KHZ800);

enum {
  RED = 0,
  GREEN = 1,
  BLUE = 2,
  PURPLE = 3,
  YELLOW = 4,
  WHITE = 5,
};

void UpdateLED(int LED, int Color, int Brightness);
// Forward declaration
static inline float getDorsoVentralMG(float ax_mg, float ay_mg, float az_mg);

// Sampling/filtering
#define FILTER_SIZE 7
const unsigned long SENSOR_INTERVAL = 100;  // ~10 Hz read
const unsigned long SEND_INTERVAL = 8000;

// Step detection thresholds/timing (all mg except ms)
#define DEAD_BAND_MG 200          // Ignore tiny fluctuations (< ~0.2g)
#define MIN_PP_MG_BASE 80         // Base peak-to-peak per BreedFactor
#define SHOCK_MG 700              // Single-sample spike threshold to treat as jerk
#define MIN_STEP_INTERVAL_MS 300  // Debounce between steps
#define CADENCE_MIN_MS 250        // Fastest plausible cadence
#define CADENCE_MAX_MS 1200       // Slowest plausible cadence
#define CADENCE_TIMEOUT_MS 2500   // Exit active cadence after this idle time

// Projection and filtering state
float gravityVec[3] = { 0 };
float restProjectionMG = 0.0f;

int filteredBuffer[FILTER_SIZE] = { 0 };
int filterIndex = 0;
bool directionUnknown = true;
bool rising = true;
bool stepArmed = false;
int lastPeakMG = 0;
int lastValleyMG = 0;
int prevSwingMG = 0;

// Cadence gating state
bool pendingStep = false;
unsigned long pendingStepTime = 0;
bool activeCadence = false;
unsigned long lastStepTime = 0;

// Shock suppression
int shockIgnoreCounter = 0;  // temporarily ignore N samples after a detected shock

unsigned long lastSensorTime = 0;
unsigned long lastSendTime = 0;


typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
} acel_t;

/*********************************************/
/************* USER  DEFINITION *************/
/*********************************************/
// Define constants for data transmission intervals in milliseconds
#define LIVE_INTERVAL   5000    // 5 seconds for "live" mode
#define NORMAL_INTERVAL 30000  //30000  // 30 seconds for "normal" mode
#define SAFE_INTERVAL   60000   // 60 seconds (1 minute) for "safe" mode
#define SLEEP_DELAY     5000

// Changed stepCount from uint to unsigned long for better portability and clarity.
// The volatile keyword is crucial here because the variable is modified by an interrupt.
volatile unsigned long stepCount = 0;
unsigned long lastStepPrint = 0;
unsigned long Interval_Set = NORMAL_INTERVAL;  // Default to normal mode interval

char payload[256] = "";

String mode = "live";                // default mode
String simNumber = "+910000000000";  // Placeholder
String bleMac;
float latitude = 0.0;
float longitude = 0.0;
float batteryLevel = 0;
int BreedFactor = 4;
int gprsStatus = 0;
int sosFlag = 0;
int resetFlag = 0;
int bleActive = 0;
int signalStrength = 0;

// Debounce timer for step counting
volatile unsigned long lastStepMillis = 0;
#define STEP_DEBOUNCE_DELAY 150  // Minimum time between steps in ms

/*********************************************
 ************* BLE  DEFINITION *************
 *********************************************/

static bool deviceConnected = false;
void set_mode(String modeStr);

BLECharacteristic* pCharacteristic;
BLEServer* pServer;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    deviceConnected = true;
    Serial.println("[BLE] Device connected.");
    UpdateLED(0, BLUE, 60);
    UpdateLED(1, BLUE, 60);
    UpdateLED(2, BLUE, 60);
  }
  void onDisconnect(BLEServer*) override {
    deviceConnected = false;
    if (mode == "live") {
      if (modem_get_status() & MODEM_GNSS_READY) {
        // connected successfully
        UpdateLED(0, PURPLE, 60);
        UpdateLED(1, PURPLE, 60);
        UpdateLED(2, PURPLE, 60);
      } else if (modem_get_status() & MODEM_STATE_READY) {
        // connected successfully
        UpdateLED(0, YELLOW, 60);
        UpdateLED(1, YELLOW, 60);
        UpdateLED(2, YELLOW, 60);
      } else {
        // connected successfully
        UpdateLED(0, WHITE, 60);
        UpdateLED(1, WHITE, 60);
        UpdateLED(2, WHITE, 60);
      }
    }
    // Start advertising again to be discoverable by other devices
    pServer->startAdvertising();
    Serial.println("[BLE] Device disconnected. Advertising restarted.");
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String incoming = String((char*)pCharacteristic->getData());
    incoming.trim();

    Serial.print("[BLE] Received command: ");
    Serial.println(incoming);

    if (incoming == "reset") {
      stepCount = 0;
      Serial.println("[BLE] Step count reset to 0.");
    } else if (incoming == "clear") {
      stepCount = 0;
      BreedFactor = 1;
      set_mode("normal");  // reset to defaults
      Serial.println("[BLE] All settings cleared to default.");
    } else {
      // Split by comma (BreedFactor,mode)
      int commaIndex = incoming.indexOf(',');
      if (commaIndex > 0) {
        String bfStr = incoming.substring(0, commaIndex);
        String modeStr = incoming.substring(commaIndex + 1);
        bfStr.trim();
        modeStr.trim();

        // Parse BreedFactor
        int val = bfStr.toInt();
        if (val >= 1 && val <= 10) {
          BreedFactor = (int)val;
          Serial.printf("BreedFactor set to: ");
          Serial.println(BreedFactor);
        }

        set_mode(modeStr);
      } else {
        Serial.println("Invalid input, expected format: <number>,<mode>");
      }
    }
  }
};

void ble_init() {
  BLEDevice::init("ESP32_Paw");
  bleMac = String(BLEDevice::getAddress().toString());
  Serial.print("[BLE] MAC Address: ");
  Serial.println(bleMac);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());
  // pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_RX, BLECharacteristic::PROPERTY_WRITE);
  // pCharacteristicRX->setCallbacks(new MyCallbacks());

  pService->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setMinInterval(0x320);
  advertising->setMaxInterval(0x640);
  advertising->start();
  // esp_sleep_enable_bt_wakeup();
}

void beacon_mode(bool mode) {
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->stop();  // Stop GATT advertising

  if (mode) {
    BLEAdvertisementData advData;
    advData.setFlags(0x06);                    // General discoverable, BR/EDR not supported
    advData.setManufacturerData("ESP32_Paw");  // Embed UID

    // advertising->setAdvertisementType(ESP_BLE_ADV_NONCONN_IND);
    advertising->setScanResponse(false);
    advertising->setAdvertisementData(advData);
  } else {
    advertising->setMinInterval(0x320);
    advertising->setMaxInterval(0x640);
  }

  advertising->start();

  Serial.printf("[BLE] %s mode started.\n", mode ? "BEACON" : "ADVER");
}

/*********************************************
 ************* EEPROM DEFINITION *************
 *********************************************/
#define EEPROM_SIZE 64
#define EEPROM_FLAG 0
#define EEPROM_BREED 4
#define EEPROM_STEP 8

void eeprom_write() {
  EEPROM.write(EEPROM_FLAG, 1);
  EEPROM.put(EEPROM_BREED, BreedFactor);
  EEPROM.put(EEPROM_STEP, stepCount);
  EEPROM.commit();
  Serial.println("[EEPROM] Data committed.");
}

void eeprom_init() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_FLAG) == 1) {
    EEPROM.get(EEPROM_BREED, BreedFactor);
    EEPROM.get(EEPROM_STEP, stepCount);
    Serial.print("[EEPROM] Restored stepCount: ");
    Serial.println(stepCount);
  } else {
    Serial.println("[EEPROM] No saved data found.");
  }
}

/*********************************************
 ************* LIS3DH DEFINITION *************
 *********************************************/
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

#define SSTP_I2C_DATA 5
#define SSTP_I2C_CLOCK 6
#define SSTP_INTERRUPT 3

// TODO: Move to dedicated header file
#define REG_CTRL_REG1 0x20
#define REG_CTRL_REG2 0x21
#define REG_CTRL_REG3 0x22
#define REG_CTRL_REG4 0x23
#define REG_CTRL_REG5 0x24
#define REG_CTRL_REG6 0x25
#define REG_INT1_CFG 0x30
#define REG_INT1_SRC 0x31
#define REG_INT1_THS 0x32
#define REG_INT1_DURATION 0x33

#define OUT_X_L 0x28
#define OUT_X_H 0x29

#define OUT_Y_L 0x2A
#define OUT_Y_H 0x2B

#define OUT_Z_L 0x2C
#define OUT_Z_H 0x2D

#define ACCLEROMETER_I2C_ADDRESS 0x19

#define STEP_INT_DELAY 5000  // 5 seconds

// I2C helper functions

/*
uint8_t I2C_RdReg(uint8_t RegisterAddress) {
  Wire.beginTransmission(ACCLEROMETER_I2C_ADDRESS);
  Wire.write(RegisterAddress);
  Wire.endTransmission();
  // Added a delay to prevent a possible issue where the requestFrom()
  // call returns 0 if it's called too soon after the endTransmission().
  delay(2);
  Wire.requestFrom(ACCLEROMETER_I2C_ADDRESS, 1);
  return (uint8_t)Wire.read();
}
*/
void I2C_WrReg(uint8_t RegisterAddress, uint8_t Data) {
  Wire.beginTransmission(ACCLEROMETER_I2C_ADDRESS);
  Wire.write(RegisterAddress);
  Wire.write(Data);
  Wire.endTransmission();
}

void sleep_accel() {
  I2C_WrReg(REG_CTRL_REG1, 0x00);
}
void wake_accel() {
  I2C_WrReg(REG_CTRL_REG1, 0x47);
}

// IRAM_ATTR places the function in the IRAM memory, making it
// execute faster and preventing issues when interrupts occur during flash access.
// void IRAM_ATTR onStepDetected() { }

void read_val(acel_t& acel_data) {
  lis.read();
  acel_data.x = lis.x;
  acel_data.y = lis.y;
  acel_data.z = lis.z;
}

void calibrateGravityAxis() {
  Serial.println("Calibrating gravity...");
  float sumX = 0, sumY = 0, sumZ = 0;
  for (int i = 0; i < 200; i++) {
    lis.read();
    sumX += lis.x;  // mg
    sumY += lis.y;
    sumZ += lis.z;
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

  // Rest projection in mg (dot product of avg with unit gravity)
  restProjectionMG = getDorsoVentralMG(avgX, avgY, avgZ);
  Serial.print("Gravity unit: ");
  Serial.print(gravityVec[0], 4);
  Serial.print(", ");
  Serial.print(gravityVec[1], 4);
  Serial.print(", ");
  Serial.println(gravityVec[2], 4);
  Serial.print("Rest projection (mg): ");
  Serial.println(restProjectionMG, 1);
}

// Projection onto gravity unit vector, using raw mg inputs
static inline float getDorsoVentralMG(float ax_mg, float ay_mg, float az_mg) {
  return ax_mg * gravityVec[0] + ay_mg * gravityVec[1] + az_mg * gravityVec[2];
}

void resetWaveState(int currentMG) {
  directionUnknown = true;
  rising = true;
  stepArmed = false;
  lastPeakMG = currentMG;
  lastValleyMG = currentMG;
}


void handleStepCadence(unsigned long now) {
  // Called when a candidate step has passed amplitude and debounce checks
  if (activeCadence) {
    // In active cadence: count step if interval plausible, else break cadence and start pending
    unsigned long dt = now - lastStepTime;
    if (dt >= CADENCE_MIN_MS && dt <= CADENCE_MAX_MS) {
      stepCount++;
      lastStepTime = now;
    } else {
      activeCadence = false;
      pendingStep = true;
      pendingStepTime = now;
      // Do not increment yet
    }
    return;
  }

  if (pendingStep) {
    unsigned long dt = now - pendingStepTime;
    if (dt >= CADENCE_MIN_MS && dt <= CADENCE_MAX_MS) {
      // Confirm cadence: count both pending and current
      stepCount += 2;
      lastStepTime = now;
      pendingStep = false;
      activeCadence = true;
      return;
    } else if (dt > CADENCE_MAX_MS) {
      // Pending expired: replace with new pending
      pendingStepTime = now;
      // keep pendingStep true, no count yet
      return;
    } else {
      // Too soon (< CADENCE_MIN_MS), keep waiting
      return;
    }
  } else {
    // No pending: start waiting for a confirm
    pendingStep = true;
    pendingStepTime = now;
    // No count yet
  }
}
void updateStepCounter() {
  unsigned long now = millis();

  // Read raw mg from LIS3DH
  lis.read();
  float ax_mg = lis.x;
  float ay_mg = lis.y;
  float az_mg = lis.z;

  float projMG = getDorsoVentralMG(ax_mg, ay_mg, az_mg);
  float swingMGf = fabsf(projMG - restProjectionMG);
  int swingMG = (int)(swingMGf + 0.5f);

  // Shock suppression: ignore short, large spikes
  if (shockIgnoreCounter > 0) {
    shockIgnoreCounter--;
    prevSwingMG = swingMG;
    return;
  }
  if (prevSwingMG < DEAD_BAND_MG && swingMG >= SHOCK_MG) {
    // Detected a sudden impact-like jerk; ignore a few samples
    Serial.println("Shock detected -> ignoring transient");
    shockIgnoreCounter = 3;  // ignore next ~300 ms at 10 Hz
    prevSwingMG = swingMG;
    return;
  }

  // Dead-band
  if (swingMG < DEAD_BAND_MG) swingMG = 0;

  // Simple moving average filter
  filteredBuffer[filterIndex] = swingMG;
  filterIndex = (filterIndex + 1) % FILTER_SIZE;
  int filteredSwingMG = 0;
  for (int i = 0; i < FILTER_SIZE; i++) filteredSwingMG += filteredBuffer[i];
  filteredSwingMG /= FILTER_SIZE;

  // Exit cadence if idle for too long
  if (activeCadence && (now - lastStepTime) > CADENCE_TIMEOUT_MS) {
    activeCadence = false;
    pendingStep = false;
  }

  // Peak/valley tracking with hysteresis
  int MIN_PP_MG = (MIN_PP_MG_BASE * BreedFactor);  // sensitivity gate scales by breed factor

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
      // Enough drop from peak -> arm possible step on next valid rise
      rising = false;
      stepArmed = true;
      lastValleyMG = current;
      //Serial.println("Armed after downward swing");
    }
  } else {
    if (current < lastValleyMG) lastValleyMG = current;
    else if (stepArmed && (current - lastValleyMG) > MIN_PP_MG) {
      // Full cycle detected (valley -> rise)
      if ((now - lastStepTime) >= MIN_STEP_INTERVAL_MS) {
        // Instead of counting immediately, pass through cadence gate
        handleStepCadence(now);
      }
      // Reset for next cycle
      stepArmed = false;
      rising = true;
      lastPeakMG = current;
      lastValleyMG = current;
    }
  }

  prevSwingMG = swingMG;
}

void lis3dh_init(void) {
  uint8_t val = 0;
  // Ensure these are defined in the header or main file
  Wire.begin(SSTP_I2C_DATA, SSTP_I2C_CLOCK);

  if (!lis.begin(ACCLEROMETER_I2C_ADDRESS)) {
    Serial.println("LIS3DH not detected!");
    return;
  }
  val = 0x33;  // HARD CODE;

  // val = I2C_RdReg(0x0F);
  lis.setRange(LIS3DH_RANGE_2_G);  // ±2g
  Serial.printf("Motion Sensor LIS3DH: %s\n", (val == 0x33) ? "Found" : "Not Found");
  Serial.println("Hold device still for 3 seconds...");
  delay(3000);

  // // Configure LIS3DH
  // I2C_WrReg(REG_CTRL_REG1, 0x57);      // 100Hz, XYZ enabled
  // I2C_WrReg(REG_CTRL_REG4, 0x08);      // High resolution
  // I2C_WrReg(REG_CTRL_REG2, 0x05);      // High-pass filter
  I2C_WrReg(REG_CTRL_REG3, 0x40);  // INT1 interrupt on INT1 pin
  I2C_WrReg(REG_CTRL_REG5, 0x08);  // Latch interrupt
  I2C_WrReg(REG_CTRL_REG6, 0x02);  // INT1 active high
  // I2C_WrReg(REG_INT1_THS, 0x18);       // Threshold
  // I2C_WrReg(REG_INT1_DURATION, 0x00);  // Duration
  // I2C_WrReg(REG_INT1_CFG, 0x2A);       // Enable XH, YH, ZH interrupts

  // I2C_RdReg(REG_INT1_SRC);  // clear interrupt

  calibrateGravityAxis();
  pinMode(SSTP_INTERRUPT, INPUT_PULLUP);
  gpio_wakeup_enable((gpio_num_t)SSTP_INTERRUPT, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();  // wake up on HIGH (rising)
}

/******************************************************************************************/
void set_mode(String modeStr) {
  modeStr.toLowerCase();

  // Update the global mode variable
  if (modeStr == "live") {
    Serial.println("[MODE] Live Mode Activated");
    mode = modeStr;
    Interval_Set = LIVE_INTERVAL;
    modem_init();
    modem_mqtt_start();
    // Try to bring gnss online
  } else if (modeStr == "safe") {
    Serial.println("[MODE] Safe Mode Activated");
    mode = modeStr;
    Interval_Set = SAFE_INTERVAL;
    modem_deinit();
  } else if (modeStr == "normal") {
    Serial.println("[MODE] Normal Mode Activated");
    mode = modeStr;
    Interval_Set = NORMAL_INTERVAL;
    modem_deinit();
  } else if (modeStr == "blegps") {
    Serial.println("[MODE] blegps Mode Activated");
    mode = modeStr;
    Interval_Set = NORMAL_INTERVAL;
    modem_init();
  } else {
    Serial.println("Supported Modes are: live, safe, normal, blegps");
  }
}

// New function to handle incoming MQTT messages
void handleIncomingMQTT() {
  // Check for any incoming message from the modem
  String payload = modem_mqtt_read_payload(0);
  if (payload.length() > 0) {
    // We received a message, no w parse the JSON
    Serial.println("[MQTT] Parsing incoming JSON payload.");
    StaticJsonDocument<256> doc;  // Create a JSON document on the stack
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[JSON] Deserialization failed: ");
      Serial.println(error.c_str());
      return;
    }

    // Check if the JSON contains a "mode" key
    if (doc.containsKey("mode")) {
      String newMode = doc["mode"].as<String>();
      newMode.toLowerCase();
      set_mode(newMode);  // Update the mode and interval
      Serial.printf("[CONFIG] Mode updated to: %s\n", mode.c_str());
    }

    // Check if the JSON contains a "breedFactor" key
    if (doc.containsKey("breedFactor")) {
      int newBreedFactor = doc["breedFactor"].as<int>();
      if (newBreedFactor >= 1 && newBreedFactor <= 10) {
        BreedFactor = newBreedFactor;
        Serial.print("[CONFIG] BreedFactor updated to: ");
        Serial.println(BreedFactor);
      }
    }
  }
}

void UpdateLED(int LED, int Color, int Brightness) {

  switch (Color) {
    case RED:
      pixels.setPixelColor(LED, pixels.Color(Brightness, 0, 0));
      break;
    case GREEN:
      pixels.setPixelColor(LED, pixels.Color(0, Brightness, 0));
      break;
    case BLUE:
      pixels.setPixelColor(LED, pixels.Color(0, 0, Brightness));
      break;
    case PURPLE:
      pixels.setPixelColor(LED, pixels.Color(Brightness, 0, Brightness));
      break;
    case YELLOW:
      pixels.setPixelColor(LED, pixels.Color(Brightness, Brightness, 0));
      break;
    case WHITE:
      pixels.setPixelColor(LED, pixels.Color(Brightness, Brightness, Brightness));
      break;
    default:
      pixels.setPixelColor(LED, pixels.Color(0, 0, 0));
      break;
  }
  pixels.show();  // This sends the updated pixel color to the hardware.
}

void setup() {
  Serial.begin(115200);  // Debug port
  Serial1.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); // modem port
  pinMode(2, INPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_GSMKEY, OUTPUT);

  lis3dh_init();
  eeprom_init();
  modem_set_gsm_state(1, 200);

  // connected successfully
  UpdateLED(0, RED, 60);
  UpdateLED(1, RED, 60);
  UpdateLED(2, RED, 60);

  // Set initial interval based on the default mode
  set_mode(mode);

  ble_init();
}

#define ADC_TO_VIN 0.002086
void battery_read(float* er) {
  int rawADC = analogRead(2);
  float batteryVoltage = rawADC * ADC_TO_VIN;

  Serial.print("ADC: ");
  Serial.print(rawADC);
  Serial.print(" | Battery Voltage: ");
  Serial.print(batteryVoltage, 3);
  Serial.println(" V");

  *er = batteryVoltage;
}

String topic = "";

void sendData() {
  if (topic == "") {
    bleMac.toUpperCase();
    topic = "pets/" + bleMac + "/data";
  }
  modem_get_gps(&latitude, &longitude);
  modem_battery_get_status((float*)&batteryLevel);
  snprintf(payload, sizeof(payload),
           "{"
           "\"SIM\":\"%s\","
           "\"MACID\":\"%s\","
           "\"Latitude\":%.6f,"
           "\"Longitude\":%.6f,"
           "\"Battery\":%.2f,"
           "\"StepCount\":%lu,"  // Use %lu for unsigned long
           "\"WiFi\":%d,"
           "\"Signal\":%d,"
           "\"SOS\":%d,"
           "\"Reset\":%d,"
           "\"BLE\":%d,"
           "\"BreedFactor\":%d,"     // Comma added here
           "\"Mode\":\"%s\""         // normal, safe, live
           "}",
           simNumber.c_str(),
           bleMac.c_str(),
           latitude,
           longitude,
           batteryLevel,
           stepCount,
           gprsStatus,
           signalStrength,
           sosFlag,
           resetFlag,
           bleActive,
           BreedFactor,
           mode.c_str());  // Correctly pass the C-style string

  if (deviceConnected) {
    bleActive = 1;
    UpdateLED(0, BLUE, 60);
    UpdateLED(1, BLUE, 60);
    UpdateLED(2, BLUE, 60);
    if (pCharacteristic != nullptr) {
      pCharacteristic->setValue(payload);
      pCharacteristic->notify();
      Serial.println("[BLE] Data sent to connected device.");
    }
    // connected successfully
  } else if (mode == "live") {
    bleActive = 0;
    if (payload) {
      Serial.print("[MQTT] Sending payload: ");
      Serial.println(payload);
      if (modem_mqtt_send(0, topic, String(payload))) {
        Serial.println("[MQTT] Modem not ready!");
        return;
      }

      if (modem_get_status() & MODEM_GNSS_READY) {
        // connected successfully
        UpdateLED(0, PURPLE, 60);
        UpdateLED(1, PURPLE, 60);
        UpdateLED(2, PURPLE, 60);
      } else {
        // connected successfully
        UpdateLED(0, YELLOW, 60);
        UpdateLED(1, YELLOW, 60);
        UpdateLED(2, YELLOW, 60);
      }
      Serial.println("[MQTT] sent data.");
    }
  }
}

void loop() {
  static unsigned long lastStepPrint = 0;
  static unsigned long lastSensorTime = 0;
  static unsigned long sleep_ns = 0;
  unsigned long now = millis();

  // Always check for new messages, but don't block the loop
  handleIncomingMQTT();

  // Check if enough time has passed based on the current mode's interval
  if ((now - lastStepPrint) >= Interval_Set) {
    lastStepPrint = now;
    Serial.printf("Steps counted: %lu\n", stepCount);
    sendData();  //send BLE data
    eeprom_write();
  }
  if ((now - lastSensorTime) >= 100) {
    wake_accel();
    updateStepCounter();
    sleep_accel();
    lastSensorTime = now;
  }
  if (mode == "safe" || mode == "normal") {
    if ((now - sleep_ns) >= SLEEP_DELAY) {
      sleep_ns = now;
      Serial.printf("Entering Sleep Mode\n");
      delay(200);
      beacon_mode(true);
      delay(200);
      esp_light_sleep_start();
      beacon_mode(false);
      Serial.printf("Exiting Sleep Mode\n");
    }
  }
}
