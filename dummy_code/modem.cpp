#include "modem.h"

TinyGsm modem(Serial1);

static int modem_state = 0x00;

// helper fucntions
void loading_bar(int x){
  for (int i = 0; i < x;i++){
    Serial.printf(" > ");
    delay(500);
  }
  Serial.println();
}

/*********************************************
 ************^* MQTT FUNCTIONS ***************
 *********************************************/
static void modem_mqtt_accquire(const uint8_t idx, const String client_name) {
  String at_cmd = "+CMQTTACCQ=" + String(idx) + ",\"" + client_name + "\",0";
  modem.sendAT(GF(at_cmd));
  if (modem.waitResponse(1000UL) == 1)
    Serial.printf("MQTT idx: %d accquired\n", idx);
}

static bool modem_mqtt_connect(const uint8_t idx, const String url, const String uname) {
  String cmd = "+CMQTTCONNECT=" + String(idx) + ",\"" + url + "\",20,0,\"" + uname + "\"";
  modem.sendAT(GF(cmd));
  delay(100);
  if (modem.waitResponse(5000UL) == 1) {
    Serial.printf("MQTT idx: %d connected\n", idx);
    return true;
  }
  return false;
}

void modem_mqtt_start(void) {
  if (!(modem_state & MODEM_STATE_READY)) {
    Serial.printf("Call modem_init() before mqtt_start\n");
    return;
  }

  modem.sendAT(GF("+CMQTTSTART"));
  if (modem.waitResponse("+CMQTTSTART: 0") == 1)
    Serial.printf("Modem mqtt service started succesfully!\n");

  modem_mqtt_accquire(0, MQTT_DEV_NAME);
  if (!modem_mqtt_connect(0, MQTT_SERVER_NAME, MQTT_TOKEN))
    return;

  modem_state |= MODEM_MQTT_READY;
}

static void modem_mqtt_set_topic(const uint8_t idx, const String topic) {
  int cmd_len = topic.length();
  String cmd = "+CMQTTTOPIC=" + String(idx) + "," + String(cmd_len);
  Serial.println("AT" + cmd);
  modem.sendAT(GF(cmd));

  // ✅ Wait a bit instead of scanning '>'
  delay(1000);

  modem.stream.write(topic.c_str(), cmd_len);
  modem.stream.flush();

  Serial.printf("Set MQTT Topic to %s\n", topic.c_str());

  if (modem.waitResponse(5000, "OK") != 1)
    Serial.println("Error: Topic set failed (no OK)");
}

static int modem_mqtt_set_payload(const uint8_t idx, const String payload) {
  int cmd_len = payload.length();
  String cmd = "+CMQTTPAYLOAD=" + String(idx) + "," + String(cmd_len);
  Serial.println("AT" + cmd);

  modem.sendAT(GF(cmd));

  // ✅ Instead of waiting for '>', just wait a bit
  delay(1000);

  // Send the payload string
  modem.stream.write(payload.c_str(), cmd_len);
  modem.stream.flush();

  Serial.printf("Sent MQTT payload: %s\n", payload.c_str());

  // Wait for OK from modem
  if (modem.waitResponse(5000, "OK") != 1) {
    Serial.println("Error: Payload set failed (no OK)");
    return -1;
  }

  return 0;
}

static void modem_mqtt_publish(const uint8_t idx) {
  String cmd = "+CMQTTPUB=" + String(idx) + ",0,100";
  modem.sendAT(GF(cmd));
  Serial.println(cmd);
  cmd = "";
  if (modem.waitResponse(1000UL, cmd) != 1)
    Serial.println("Error: publish set failed");

  Serial.print("Publish:");
  Serial.println(cmd);
}

int modem_mqtt_send(const uint8_t idx, const String topic, const String Payload) {
  if (!(modem_state & MODEM_MQTT_READY)) {
    modem_mqtt_start();
    return 1;
  }

  modem_mqtt_set_topic(idx, topic);
  if (modem_mqtt_set_payload(idx, Payload) == 0)
    modem_mqtt_publish(idx);

  return 0;
}

