// ==================== FORCE GPRS MODE (no WiFi) ====================
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

#define SerialMon Serial
#define SerialAT Serial1

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif

#define GSM_AUTOBAUD_MIN 9600
#define GSM_AUTOBAUD_MAX 115200

// -------------------- MQTT Broker (flespi.io) --------------------
const char mqtt_broker[] = "mqtt.flespi.io";
const int  mqtt_port     = 1883;               // Non‑SSL port (for easy testing)
const char mqtt_topic[]  = "esp32c3/test";     // Your topic (cannot start with 'flespi/')
// Replace with your own flespi token (64‑byte string)
const char flespi_token[] = "Hz8ZN1ZvlOH5yZcvgKJyaC3dOrdqILdsMQkdmFUKkqUabNcVIQprNgf7Fd1Vb3ZV";   // <--- CHANGE THIS!

// -------------------- APN Settings (Vodafone Idea) --------------------
const char apn[]      = "vi";
const char gprsUser[] = "";
const char gprsPass[] = "";

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

// -------------------- Modem & MQTT Objects --------------------
#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm        modem(debugger);
#else
TinyGsm        modem(SerialAT);
#endif

TinyGsmClient client(modem);
PubSubClient  mqtt(client);               // MQTT client using GSM client

// -------------------- GPIO Pins --------------------
#define GPIO_IIC_DATA   5
#define GPIO_IIC_CLOCK  6
#define GPIO_PWRKEY     7
#define GPIO_GSM_ENABLE 10
#define GPIO_TPS_ENABLE 4
#define GPIO_INT1       3
#define GPIO_SOS        9
#define GPIO_CHG_IN     4
#define GPIO_LED_SIGNAL 8

Adafruit_NeoPixel pixels(3, GPIO_LED_SIGNAL, NEO_GRB + NEO_KHZ800);

#define BATTERY_LED  0
#define NETWORK_LED  1
#define LOCATION_LED 2

#define RED   0
#define GREEN 1
#define BLUE  2
#define BRIGHTNESS 64

// ==================== LED Control ====================
void UpdateLED(int LED, int Color, int Brightness) {
  switch(Color) {
    case RED:   pixels.setPixelColor(LED, pixels.Color(Brightness, 0, 0)); break;
    case GREEN: pixels.setPixelColor(LED, pixels.Color(0, Brightness, 0)); break;
    case BLUE:  pixels.setPixelColor(LED, pixels.Color(0, 0, Brightness)); break;
    default:    pixels.setPixelColor(LED, pixels.Color(0, 0, 0)); break;
  }
  pixels.show();
}

// ==================== GSM Power & GPIO ====================
void EnableGSM(void)   { digitalWrite(GPIO_GSM_ENABLE, HIGH); }
void DisableGSM(void)  { digitalWrite(GPIO_GSM_ENABLE, LOW); }

void InitGPIO(void) {
  pinMode(GPIO_PWRKEY, OUTPUT);
  pinMode(GPIO_GSM_ENABLE, OUTPUT);
}

void InitLED(void) {
  pixels.begin();                 // FIXED: was commented
  UpdateLED(BATTERY_LED, RED, BRIGHTNESS);
  UpdateLED(NETWORK_LED, GREEN, BRIGHTNESS);
  UpdateLED(LOCATION_LED, BLUE, BRIGHTNESS);
}

// Corrected power‑on sequence (low pulse on PWRKEY)
void InitGSM(void) {
  digitalWrite(GPIO_PWRKEY, HIGH);
  delay(500);
  digitalWrite(GPIO_PWRKEY, LOW);   // power on
  delay(1000);
  digitalWrite(GPIO_PWRKEY, HIGH);
}

void InitUART0(void) { Serial.begin(115200); }
void InitUART1(void) { Serial1.begin(115200, SERIAL_8N1, 1, 0); }

// ==================== Accelerometer (LIS3DH) ====================
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
#define ACCLEROMETER_I2C_ADDRESS 0x19

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

void InitAccelerometer(void) {
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
  I2C_WrReg(REG_CTRL_REG1, 0x57);
  I2C_WrReg(REG_CTRL_REG4, 0x08);
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

// ==================== MQTT Callback (optional) ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  SerialMon.print("Message arrived [");
  SerialMon.print(topic);
  SerialMon.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    SerialMon.print((char)payload[i]);
  }
  SerialMon.println();
}

// ==================== MQTT Reconnection Helper ====================
void reconnectMQTT() {
  while (!mqtt.connected()) {
    SerialMon.print("Connecting to MQTT broker...");
    // Use your flespi token as the username, password empty
    if (mqtt.connect("ESP32C3_GSM_Client", flespi_token, "")) {
      SerialMon.println(" connected!");
      // Subscribe to a topic if you want to receive messages
      // mqtt.subscribe("esp32c3/commands");
    } else {
      SerialMon.print(" failed, rc=");
      SerialMon.println(mqtt.state());
      SerialMon.println(" retry in 5 seconds");
      delay(5000);
    }
  }
}

// ==================== Setup ====================
void setup() {
  delay(5000);
  InitGPIO();
  InitUART0();
  InitUART1();
  EnableGSM();
  delay(2000);                     // Let GSM power stabilise
  InitLED();
  InitAccelerometer();
  InitGSM();                       // Power‑on pulse

  SerialMon.println("Starting...");
  delay(1000);

  SerialMon.println("Restarting modem...");
  modem.restart();
  delay(1000);

  SerialMon.print("Waiting for network...");
  if (!modem.waitForNetwork(30000L)) {
    SerialMon.println(" FAIL");
    UpdateLED(NETWORK_LED, RED, BRIGHTNESS);
    while (true) { delay(1000); }
  }
  SerialMon.println(" OK");

  delay(2000);                    // Extra settle time

  SerialMon.print("Connecting to GPRS (APN: ");
  SerialMon.print(apn);
  SerialMon.print(")...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println(" FAIL");
    UpdateLED(NETWORK_LED, RED, BRIGHTNESS);
    while (true) { delay(1000); }
  }
  SerialMon.println(" SUCCESS");

  delay(3000);                    // Wait for IP assignment

  if (modem.isGprsConnected()) {
    SerialMon.println("GPRS connected!");
    UpdateLED(NETWORK_LED, GREEN, BRIGHTNESS);
  } else {
    SerialMon.println("GPRS NOT connected after connect attempt");
    while (true) { delay(1000); }
  }

  // Setup MQTT
  mqtt.setServer(mqtt_broker, mqtt_port);
  mqtt.setCallback(mqttCallback);   // optional
  reconnectMQTT();                  // initial connection
}

// ==================== Loop ====================
void loop() {
  // Check GPRS – if lost, reconnect
  if (!modem.isGprsConnected()) {
    SerialMon.println("GPRS connection lost. Reconnecting...");
    UpdateLED(NETWORK_LED, RED, BRIGHTNESS);
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      SerialMon.println("GPRS reconnect failed");
      delay(10000);
      return;
    }
    delay(3000);
    UpdateLED(NETWORK_LED, GREEN, BRIGHTNESS);
  }

  // Maintain MQTT connection
  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();

  // Publish a test message
  String message = "Hello from ESP32-C3 GSM! Uptime: " + String(millis() / 1000) + "s";
  if (mqtt.publish(mqtt_topic, message.c_str())) {
    SerialMon.println("MQTT message published");
  } else {
    SerialMon.println("MQTT publish failed");
  }

  // You can read accelerometer data here and publish as JSON later
  // ...

  delay(30000);   // Wait 30 seconds between publications
}