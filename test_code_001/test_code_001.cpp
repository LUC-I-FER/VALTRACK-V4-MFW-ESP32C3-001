#define TINY_GSM_MODEM_SIM7600

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ---------------- PIN CONFIG ----------------
#define TPS_ENABLE 4
#define GSM_ENABLE 10
#define GSM_PWRKEY 7
#define MODEM_TX 0
#define MODEM_RX 1

#define LED_PIN 8
#define NUM_PIXELS 3

// ---------------- VERBOSE LOGGING ----------------
#define VERBOSE true

#if VERBOSE
  #define LOG(x) Serial.print(x)
  #define LOGLN(x) Serial.println(x)
  #define LOGF(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG(x)
  #define LOGLN(x)
  #define LOGF(...)
#endif

// ---------------- LED SETUP ----------------
Adafruit_NeoPixel pixels(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define LED_BOOT     0
#define LED_NETWORK  1
#define LED_INTERNET 2

// ---------------- UART ----------------
HardwareSerial SerialAT(1);

// ---------------- FUNCTION DECL ----------------
void setupModemPower();
bool sendAT(String cmd, String expected, int timeout = 3000);
bool waitForNetwork();
void setup_GSM();
void loading_bar(int steps, int delay_ms = 500);

// LED helpers
void setLED(int led, int r, int g, int b);
void clearLEDs();

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Init LEDs
  pixels.begin();
  pixels.setBrightness(50);
  clearLEDs();

  LOGLN("[SYSTEM] Booting...");

  setLED(LED_BOOT, 255, 0, 0); // RED = boot start

  setupModemPower();

  LOGLN("[MODEM] Waiting for full boot...");
  loading_bar(30, 500);   // ~15 sec

  setup_GSM();
}

void loop() {}

// ---------------- LED FUNCTIONS ----------------
void setLED(int led, int r, int g, int b) {
  pixels.setPixelColor(led, pixels.Color(r, g, b));
  pixels.show();
}

void clearLEDs() {
  pixels.clear();
  pixels.show();
}

// ---------------- POWER SEQUENCE ----------------
void setupModemPower() {

  LOGLN("[MODEM] INIT START");

  pinMode(TPS_ENABLE, OUTPUT);
  pinMode(GSM_ENABLE, OUTPUT);
  pinMode(GSM_PWRKEY, OUTPUT);

  digitalWrite(TPS_ENABLE, HIGH);
  LOGLN("[MODEM] TPS ENABLED");
  delay(100);

  digitalWrite(GSM_ENABLE, HIGH);
  LOGLN("[MODEM] GSM ENABLED");
  delay(500);

  // PWRKEY pulse
  digitalWrite(GSM_PWRKEY, LOW);
  delay(100);

  digitalWrite(GSM_PWRKEY, HIGH);
  LOGLN("[MODEM] PWRKEY HIGH");
  delay(1500);

  digitalWrite(GSM_PWRKEY, LOW);
  LOGLN("[MODEM] PWRKEY LOW");

  LOGLN("[MODEM] BOOTING...");
  loading_bar(16, 500);  // ~8 sec boot

  setLED(LED_BOOT, 0, 255, 0); // GREEN = boot done

  LOGLN("[MODEM] INIT DONE");
}

// ---------------- SEND AT ----------------
bool sendAT(String cmd, String expected, int timeout) {

  LOG("\n>> ");
  LOGLN(cmd);

  SerialAT.flush();
  SerialAT.println(cmd);

  long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (response.length() < 500) {
        response += c;
      }
    }
  }

  LOG("<< ");
  LOGLN(response);

  if (response.indexOf(expected) != -1) {
    return true;
  } else {
    LOGLN("[ERROR] Unexpected response");
    return false;
  }
}

// ---------------- NETWORK WAIT ----------------
bool waitForNetwork() {

  LOGLN("[MODEM] Waiting for network...");
  setLED(LED_NETWORK, 255, 0, 0); // RED = searching

  for (int i = 0; i < 15; i++) {

    SerialAT.println("AT+CREG?");
    delay(500);

    String resp = "";
    while (SerialAT.available()) {
      resp += (char)SerialAT.read();
    }

    LOGLN(resp);

    if (resp.indexOf(",1") != -1 || resp.indexOf(",5") != -1) {
      LOGLN("[MODEM] NETWORK REGISTERED");
      setLED(LED_NETWORK, 0, 255, 0); // GREEN = connected
      return true;
    }

    loading_bar(1, 2000);
  }

  LOGLN("\n[MODEM] NETWORK FAILED");
  return false;
}

// ---------------- GSM SETUP ----------------
void setup_GSM() {

  // 1. Modem check
  if (!sendAT("AT", "OK")) return;

  // 2. SIM check
  if (!sendAT("AT+CPIN?", "READY")) return;

  // 3. Signal check
  sendAT("AT+CSQ", "OK");

  // 4. Network registration
  if (!waitForNetwork()) return;

  // 5. Attach GPRS
  if (!sendAT("AT+CGATT=1", "OK")) return;

  // 6. Set APN
  LOGLN("[MODEM] Setting APN...");
  if (!sendAT("AT+CGDCONT=1,\"IP\",\"vi\"", "OK")) return;

  // 7. Internet connect
  setLED(LED_INTERNET, 255, 0, 0); // RED = connecting

  if (!sendAT("AT+NETOPEN", "OK")) {
    LOGLN("[MODEM] NETOPEN failed, retrying...");
    sendAT("AT+NETCLOSE", "OK");
    delay(2000);
    if (!sendAT("AT+NETOPEN", "OK")) return;
  }

  // 8. Get IP
  sendAT("AT+IPADDR", ".");

  setLED(LED_INTERNET, 0, 0, 255); // BLUE = internet ready

  LOGLN("\nINTERNET READY");
}

// ---------------- LOADING BAR ----------------
void loading_bar(int steps, int delay_ms){
  for (int i = 0; i < steps; i++){
    LOG(" > ");

    // blink boot LED for animation
    pixels.setPixelColor(LED_BOOT, pixels.Color(0, 0, 0));
    pixels.show();
    delay(delay_ms / 2);

    pixels.setPixelColor(LED_BOOT, pixels.Color(255, 0, 0));
    pixels.show();
    delay(delay_ms / 2);
  }
  LOGLN("");
}