// Rewritten function to handle incoming MQTT messages as unsolicited result codes (URCs)
String modem_mqtt_read_payload(const uint8_t idx) {
  String payload = "";
  // Check the serial stream for a URC from the modem
  if (modem.stream.available()) {
    String buffer = modem.stream.readStringUntil('\n');
    buffer.trim();

    // The A7672 uses a multi-line URC for received messages
    if (buffer.startsWith("+CMQTTRXSTART: ")) {
      // The modem will now send the topic and payload automatically.
      // We need to read them from the serial stream.

      // Read and discard the topic line
      while (!modem.stream.available())
        delay(10);

      String topic_line = modem.stream.readStringUntil('\n');
      Serial.print("[MQTT] Received topic info: ");
      Serial.println(topic_line);

      // Read the payload line
      while (!modem.stream.available())
        delay(10);

      String payload_line = modem.stream.readStringUntil('\n');
      Serial.print("[MQTT] Received payload info: ");
      Serial.println(payload_line);

      // Extract the payload content (it's the last part of the URC)
      int payload_start = payload_line.indexOf("\"") + 1;
      int payload_end = payload_line.lastIndexOf("\"");
      if (payload_start > 0 && payload_end > payload_start)
        payload = payload_line.substring(payload_start, payload_end);
    }
  }

  if (payload.length() > 0) {
    Serial.print("[MQTT] Successfully read payload: ");
    Serial.println(payload);
  }
  return payload;
}

int modem_get_status() {
  return modem_state;
}

void modem_battery_get_status(float *vbat) {
  if (!(modem_state & MODEM_STATE_READY)) {
    Serial.printf("Call modem_init() before mqtt_start\n");
    return;
  }
  String res = "";

  modem.sendAT("+CBC");
  if (!modem.waitResponse(1000L, res)) {
    *vbat = 0.00;
    return;
  }

  res = res.substring(res.indexOf(':') + 1, res.indexOf('V'));
  modem.waitResponse();
  *vbat = res.toFloat();
}

void modem_gnss_power() {
  if (modem_state & MODEM_GNSS_READY)
    return;

  Serial.printf("Calling gnss init!");

  String cmd = "+CGNSSPWR=1";
  modem.sendAT(GF(cmd));
  if (modem.waitResponse(10000UL, GF("+CGNSSPWR:")) != 1) {
    Serial.printf("GPS %s start failed\n", cmd.c_str());
    return;
  }
  Serial.printf("GPS %s start done\n", cmd.c_str());

  cmd = "+CGNSSMODE=3";
  modem.sendAT(GF(cmd));
  if (modem.waitResponse(1000UL) != 1) {
    Serial.printf("GPS %s start failed\n", cmd.c_str());
    return;
  }
  Serial.printf("GPS %s start done\n", cmd.c_str());

  cmd = "+CGPSCOLD";
  modem.sendAT(GF(cmd));
  if (modem.waitResponse(1000UL) != 1) {
    Serial.printf("GPS %s start failed\n", cmd.c_str());
    return;
  }
  Serial.printf("GPS %s start done\n", cmd.c_str());

  cmd = "+CGNSSTST=1";
  modem.sendAT(GF(cmd));
  if (modem.waitResponse(1000UL) != 1) {
    Serial.printf("GPS %s start failed\n", cmd.c_str());
    return;
  }
  Serial.printf("GPS %s start done\n", cmd.c_str());

  modem_state |= MODEM_GNSS_READY;
}

