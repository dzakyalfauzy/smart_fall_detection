#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <math.h>

const char *WIFI_SSID = "KONTRAKAN BARU";
const char *WIFI_PASSWORD = "kuncipintu";

const char *API_BASE_URL = "https://smartfalldetection-production.up.railway.app";
const char *DEVICE_TOKEN = "auunduulA7rlmkXu";

const char *ENDPOINT_SENSOR = "/api/sensor-data";
const char *ENDPOINT_FALL = "/api/fall-detected";
const char *ENDPOINT_SOS = "/api/sos";

#define SDA_PIN 32
#define SCL_PIN 26

#define BUZZER_PIN 33
#define SOS_BUTTON 25

#define BATTERY_PIN 34
float impactThreshold = 2.8;
float movementThreshold = 1.2;

unsigned long immobilityTime = 3000;

bool impactDetected = false;
bool fallAlarm = false;
bool sosAlarm = false;

unsigned long impactTime = 0;
float impactMagnitude = 0.0; // peak G captured at moment of impact

int lastButtonState = HIGH;

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;

bool getAlarmState() { return fallAlarm || sosAlarm; }
void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi Failed");
  }
}

float readBatteryPercentage() {
  float percentage = 76.0;

  Serial.printf("[BATTERY] Baterai: %.1f%%\n", percentage);

  return percentage;
}

void sendSensorData(float mag, float ax, float ay, float az, float battery) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(API_BASE_URL) + ENDPOINT_SENSOR;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.addHeader("Accept", "application/json");

  StaticJsonDocument<256> doc;
  doc["magnitude"] = mag;
  doc["ax"] = ax;
  doc["ay"] = ay;
  doc["az"] = az;
  doc["battery"] = battery;
  doc["status"] = getAlarmState() ? "alarm" : "normal";

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);

  if (httpCode == 200 || httpCode == 201) {
    String responseBody = http.getString();
    StaticJsonDocument<512> resDoc;
    DeserializationError error = deserializeJson(resDoc, responseBody);

    if (!error) {
      bool shouldBuzzerBeOn = resDoc["telemetry"]["command_buzzer"];

      if (getAlarmState() == true && shouldBuzzerBeOn == false) {
        Serial.println("🔕 ALARM DISMISSED FROM DASHBOARD!");

        fallAlarm = false;
        sosAlarm = false;
        impactDetected = false;

        digitalWrite(BUZZER_PIN, LOW);
      }
    }
    Serial.println("📡 Data sent & Sync success");
  } else {
    Serial.printf("⚠️ HTTP Error: %d\n", httpCode);
  }

  http.end();
}

void sendFallAlert(float mag) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(API_BASE_URL) + ENDPOINT_FALL;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  StaticJsonDocument<256> doc;
  doc["event"] = "fall_detected";
  doc["magnitude"] = mag;
  doc["message"] = "Pengguna terdeteksi jatuh!";
  doc["active"] = true;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  Serial.print("HTTP FALL: ");
  Serial.println(httpCode);

  if (httpCode == 200 || httpCode == 201) {
    Serial.println("🆘 FALL alert sent!");
  } else {
    Serial.printf("⚠️ Fall Error: %d\n", httpCode);
  }

  http.end();
}

void sendSOSAlert(bool isActive) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(API_BASE_URL) + ENDPOINT_SOS;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.addHeader("Accept", "application/json");

  StaticJsonDocument<256> doc;
  doc["event"] = isActive ? "sos_active" : "sos_cancelled";
  doc["message"] = isActive ? "Tombol SOS ditekan!" : "Alarm SOS dibatalkan";
  doc["active"] = isActive;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  Serial.print("HTTP SOS: ");
  Serial.println(httpCode);

  if (httpCode == 200 || httpCode == 201) {
    Serial.println("📲 SOS alert sent!");
  } else {
    Serial.printf("⚠️ SOS Error: %d\n", httpCode);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);

  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(SOS_BUTTON, INPUT_PULLUP);

  analogReadResolution(12);

  Serial.println("=================================");
  Serial.println(" SMART FALL DETECTION SYSTEM ");
  Serial.println("=================================");

  connectWiFi();

  Serial.println("✅ SYSTEM READY");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  Wire.beginTransmission(0x68);
  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0) {
    Serial.println("❌ MPU6050 NOT DETECTED");
    delay(1000);
    return;
  }

  Wire.requestFrom(0x68, 6, true);

  int16_t ax_raw = Wire.read() << 8 | Wire.read();
  int16_t ay_raw = Wire.read() << 8 | Wire.read();
  int16_t az_raw = Wire.read() << 8 | Wire.read();

  float ax_g = ax_raw / 16384.0;
  float ay_g = ay_raw / 16384.0;
  float az_g = az_raw / 16384.0;

  float magnitude = sqrt((ax_g * ax_g) + (ay_g * ay_g) + (az_g * az_g));
  Serial.print("MAG: ");
  Serial.println(magnitude);

  int batteryPercentage = (int)readBatteryPercentage();

  if (millis() - lastSendTime >= sendInterval) {
    sendSensorData(magnitude, ax_g, ay_g, az_g, batteryPercentage);
    lastSendTime = millis();
  }

  if (magnitude > impactThreshold && !impactDetected) {
    Serial.println("🚨 IMPACT DETECTED!");
    impactDetected = true;
    impactTime = millis();
    impactMagnitude = magnitude; // save peak G right now, not after immobility
    Serial.printf("Impact Saved: %.3f G\n", impactMagnitude);
  }

  if (impactDetected) {
    unsigned long elapsedTime = millis() - impactTime;

    if (elapsedTime > immobilityTime) {
      if (magnitude < movementThreshold) {
        Serial.println("🆘 FALL DETECTED!");
        fallAlarm = true;
        digitalWrite(BUZZER_PIN, HIGH);
        sendFallAlert(impactMagnitude); // send the peak, not the post-immobility ~0G reading
      } else {
        Serial.println("❌ FALSE ALARM");
      }
      impactDetected = false;
    }
  }

  int currentButtonState = digitalRead(SOS_BUTTON);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    sosAlarm = !sosAlarm;

    if (sosAlarm) {
      Serial.println("🔴 EMERGENCY BUTTON");
    } else {
      Serial.println("🟢 ALERT CANCELLED");
      fallAlarm = false;
      sosAlarm = false;
    }

    digitalWrite(BUZZER_PIN, getAlarmState() ? HIGH : LOW);
    sendSOSAlert(sosAlarm);
    sendSensorData(magnitude, ax_g, ay_g, az_g, batteryPercentage);
  }

  lastButtonState = currentButtonState;

  digitalWrite(BUZZER_PIN, getAlarmState() ? HIGH : LOW);
}
