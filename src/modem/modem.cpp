#include "modem.h"
#include <Arduino.h>

#define SerialAT Serial1

#define GSM_AUTOBAND_MIN 9600
#define GSM_AUTOBAND_MAX 115200

#define GPIO_PWRKEY     7
#define GPIO_GSM_ENABLE 10
#define GPIO_GSM_RX 1
#define GPIO_GSM_TX 0

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

TinyGsmClient client(modem);

void initUARTs(){
    Serial.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, GPIO_GSM_RX , GPIO_GSM_TX);
}

void initGPIO(){
    pinMode(GPIO_PWRKEY, OUTPUT);
    pinMode(GPIO_GSM_ENABLE, OUTPUT);
}

void enableGSM()   { digitalWrite(GPIO_GSM_ENABLE, HIGH); }
void disableGSM()  { digitalWrite(GPIO_GSM_ENABLE, LOW); }

void initGSMpower(){
    digitalWrite(GPIO_PWRKEY, HIGH);
    delay(500);
    digitalWrite(GPIO_PWRKEY, LOW);   // power on (low pulse)
    delay(1000);
    digitalWrite(GPIO_PWRKEY, HIGH);
}

void restartModem(){
    modem.restart();
    delay(1000);
}

bool waitForNetwork(unsigned long timeout_ms){
    return modem.waitForNetwork(timeout_ms);
}

bool connectGPRS(const char* apn, const char* user, const char* pass){
    return modem.gprsConnect(apn, user, pass);
}

bool isGPRSConnected(){
    return modem.isGprsConnected();
}

void modem_gnss_power(void) {
    SerialMon.println("Starting GPS...");
    modem.sendAT(GF("+CGNSSPWR=1"));
    if (modem.waitResponse(10000UL, GF("+CGNSSPWR:")) != 1) {
        SerialMon.println("GPS power on failed");
        return;
    }
    modem.sendAT(GF("+CGNSSMODE=3"));   // GPS + GLONASS
    modem.waitResponse(1000UL);
    modem.sendAT(GF("+CGPSCOLD"));      // Cold start
    modem.waitResponse(1000UL);
    modem.sendAT(GF("+CGNSSTST=1"));    // Continuous output
    modem.waitResponse(1000UL);
    SerialMon.println("GPS started");
}

void modem_get_gps(float *lat, float *lon) {
    String res;
    modem.sendAT(GF("+CGPSINFO"));
    if (!modem.waitResponse(10000L, res)) {
        *lat = 0.0;
        *lon = 0.0;
        return;
    }
    // Parse: +CGPSINFO: ddmm.mmmm,N,ddmm.mmmm,E,...
    // Example: +CGPSINFO: 3723.2475,N,12158.3416,W,...
    int start = res.indexOf(':') + 2;
    int comma1 = res.indexOf(',', start);
    String latStr = res.substring(start, comma1);
    int comma2 = res.indexOf(',', comma1 + 1);
    String latDir = res.substring(comma1 + 1, comma2);
    int comma3 = res.indexOf(',', comma2 + 1);
    String lonStr = res.substring(comma2 + 1, comma3);
    int comma4 = res.indexOf(',', comma3 + 1);
    String lonDir = res.substring(comma3 + 1, comma4);

    // Convert ddmm.mmmm to decimal degrees
    if (latStr.length() > 0) {
        float latVal = latStr.toFloat();
        int deg = (int)(latVal / 100);
        float minutes = latVal - deg * 100;
        *lat = deg + minutes / 60.0;
        if (latDir == "S") *lat = -*lat;
    }
    if (lonStr.length() > 0) {
        float lonVal = lonStr.toFloat();
        int deg = (int)(lonVal / 100);
        float minutes = lonVal - deg * 100;
        *lon = deg + minutes / 60.0;
        if (lonDir == "W") *lon = -*lon;
    }
}

void modem_battery_get_status(float *vbat) {
    String res;
    modem.sendAT(GF("+CBC"));
    if (!modem.waitResponse(5000L, res)) {
        *vbat = 0.0;
        return;
    }
    // Response: +CBC: bcs, bcl, voltage
    // Extract voltage (last field, ends with 'V')
    int vStart = res.lastIndexOf(',') + 1;
    int vEnd = res.indexOf('V', vStart);
    if (vStart > 0 && vEnd > vStart) {
        *vbat = res.substring(vStart, vEnd).toFloat();
    } else {
        *vbat = 0.0;
    }
}