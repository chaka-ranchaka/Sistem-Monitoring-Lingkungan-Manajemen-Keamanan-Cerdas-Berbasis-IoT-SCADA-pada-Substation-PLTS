/*
 * Monitoring & Keamanan Cerdas IoT/SCADA Ruang Inverter PLTS
 * ESP32 (simulasi Wokwi) -> MQTT broker.hivemq.com -> Node-RED (HMI)
 *
 * Pin, aturan R1–R11, dan topik MQTT mengikuti 02_Perancangan/perancangan_sistem.md.
 * LM35, BH1750, dan SCT-013 disimulasikan dengan custom chip (*.chip.c).
 */
#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// 1 = simulasi Wokwi (ADC virtual linear, referensi 5 V), 0 = ESP32 asli (kalibrasi eFuse)
#define SIMULASI_WOKWI 1

// ---------------- Koneksi ----------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
#define TOPIC_PREFIX "pltsiot/2300118/"   // kode unik: NIM

const char* T_SENSOR = TOPIC_PREFIX "sensor";
const char* T_STATUS = TOPIC_PREFIX "status";
const char* T_ALARM  = TOPIC_PREFIX "alarm";
const char* T_ONLINE = TOPIC_PREFIX "online";
const char* T_ACK    = TOPIC_PREFIX "ack";
const char* T_CMD    = TOPIC_PREFIX "cmd/#";
const char* T_CMD_MODE  = TOPIC_PREFIX "cmd/mode";
const char* T_CMD_ARM   = TOPIC_PREFIX "cmd/arm";
const char* T_CMD_RELAY = TOPIC_PREFIX "cmd/relay";
const char* T_CMD_DOOR  = TOPIC_PREFIX "cmd/door";

// ---------------- Pin ----------------
const int PIN_LM35   = 34;
const int PIN_CT     = 35;
const int PIN_TRIG   = 18;
const int PIN_ECHO   = 19;
const int PIN_PIR    = 27;
const int PIN_SDA    = 21;
const int PIN_SCL    = 22;
const int PIN_SERVO  = 13;
const int PIN_BUZZER = 23;
const int PIN_LED    = 2;

enum { FAN = 0, LAMP = 1, PUMP = 2, FAN2 = 3 };          // indeks = CH - 1
const int RELAY_PIN[4] = {26, 25, 33, 32};               // CH1..CH4
const char* RELAY_NAME[4] = {"fan", "lamp", "pump", "fan2"};
const bool RELAY_ACTIVE_LOW = true;   // modul relay 4-CH aktif LOW

// ---------------- Parameter ----------------
const float TANK_HEIGHT_CM = 100.0;
const float CT_RATIO = 2000.0, R_BURDEN = 22.0;
const float V_GRID = 220.0, POWER_FACTOR = 0.9;   // asumsi untuk perhitungan daya
const uint8_t BH1750_ADDR = 0x23;

const float T_FAN_ON = 35, T_FAN_OFF = 32;          // R1, R2
const float T_FAN2_ON = 40, T_FAN2_OFF = 37;        // R3
const float T_OVERHEAT = 45, T_OVERHEAT_CLR = 43;   // R4
const float LEVEL_LOW = 20, LEVEL_HIGH = 80;        // R8, R9
const float I_MAX = 2.0;                            // R11

const unsigned long LAMP_HOLD_MS      = 30000;  // R5
const unsigned long DOOR_OPEN_MS      = 10000;  // R7
const unsigned long INTRUSION_HOLD_MS = 30000;  // R6: alarm reda 30 s setelah gerakan terakhir
const unsigned long READ_MS = 1000, PUBLISH_MS = 2000, STATUS_MS = 10000;

const int SERVO_LOCKED = 0, SERVO_OPEN = 90;
const int SERVO_CH = 2;   // kanal LEDC untuk core ESP32 2.x

// ---------------- State ----------------
struct {
  float temp = 0, dist = 0, level = 0, lux = 0, irms = 0, power = 0, energyWh = 0;
  bool pir = false;
} s;

bool autoMode = true;
bool armed = true;
bool relay[4] = {false, false, false, false};
bool manualRelay[4] = {false, false, false, false};
bool doorOpen = false;
bool buzzerOn = false;
bool overheat = false, intrusion = false, levelLow = false, overCurrent = false;
bool motionSeen = false;

