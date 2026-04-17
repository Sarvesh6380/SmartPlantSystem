#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ═══════════════════════════════════════════════════════════
//  CONFIGURATION
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "1234ABCD"
#define WIFI_PASSWORD "ABCD1234"
#define API_KEY       "AIzaSyBcfKljXgucdPWl6J-EXvwZM0snNCjBedw"
// FIX: SSL/ERRNO113: Corrected malformed URL protocol ('htttps://' to 'https://') which causes SSL parsing to fail.
#define DATABASE_URL  "https://smartplantcare-1f8f3-default-rtdb.firebaseio.com"

// ═══════════════════════════════════════════════════════════
//  PIN MAPPING (ESP32-S3)
// ═══════════════════════════════════════════════════════════
#define SOIL_PIN   4    // ADC1_CH3
#define DHTPIN     5    // DHT11 data
#define LDR_PIN    1    // ADC1_CH1 (NOT GPIO6 — that's USB/JTAG on S3)
#define RELAY_PIN  7    // Active-LOW relay output
#define OLED_SDA   8
#define OLED_SCL   9
#define DHTTYPE    DHT11

// ═══════════════════════════════════════════════════════════
//  CALIBRATION — update after checking Serial Monitor raw values
// ═══════════════════════════════════════════════════════════
#define SOIL_DRY    3000
#define SOIL_WET    1500
#define LDR_DARK     300
#define LDR_BRIGHT  3700

// ═══════════════════════════════════════════════════════════
//  TIMING
// ═══════════════════════════════════════════════════════════
#define OLED_SCREEN_INTERVAL  5000UL
#define OLED_REFRESH_INTERVAL  250UL

// ═══════════════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════════════
Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT              dht(DHTPIN, DHTTYPE);
FirebaseData     fbdo;
FirebaseData     fbdo_ctrl;
FirebaseAuth     auth;
FirebaseConfig   firebaseConfig;
bool             oledReady = false;

// ═══════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════
float temperature   = 0.0;
float humidity      = 0.0;
int   soilPercent   = 0;
int   lightPercent  = 0;
bool  pumpOn        = false;
bool  dhtOk         = false;
bool  manualOverride = false;
bool  manualPumpOn   = false;

unsigned int failedUploadCount = 0; // FIX: SSL/ERRNO113: Track consecutive upload failures for connection retry backoff

unsigned long lastSensorRead     = 0;
unsigned long lastFirebaseUpload = 0;
unsigned long lastControlCheck   = 0;
unsigned long lastOledSwitch     = 0;
unsigned long lastOledRefresh    = 0;
bool          showMoodScreen     = false;

enum PlantMood { MOOD_SMILE, MOOD_SAD, MOOD_SLEEPY, MOOD_CRYING, MOOD_HEART_EYES };

// ═══════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════
int stableRead(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogRead(pin); delay(2); }
  return (int)(sum / samples);
}

bool isNightTime() { return lightPercent <= 20; }

PlantMood currentMood() {
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool perfect = dhtOk && wifiOk && soilPercent >= 55 && lightPercent >= 35
              && lightPercent <= 85 && temperature >= 20.0 && temperature <= 32.0
              && humidity >= 40.0 && humidity <= 75.0 && !pumpOn;
  if (!dhtOk || !wifiOk)  return MOOD_SAD;
  if (soilPercent <= 15)  return MOOD_CRYING;
  if (isNightTime())      return MOOD_SLEEPY;
  if (perfect)            return MOOD_HEART_EYES;
  if (soilPercent < 30)   return MOOD_SAD;
  return MOOD_SMILE;
}

const char* statusMessage() {
  PlantMood mood = currentMood();
  if (mood == MOOD_CRYING)     return "Soil very dry";
  if (mood == MOOD_SLEEPY)     return "Night mode";
  if (mood == MOOD_HEART_EYES) return "Perfect care";
  if (!dhtOk)                  return "Check DHT";
  if (soilPercent < 30)        return "Need water";
  if (WiFi.status() != WL_CONNECTED) return "WiFi issue";
  return "All good";
}

const char* moodLabel(PlantMood mood) {
  switch (mood) {
    case MOOD_CRYING:     return "Crying :'(";
    case MOOD_SLEEPY:     return "Sleepy -_-";
    case MOOD_HEART_EYES: return "Loving <3";
    case MOOD_SAD:        return "Not happy :(";
    default:              return "Smiling :)";
  }
}

