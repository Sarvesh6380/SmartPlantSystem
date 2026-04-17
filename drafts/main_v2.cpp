#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Wi-Fi and Firebase
#define WIFI_SSID     "1234ABCD"
#define WIFI_PASSWORD "ABCD1234"
#define API_KEY       "AIzaSyBcfKljXgucdPWl6J-EXvwZM0snNCjBedw"
#define DATABASE_URL  "https://smartplantcare-1f8f3-default-rtdb.firebaseio.com"

// ESP32-S3 pin mapping
#define SOIL_PIN   4
#define LDR_PIN    1
#define DHT_PIN    5
#define RELAY_PIN  7
#define OLED_SDA   8
#define OLED_SCL   9
#define DHT_TYPE   DHT11

// Analog calibration
#define SOIL_DRY_RAW    2800
#define SOIL_WET_RAW    1200
#define LDR_DARK_RAW    3800
#define LDR_BRIGHT_RAW   400
#define PUMP_THRESHOLD    30

constexpr unsigned long SENSOR_INTERVAL_MS = 2000;
constexpr unsigned long FIREBASE_INTERVAL_MS = 15000;
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr int ADC_SAMPLES = 16;

struct SensorState {
  float temperature = 0.0f;
  float humidity = 0.0f;
  int soilPercent = 0;
  int lightPercent = 0;
  int rawSoil = 0;
  int rawLight = 0;
  bool pumpOn = false;
  bool dhtValid = false;
};

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

SensorState state;
bool oledReady = false;
bool firebaseReady = false;

unsigned long lastSensorRead = 0;
unsigned long lastFirebasePush = 0;
unsigned long lastDhtRead = 0;

int readAveragedADC(int pin, int samples = ADC_SAMPLES) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(2);
  }
  return static_cast<int>(total / samples);
}

bool readDhtValues(float &temperature, float &humidity) {
  if (millis() - lastDhtRead < 2000) {
    return false;
  }

  lastDhtRead = millis();

  const float t = dht.readTemperature();
  const float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println("[WARN] DHT read failed");
    return false;
  }

  if (t < -10 || t > 80 || h < 0 || h > 100) {
    Serial.printf("[WARN] DHT out of range: T=%.1f H=%.1f\n", t, h);
    return false;
  }

  temperature = t;
  humidity = h;
  return true;
}

void updatePump(bool pumpOn) {
  digitalWrite(RELAY_PIN, pumpOn ? LOW : HIGH);
}

void updateDisplay(const SensorState &data) {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Temp: %.1f C\n", data.temperature);
  display.printf("Hum : %.0f %%\n", data.humidity);
  display.printf("Soil: %d %%\n", data.soilPercent);
  display.printf("LDR : %d %%\n", data.lightPercent);
  display.printf("Pump: %s\n", data.pumpOn ? "ON" : "OFF");
  display.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "ERR");
  display.display();
}

void printSensorLog(const SensorState &data) {
  Serial.printf("[Sensor] Temp=%.1fC Hum=%.1f%% Soil=%d%% Light=%d%% Pump=%s\n",
                data.temperature,
                data.humidity,
                data.soilPercent,
                data.lightPercent,
                data.pumpOn ? "ON" : "OFF");

  Serial.printf("[RAW] SoilADC=%d LightADC=%d Dry=%d Wet=%d Dark=%d Bright=%d\n",
                data.rawSoil,
                data.rawLight,
                SOIL_DRY_RAW,
                SOIL_WET_RAW,
                LDR_DARK_RAW,
                LDR_BRIGHT_RAW);
}

void pushToFirebase(const SensorState &data) {
  if (!Firebase.ready()) {
    return;
  }

  FirebaseJson json;
  json.set("temp", data.temperature);
  json.set("humidity", data.humidity);
  json.set("soil", data.soilPercent);
  json.set("light", data.lightPercent);
  json.set("pump", data.pumpOn ? "ON" : "OFF");

  if (Firebase.RTDB.setJSON(&fbdo, "/PlantData", &json)) {
    Serial.println("[Firebase] Cloud sync: OK");
  } else {
    Serial.println("[Firebase] Cloud sync FAILED: " + fbdo.errorReason());
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Connection timeout. Running offline.");
  }
}

void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("[Firebase] Anonymous auth: OK");
    firebaseReady = true;
  } else {
    Serial.printf("[Firebase] Auth failed: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n--- SMART IRRIGATION V2 ---");

  analogSetAttenuation(ADC_11db);
  pinMode(RELAY_PIN, OUTPUT);
  updatePump(false);

  Wire.begin(OLED_SDA, OLED_SCL);
  dht.begin();
  delay(2000);

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledReady = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Smart Irrigation");
    display.println("Booting...");
    display.display();
  } else {
    Serial.println("[WARN] OLED not detected");
  }

  connectWiFi();
  initFirebase();
}

void loop() {
  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();

    float newTemperature = state.temperature;
    float newHumidity = state.humidity;
    state.dhtValid = readDhtValues(newTemperature, newHumidity);
    if (state.dhtValid) {
      state.temperature = newTemperature;
      state.humidity = newHumidity;
    }

    state.rawSoil = readAveragedADC(SOIL_PIN);
    state.rawLight = readAveragedADC(LDR_PIN);

    state.soilPercent = constrain(map(state.rawSoil, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
    state.lightPercent = constrain(map(state.rawLight, LDR_DARK_RAW, LDR_BRIGHT_RAW, 0, 100), 0, 100);

    state.pumpOn = state.soilPercent < PUMP_THRESHOLD;
    updatePump(state.pumpOn);

    updateDisplay(state);
    printSensorLog(state);
  }

  if (millis() - lastFirebasePush >= FIREBASE_INTERVAL_MS) {
    lastFirebasePush = millis();
    pushToFirebase(state);
  }

  delay(50);
}