unsigned long lastMotion = 0, doorOpenedAt = 0;
unsigned long lastRead = 0, lastPub = 0, lastStatusPub = 0, lastEnergy = 0;
unsigned long lastWifiTry = 0, lastMqttTry = 0;
String lastStatus;

WiFiClient net;
PubSubClient mqtt(net);

// ---------------- Aktuator ----------------
void servoWriteDeg(int deg) {
  uint32_t us = map(deg, 0, 180, 500, 2400);
  uint32_t duty = us * 65535UL / 20000UL;   // 50 Hz, resolusi 16 bit
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO, duty);
#else
  ledcWrite(SERVO_CH, duty);
#endif
}

void lockDoor()  { doorOpen = false; servoWriteDeg(SERVO_LOCKED); }
void openDoor()  { doorOpen = true; doorOpenedAt = millis(); servoWriteDeg(SERVO_OPEN); }

void writeRelay(int i, bool on) {
  digitalWrite(RELAY_PIN[i], (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

// ---------------- MQTT publish ----------------
void publishAlarm(const char* type, bool active, float value) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"type\":\"%s\",\"active\":%d,\"value\":%.2f,\"uptime\":%lu}",
           type, active, value, millis() / 1000);
  mqtt.publish(T_ALARM, buf);
  Serial.printf("[ALARM] %s\r\n", buf);
}

String buildStatus() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"mode\":\"%s\",\"arm\":\"%s\",\"fan\":%d,\"lamp\":%d,\"pump\":%d,\"fan2\":%d,"
           "\"door\":\"%s\",\"buzzer\":%d,\"overheat\":%d,\"intrusi\":%d,\"levelLow\":%d,\"overCurrent\":%d}",
           autoMode ? "AUTO" : "MANUAL", armed ? "ARMED" : "DISARMED",
           relay[FAN], relay[LAMP], relay[PUMP], relay[FAN2],
           doorOpen ? "OPEN" : "LOCKED", buzzerOn, overheat, intrusion, levelLow, overCurrent);
  return String(buf);
}

void publishStatus(bool force) {
  String st = buildStatus();
  if (force || st != lastStatus || millis() - lastStatusPub >= STATUS_MS) {
    if (mqtt.publish(T_STATUS, st.c_str(), true)) {
      lastStatus = st;
      lastStatusPub = millis();
    }
  }
}

void publishSensors() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"temp\":%.2f,\"dist\":%.1f,\"level\":%.1f,\"pir\":%d,\"lux\":%.1f,"
           "\"irms\":%.3f,\"power\":%.1f,\"energy\":%.4f,\"uptime\":%lu}",
           s.temp, s.dist, s.level, s.pir, s.lux, s.irms, s.power, s.energyWh, millis() / 1000);
  mqtt.publish(T_SENSOR, buf);
  Serial.println(buf);
}

void sendAck(long id, bool done, const char* reason) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"id\":%ld,\"done\":%d,\"reason\":\"%s\"}", id, done, reason);
  mqtt.publish(T_ACK, buf);
}

// ---------------- Sensor ----------------
// Wokwi memodelkan ADC ESP32 linear dengan referensi 5 V (raw = V / 5 V × 4095).
// ESP32 asli tidak linear, jadi di hardware dipakai analogReadMilliVolts() yang terkalibrasi.
float readMilliVolts(int pin) {
#if SIMULASI_WOKWI
  return analogRead(pin) * 5000.0 / 4095.0;
#else
  return analogReadMilliVolts(pin);
#endif
}

float readTemp() {
  float sum = 0;
  for (int i = 0; i < 16; i++) sum += readMilliVolts(PIN_LM35);
  float mv = sum / 16.0;
  return mv / 10.0;   // LM35: 10 mV/°C
}

float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000);
  if (us == 0) return -1;      // di luar jangkauan
  return us * 0.0343 / 2.0;
}

void bh1750Begin() {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(0x01);   // power on
  Wire.endTransmission();
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(0x10);   // continuous high-resolution mode (1 lx)
  Wire.endTransmission();
}