// ═══════════════════════════════════════════════════════════
//  OLED DRAW HELPERS
// ═══════════════════════════════════════════════════════════
void drawCenteredText(const char *text, int y) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (128 - (int)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void drawScreenBorder() { display.drawRect(0, 0, 128, 64, WHITE); }

void drawHeartEye(int x, int y, bool pulse) {
  int size = pulse ? 6 : 5;
  int topY = pulse ? y - 1 : y;
  display.fillCircle(x - 2, topY - 1, 2, WHITE);
  display.fillCircle(x + 2, topY - 1, 2, WHITE);
  display.fillTriangle(x - size, topY, x + size, topY, x, topY + size + 1, WHITE);
}

void drawTempIcon(int x, int y) {
  display.drawCircle(x+3,y+7,2,WHITE);
  display.drawLine(x+3,y+1,x+3,y+6,WHITE);
  display.drawLine(x+1,y+2,x+1,y+5,WHITE);
  display.drawLine(x+5,y+2,x+5,y+5,WHITE);
  display.drawPixel(x+3,y,WHITE);
}

void drawDropIcon(int x, int y) {
  display.drawTriangle(x+4,y,x+1,y+4,x+7,y+4,WHITE);
  display.drawCircle(x+4,y+5,3,WHITE);
}

void drawSoilIcon(int x, int y) {
  display.drawLine(x+4,y+2,x+4,y+7,WHITE);
  display.drawLine(x+4,y+4,x+1,y+2,WHITE);
  display.drawLine(x+4,y+4,x+7,y+2,WHITE);
  display.drawLine(x+1,y+8,x+7,y+8,WHITE);
  display.drawLine(x+1,y+9,x+7,y+9,WHITE);
  display.drawPixel(x+2,y+10,WHITE);
  display.drawPixel(x+5,y+10,WHITE);
}

void drawSunIcon(int x, int y) {
  display.drawCircle(x+4,y+4,2,WHITE);
  display.drawLine(x+4,y,x+4,y+1,WHITE);
  display.drawLine(x+4,y+7,x+4,y+8,WHITE);
  display.drawLine(x,y+4,x+1,y+4,WHITE);
  display.drawLine(x+7,y+4,x+8,y+4,WHITE);
  display.drawLine(x+1,y+1,x+2,y+2,WHITE);
  display.drawLine(x+6,y+6,x+7,y+7,WHITE);
  display.drawLine(x+1,y+7,x+2,y+6,WHITE);
  display.drawLine(x+6,y+2,x+7,y+1,WHITE);
}

void drawPumpIcon(int x, int y) {
  display.drawLine(x+1,y+2,x+6,y+2,WHITE);
  display.drawLine(x+6,y+2,x+6,y+5,WHITE);
  display.drawLine(x+3,y+5,x+8,y+5,WHITE);
  display.drawLine(x+3,y+5,x+3,y+8,WHITE);
  if (pumpOn) {
    display.drawLine(x+8,y+6,x+7,y+9,WHITE);
    display.drawPixel(x+7,y+10,WHITE);
  }
}

void drawWifiIcon(int x, int y) {
  display.drawCircle(x+4,y+7,1,WHITE);
  display.drawLine(x+2,y+5,x+6,y+5,WHITE);
  display.drawLine(x+1,y+3,x+7,y+3,WHITE);
  display.drawLine(x,y+1,x+8,y+1,WHITE);
}

// ═══════════════════════════════════════════════════════════
//  FACE ANIMATION — unchanged from your original
// ═══════════════════════════════════════════════════════════
void drawFace(PlantMood mood, uint8_t frame) {
  const int faceX = 64;
  const int faceY = 22;
  const int faceR = 18;
  bool blink = (frame == 0);
  bool pulse = (frame % 2 == 0);
  int smileY = (frame % 2 == 0) ? 11 : 12;
  int frownY = (frame % 2 == 0) ?  7 :  6;
  int tearOffset = frame % 3;

  display.drawCircle(faceX, faceY, faceR, WHITE);

  switch (mood) {
    case MOOD_HEART_EYES:
      drawHeartEye(faceX - 7, faceY - 5, pulse);
      drawHeartEye(faceX + 7, faceY - 5, pulse);
      display.drawLine(faceX-9, faceY+6,         faceX-4, faceY+smileY,   WHITE);
      display.drawLine(faceX-4, faceY+smileY,     faceX,   faceY+smileY+1, WHITE);
      display.drawLine(faceX,   faceY+smileY+1,   faceX+4, faceY+smileY,   WHITE);
      display.drawLine(faceX+4, faceY+smileY,     faceX+9, faceY+6,        WHITE);
      break;

    case MOOD_SLEEPY:
      display.drawLine(faceX-10, faceY-5, faceX-4, faceY-5, WHITE);
      display.drawLine(faceX+4,  faceY-5, faceX+10,faceY-5, WHITE);
      display.drawLine(faceX-8,  faceY+10,faceX+8, faceY+10,WHITE);
      if (pulse) { display.setCursor(faceX+13, faceY-12); display.print("z"); }
      else       { display.setCursor(faceX+10, faceY-14); display.print("Z"); }
      break;

    case MOOD_CRYING:
      if (blink) {
        display.drawLine(faceX-10,faceY-5,faceX-4,faceY-5,WHITE);
        display.drawLine(faceX+4, faceY-5,faceX+10,faceY-5,WHITE);
      } else {
        display.fillCircle(faceX-7,faceY-5,2,WHITE);
        display.fillCircle(faceX+7,faceY-5,2,WHITE);
      }
      display.drawLine(faceX-9,faceY+12,faceX-4,faceY+7,WHITE);
      display.drawLine(faceX-4,faceY+7, faceX,  faceY+frownY,WHITE);
      display.drawLine(faceX,  faceY+frownY,faceX+4,faceY+7,WHITE);
      display.drawLine(faceX+4,faceY+7, faceX+9,faceY+12,WHITE);
      display.drawLine(faceX-8,faceY+tearOffset,faceX-10,faceY+6+tearOffset,WHITE);
      display.drawLine(faceX+8,faceY+tearOffset,faceX+10,faceY+6+tearOffset,WHITE);
      break;

    case MOOD_SAD:
      if (blink) {
        display.drawLine(faceX-10,faceY-5,faceX-4,faceY-5,WHITE);
        display.drawLine(faceX+4, faceY-5,faceX+10,faceY-5,WHITE);
      } else {
        display.fillCircle(faceX-7,faceY-5,2,WHITE);
        display.fillCircle(faceX+7,faceY-5,2,WHITE);
      }
      display.drawLine(faceX-9,faceY+12,faceX-4,faceY+7,WHITE);
      display.drawLine(faceX-4,faceY+7, faceX,  faceY+frownY,WHITE);
      display.drawLine(faceX,  faceY+frownY,faceX+4,faceY+7,WHITE);
      display.drawLine(faceX+4,faceY+7, faceX+9,faceY+12,WHITE);
      break;

    default: // MOOD_SMILE
      if (blink) {
        display.drawLine(faceX-10,faceY-5,faceX-4,faceY-5,WHITE);
        display.drawLine(faceX+4, faceY-5,faceX+10,faceY-5,WHITE);
      } else {
        display.fillCircle(faceX-7,faceY-5,2,WHITE);
        display.fillCircle(faceX+7,faceY-5,2,WHITE);
      }
      display.drawLine(faceX-9,faceY+6,       faceX-4,faceY+smileY,  WHITE);
      display.drawLine(faceX-4,faceY+smileY,  faceX,  faceY+smileY+1,WHITE);
      display.drawLine(faceX,  faceY+smileY+1,faceX+4,faceY+smileY,  WHITE);
      display.drawLine(faceX+4,faceY+smileY,  faceX+9,faceY+6,       WHITE);
      break;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREENS
// ═══════════════════════════════════════════════════════════
void drawReadingScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  drawScreenBorder();

  drawTempIcon(4, 4);
  display.setCursor(16, 4);  display.printf("%.1f C", temperature);

  drawSoilIcon(70, 4);
  display.setCursor(82, 4);  display.printf("%2d%%", soilPercent);

  drawDropIcon(4, 16);
  display.setCursor(16, 16); display.printf("%3d%%", (int)round(humidity));

  drawSunIcon(70, 16);
  display.setCursor(82, 16); display.printf("%2d%%", lightPercent);

  display.drawFastHLine(4, 31, 120, WHITE);

  drawPumpIcon(22, 38);
  display.setCursor(36, 38);
  display.printf("%s [%s]", pumpOn ? "ON" : "OFF", manualOverride ? "MAN" : "AUTO");

  drawWifiIcon(22, 50);
  display.setCursor(36, 50);
  display.printf("%s", WiFi.status() == WL_CONNECTED ? "Connected" : "ERROR");

  display.display();
}

void drawMoodScreen() {
  PlantMood mood = currentMood();
  uint8_t frame = (millis() / 250) % 4;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  drawScreenBorder();
  drawFace(mood, frame);
  drawCenteredText(moodLabel(mood), 42);
  drawCenteredText(statusMessage(), 52);
  display.display();
}

void updateOLED() {
  if (!oledReady) return;
  if (millis() - lastOledSwitch >= OLED_SCREEN_INTERVAL) {
    showMoodScreen = !showMoodScreen;
    lastOledSwitch = millis();
  }
  showMoodScreen ? drawMoodScreen() : drawReadingScreen();
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000);
  Serial.println("\n═══ SMART PLANT CARE SYSTEM (ESP32-S3) ═══");

  // ── ADC ──────────────────────────────────────────────────
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // ── Pins ─────────────────────────────────────────────────
  pinMode(SOIL_PIN,  INPUT);
  pinMode(LDR_PIN,   INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // HIGH = relay OFF (active-LOW module)
  // ╔══════════════════════════════════════════════════════╗
  // ║  RELAY WIRING REMINDER                               ║
  // ║  Relay VCC → 5V  (NOT 3.3V — coil needs 5V)         ║
  // ║  Relay GND → GND                                     ║
  // ║  Relay IN  → GPIO7                                   ║
  // ║  If relay clicks but load doesn't switch:            ║
  // ║   → Check COM/NO/NC connections on the relay         ║
  // ║  If relay never clicks:                              ║
  // ║   → Try swapping HIGH/LOW in pumpOn logic below      ║
  // ╚══════════════════════════════════════════════════════╝

  // ── I2C + Sensors ────────────────────────────────────────
  Wire.begin(OLED_SDA, OLED_SCL);
  dht.begin();

  // ── OLED ─────────────────────────────────────────────────
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[WARN] OLED not found — check GPIO 8/9 wiring");
  } else {
    oledReady = true;
    display.clearDisplay();
    display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(10,18); display.println("Smart Plant Care");
    display.setCursor(10,34); display.println("Connecting WiFi...");
    display.display();
  }

  // ── WiFi ──────────────────────────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[WiFi] Connected — IP: " + WiFi.localIP().toString());

  // ── Firebase ──────────────────────────────────────────────
  firebaseConfig.api_key               = API_KEY;
  firebaseConfig.database_url          = DATABASE_URL;
  firebaseConfig.token_status_callback = tokenStatusCallback;
  if (Firebase.signUp(&firebaseConfig, &auth, "", "")) {
    Serial.println("[Firebase] Auth: OK");
  } else {
    Serial.printf("[Firebase] Auth failed: %s\n",
      firebaseConfig.signer.signupError.message.c_str());
  }
  Firebase.begin(&firebaseConfig, &auth);
  Firebase.reconnectWiFi(true);
  if (Firebase.ready()) {
    Firebase.RTDB.setBool  (&fbdo_ctrl, "/Controls/pump_override", false);
    Firebase.RTDB.setString(&fbdo_ctrl, "/Controls/pump_manual",   "OFF");
  }

  // ── Ready splash ──────────────────────────────────────────
  if (oledReady) {
    display.clearDisplay();
    display.setCursor(18,28); display.println("System Ready!");
    display.display(); delay(800);
    lastOledSwitch  = millis();
    lastOledRefresh = millis();
  }

  // ── LDR sanity test ───────────────────────────────────────
  Serial.println("\n[LDR Test] Cover/uncover LDR now:");
  for (int i = 0; i < 5; i++) {
    Serial.printf("  Sample %d: raw=%d\n", i+1, stableRead(LDR_PIN));
    delay(500);
  }
  Serial.println("[System] Entering main loop\n");
}

// ═══════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════
void loop() {

  // ── Firebase: read manual commands every 3 s ──────────────
  // FIX: SSL/ERRNO113: Ensure WiFi is connected before attempting Firebase operation to prevent TCP stack aborts.
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && millis() - lastControlCheck >= 3000) {
    lastControlCheck = millis();
    if (Firebase.RTDB.getBool(&fbdo_ctrl, "/Controls/pump_override")) {
      manualOverride = fbdo_ctrl.boolData();
    }
    if (manualOverride) {
      if (Firebase.RTDB.getString(&fbdo_ctrl, "/Controls/pump_manual")) {
        manualPumpOn = (fbdo_ctrl.stringData() == "ON");
      }
    }
  }

  // ── Sensors every 2 s ────────────────────────────────────
  if (millis() - lastSensorRead >= 2000) {
    lastSensorRead = millis();

    // DHT11
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
      dhtOk = false;
      Serial.println("[WARN] DHT11 read failed");
    } else {
      dhtOk = true; temperature = t; humidity = h;
    }

    // Soil
    int rawSoil = stableRead(SOIL_PIN);
    soilPercent = constrain(map(rawSoil, SOIL_DRY, SOIL_WET, 0, 100), 0, 100);

    // LDR
    int rawLight = stableRead(LDR_PIN);
    lightPercent = constrain(map(rawLight, LDR_DARK, LDR_BRIGHT, 0, 100), 0, 100);

    // ── RELAY LOGIC (fixed hysteresis — prevents rapid toggling) ──
    if (manualOverride) {
      pumpOn = manualPumpOn;
    } else {
      if      (soilPercent < 30) pumpOn = true;   // dry  → pump ON
      else if (soilPercent > 60) pumpOn = false;  // wet  → pump OFF
      // between 30–60: keep current state (hysteresis zone)
    }

    // Active-LOW relay: LOW = ON, HIGH = OFF
    // If your relay does NOT respond, swap LOW↔HIGH on the next line:
    digitalWrite(RELAY_PIN, pumpOn ? LOW : HIGH);

    Serial.printf(
      "[Sensor] T=%.1fC H=%.1f%% Soil=%d%%(raw:%d) Light=%d%%(raw:%d) Pump=%s [%s]\n",
      temperature, humidity, soilPercent, rawSoil,
      lightPercent, rawLight,
      pumpOn ? "ON" : "OFF", manualOverride ? "MANUAL" : "AUTO"
    );
  }

  // ── OLED refresh every 250 ms ─────────────────────────────
  if (oledReady && millis() - lastOledRefresh >= OLED_REFRESH_INTERVAL) {
    lastOledRefresh = millis();
    updateOLED();
  }

  // ── Firebase upload every 5 s ────────────────────────────
  // FIX: SSL/ERRNO113: Check WiFi status to prevent lingering TCP attempts when router connection is lost.
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && (millis() - lastFirebaseUpload > 5000 || lastFirebaseUpload == 0)) {
    lastFirebaseUpload = millis();
    FirebaseJson json;
    json.set("temp",     temperature);
    json.set("humidity", humidity);
    json.set("soil",     soilPercent);
    json.set("light",    lightPercent);
    json.set("pump",     pumpOn ? "ON" : "OFF");
    json.set("mode",     manualOverride ? "MANUAL" : "AUTO");
    json.set("uptime",   (int)(millis() / 1000));
    if (Firebase.RTDB.setJSON(&fbdo, "/PlantData", &json)) {
      Serial.println("[Firebase] Upload: OK");
      failedUploadCount = 0; // Reset counter on success
    } else {
      Serial.print("[Firebase] Upload FAILED: ");
      Serial.println(fbdo.errorReason());
      
      // FIX: SSL/ERRNO113: Implement connection retry mechanism. Reboot WiFi stack after 3 consecutive failures.
      failedUploadCount++;
      if (failedUploadCount >= 3) {
        Serial.println("[SSL/WIFI] Consecutive failures maxed out. Forcing WiFi reconnect...");
        WiFi.disconnect();
        WiFi.reconnect();
        failedUploadCount = 0;
      }
    }
  }

  delay(50);
}