void modem_get_gps(float *lat, float *lon) {
  String res = "";
  String ret = "";
  int startIdx = 0;
  int endIdx = 0;
  float temp_val = 0.00;
  int tempDeg = 0;

  if (!(modem_state & MODEM_GNSS_READY)) {
    Serial.printf("Gnss not ready!\n");
    return;
  }

  modem.sendAT(GF("+CGPSINFO"));
  if (!modem.waitResponse(1000L, res)) {
    *lat = 0.00;
    *lon = 0.00;
    return;
  }

  startIdx = res.indexOf(':') + 2;  // Skip ": "
  endIdx = res.indexOf(',', startIdx);
  ret = res.substring(startIdx, endIdx);
  if (ret.length() > 0) {
    temp_val = ret.toFloat();
    tempDeg = (int)(ret.toFloat() / 100);
    *lat = tempDeg + (temp_val - (tempDeg * 100)) / 60;  // TODO: fix accurately
  } else {
    *lat = 0.00;
  }

  startIdx = endIdx + 1;  // Skip 'N'
  startIdx = res.indexOf(',', startIdx) + 1;
  endIdx = res.indexOf(',', startIdx);
  ret = res.substring(startIdx, endIdx);
  if (ret.length() > 0) {
    temp_val = ret.toFloat();
    tempDeg = (int)(ret.toFloat() / 100);
    *lon = tempDeg + (temp_val - (tempDeg * 100)) / 60;  // TODO: fix accurately
  } else {
    *lon = 0.00;
  }

  return;
}

/*********************************************
 ************* MODEM FUNCTIONS ***************
 *********************************************/
void modem_set_gsm_state(uint8_t enable, int delay_ms) {
  digitalWrite(MODEM_GSMKEY, enable ? 1 : 0);
  delay(delay_ms);
}

static void modem_set_power_state(bool power_on) {
  if (!power_on) {
    modem.poweroff();
    modem_state = 0x00;
    return;
  }

  if (modem.testAT()) {
    Serial.printf("Modem Already up! restarting\n");
    modem.poweroff();
    modem_set_gsm_state(0, 100);
    modem_set_gsm_state(1, 200);
  }

  digitalWrite(MODEM_PWRKEY, HIGH);
  loading_bar(2);
  digitalWrite(MODEM_PWRKEY, LOW);
  loading_bar(16);
}

void __modem_lazy_init(void *arg) {
  // Force reboot modem
reboot:
  modem_set_power_state(false);
  modem_set_gsm_state(1, 150);
  modem_set_power_state(true);  // Turn on modem and wait 8s

  if (!modem.testAT()) {
    Serial.println("Modem not responding, rebooting...");
    goto reboot;
  }

  String modemInfo = modem.getModemInfo();
  Serial.printf("Modem Info: %s\n", modemInfo.c_str());

  Serial.println("Checking SIM...");
  if (modem.getSimStatus() != 3) {
    Serial.println("SIM not ready, rebooting...");
    goto reboot;
  }

  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork(60000)) {
    Serial.println("Network registration failed, rebooting...");
    goto reboot;
  }

  Serial.println("Network registered");

  // here i have to add the codes to start the sim and get register itself to
  // cellular tower

  Serial.println("Attaching GPRS...");
  if (!modem.gprsConnect("vi", "", "")) {
    Serial.println("GPRS attach failed, rebooting...");
    goto reboot;
  }

  Serial.println("GPRS connected");

  // while (!(modem.waitResponse(GF("+CGEV:")) == 1)) {
  //   Serial.printf("waiting for pdp...\n");
  //   delay(200);
  // }

  // while (!(modem.waitResponse(GF("PB DONE")) == 1)) {
  //   Serial.printf("waiting for pb done...\n");
  //   delay(200);
  // }

  Serial.printf("Simcard network connected succesfully\n");

  modem_state |= MODEM_STATE_READY;

  loading_bar(4);

  int retry = 5;

  while (!(modem_state & MODEM_GNSS_READY)) {
    modem_gnss_power();
    loading_bar(2);
    if (!retry)
      break;

    retry--;
  }

  if (retry == 0){
    Serial.println("GNSS failed, rebooting...");
    goto reboot;

  }
    
  Serial.println("Modem fully initialized");

  vTaskDelete(NULL);

  return;
}

void modem_init() {
  if (modem_state & MODEM_STATE_READY)
    return;

  xTaskCreate(__modem_lazy_init, "modem_lazy_init", 8192, NULL, 0, NULL);
}

void modem_deinit(void) {
  if (modem_state & MODEM_STATE_READY)
    modem_set_power_state(false);
}