float readLux() {
  if (Wire.requestFrom(BH1750_ADDR, (uint8_t)2) != 2) return -1;
  uint16_t raw = (Wire.read() << 8) | Wire.read();
  return raw / 1.2;
}

// Arus RMS: sampel selama 5 siklus penuh 50 Hz, buang offset bias (rata-rata), lalu RMS.
float readIrms() {
  const unsigned long WINDOW_US = 100000;
  double sum = 0, sumSq = 0;
  uint32_t n = 0;
  unsigned long t0 = micros();
  while (micros() - t0 < WINDOW_US) {
    float mv = readMilliVolts(PIN_CT);
    sum += mv;
    sumSq += (double)mv * mv;
    n++;
  }
  if (n == 0) return 0;
  double mean = sum / n;
  double var = sumSq / n - mean * mean;
  if (var < 0) var = 0;
  double vrms = sqrt(var) / 1000.0;
  double irms = vrms / R_BURDEN * CT_RATIO;
  return irms < 0.05 ? 0 : irms;   // abaikan derau kuantisasi ADC
}

void readSensors() {
  s.temp = readTemp();

  float d = readDistanceCm();
  if (d >= 0) {
    s.dist = d;
    s.level = constrain((TANK_HEIGHT_CM - d) / TANK_HEIGHT_CM * 100.0, 0.0, 100.0);
  }

  s.pir = digitalRead(PIN_PIR) == HIGH;

  float lux = readLux();
  if (lux >= 0) s.lux = lux;

  s.irms = readIrms();
  s.power = V_GRID * s.irms * POWER_FACTOR;
  unsigned long now = millis();
  if (lastEnergy != 0) s.energyWh += s.power * (now - lastEnergy) / 3600000.0;
  lastEnergy = now;
}

// ---------------- Logika otomasi ----------------
void decide() {
  unsigned long now = millis();

  // R4 — proteksi overheat, aktif di mode AUTO maupun MANUAL
  if (!overheat && s.temp >= T_OVERHEAT) { overheat = true; publishAlarm("OVERHEAT", true, s.temp); }
  else if (overheat && s.temp <= T_OVERHEAT_CLR) { overheat = false; publishAlarm("OVERHEAT", false, s.temp); }

  // R6 — intrusi saat ARMED (keamanan diatur oleh ARM/DISARM, tidak bergantung mode)
  if (s.pir) { lastMotion = now; motionSeen = true; }
  if (armed && s.pir && !intrusion) {
    intrusion = true;
    lockDoor();
    publishAlarm("INTRUSI", true, 1);
  } else if (intrusion && (!armed || now - lastMotion > INTRUSION_HOLD_MS)) {
    intrusion = false;
    publishAlarm("INTRUSI", false, 0);
  }

  // R8 (peringatan) & R11 — alarm informasi
  bool lowNow = s.level < LEVEL_LOW;
  if (lowNow != levelLow) { levelLow = lowNow; publishAlarm("LEVEL_RENDAH", levelLow, s.level); }
  bool ocNow = s.irms > I_MAX;
  if (ocNow != overCurrent) { overCurrent = ocNow; publishAlarm("ARUS_LEBIH", overCurrent, s.irms); }

  if (autoMode) {
    // R1, R2 — kipas CH1 dengan histeresis 3 °C
    if (s.temp >= T_FAN_ON) relay[FAN] = true;
    else if (s.temp <= T_FAN_OFF) relay[FAN] = false;
    // R3 — kipas tahap 2 CH4
    if (s.temp >= T_FAN2_ON) relay[FAN2] = true;
    else if (s.temp <= T_FAN2_OFF) relay[FAN2] = false;
    // R5 — lampu menyala sampai 30 s setelah gerakan terakhir
    relay[LAMP] = motionSeen && (now - lastMotion < LAMP_HOLD_MS);
    // R8, R9 — pompa reservoir
    if (s.level < LEVEL_LOW) relay[PUMP] = true;
    else if (s.level >= LEVEL_HIGH) relay[PUMP] = false;
  } else {
    for (int i = 0; i < 4; i++) relay[i] = manualRelay[i];
  }

  if (overheat) { relay[FAN] = true; relay[FAN2] = true; }   // R4 override

  // R7 — pintu terkunci otomatis 10 s setelah dibuka
  if (doorOpen && now - doorOpenedAt >= DOOR_OPEN_MS) lockDoor();
}

void applyOutputs() {
  for (int i = 0; i < 4; i++) writeRelay(i, relay[i]);

  bool buz = overheat || intrusion;
  if (buz != buzzerOn) {
    buzzerOn = buz;
    if (buz) tone(PIN_BUZZER, 2000);
    else noTone(PIN_BUZZER);
  }

  if (mqtt.connected()) publishStatus(false);
}

// ---------------- Perintah dari HMI ----------------
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("[CMD] %s = %s\r\n", topic, msg.c_str());

  if (t == T_CMD_MODE) {
    if (msg == "AUTO") autoMode = true;
    else if (msg == "MANUAL") {
      autoMode = false;
      for (int i = 0; i < 4; i++) manualRelay[i] = relay[i];   // mulai dari kondisi sekarang
    }
  } else if (t == T_CMD_ARM) {
    if (msg == "ARMED") armed = true;
    else if (msg == "DISARMED") armed = false;
  } else if (t == T_CMD_RELAY) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return;
    int ch = doc["ch"] | 0;
    JsonVariant st = doc["state"];
    bool state = st.is<bool>() ? st.as<bool>() : st.as<int>() != 0;   // terima 1/0 atau true/false
    long id = doc["id"] | 0L;
    if (ch < 1 || ch > 4) { sendAck(id, false, "CH_INVALID"); return; }
    if (autoMode) { sendAck(id, false, "MODE_AUTO"); return; }
    manualRelay[ch - 1] = state;
    decide();
    applyOutputs();
    bool done = relay[ch - 1] == state;
    sendAck(id, done, done ? "OK" : "OVERRIDE_R4");
    return;
  } else if (t == T_CMD_DOOR) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return;
    String action = doc["action"] | "";
    long id = doc["id"] | 0L;
    if (action == "OPEN") {
      if (armed) { sendAck(id, false, "ARMED"); return; }
      openDoor();
    } else if (action == "LOCK" || action == "CLOSE") {
      lockDoor();
    } else {
      sendAck(id, false, "ACTION_INVALID");
      return;
    }
    applyOutputs();
    sendAck(id, true, "OK");
    return;
  }

  decide();
  applyOutputs();
}

// ---------------- Koneksi ----------------
void connectMqtt() {
  String clientId = "pltsiot-2300118-" + String((uint32_t)esp_random(), HEX);
  Serial.print("MQTT connect... ");
  if (mqtt.connect(clientId.c_str(), nullptr, nullptr, T_ONLINE, 1, true, "0")) {
    Serial.println("OK");
    mqtt.publish(T_ONLINE, "1", true);
    mqtt.subscribe(T_CMD, 1);
    publishStatus(true);
  } else {
    Serial.printf("gagal, rc=%d\r\n", mqtt.state());
  }
}

void maintainConnection() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiTry >= 10000) {
      lastWifiTry = now;
      Serial.println("Wi-Fi reconnect...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
    }
  } else if (!mqtt.connected() && now - lastMqttTry >= 3000) {
    lastMqttTry = now;
    connectMqtt();
  }
  digitalWrite(PIN_LED, (WiFi.status() == WL_CONNECTED && mqtt.connected()) ? HIGH : LOW);
}

// ---------------- Setup & loop ----------------
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    writeRelay(i, false);            // set level OFF sebelum jadi output (cegah relay berkedip)
    pinMode(RELAY_PIN[i], OUTPUT);
    writeRelay(i, false);
  }
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);   // rentang masukan ±0–3,1 V

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SERVO, 50, 16);
#else
  ledcSetup(SERVO_CH, 50, 16);
  ledcAttachPin(PIN_SERVO, SERVO_CH);
#endif
  lockDoor();

  Wire.begin(PIN_SDA, PIN_SCL);
  bh1750Begin();

  Serial.print("Wi-Fi connect");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " gagal, dicoba lagi di loop");
  lastWifiTry = millis();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);
}

void loop() {
  maintainConnection();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastRead >= READ_MS) {
    lastRead = now;
    readSensors();
    decide();
    applyOutputs();
  }
  if (mqtt.connected() && now - lastPub >= PUBLISH_MS) {
    lastPub = now;
    publishSensors();
  }
}
