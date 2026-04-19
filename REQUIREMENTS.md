# Smart Irrigation System - Project Requirements

This file lists all hardware and software requirements needed to build, upload, and run this project.

## 1. Hardware Requirements

### Core Controller
- ESP32-S3-DevKitC-1 development board
- USB data cable (USB-C or board-compatible)

### Sensors and Actuators
- DHT11 temperature and humidity sensor
- Soil moisture sensor (analog output)
- LDR (light-dependent resistor) module or LDR with resistor divider
- 1-channel relay module (3.3V logic compatible preferred)
- Mini DC water pump (as per your irrigation setup)

### Display and Communication
- OLED display, SSD1306, 128x64, I2C interface
- Wi-Fi network with internet access (for Firebase communication)

### Power and Wiring
- External power source for pump (recommended, do not power pump directly from ESP32 pin)
- Breadboard
- Jumper wires (male-male / male-female as needed)
- Optional: terminal block and flyback protection depending on pump/relay module

## 2. Software Requirements

### Development Tools
- Visual Studio Code (latest stable)
- PlatformIO IDE extension for VS Code
- Git (for version control and GitHub push)

### Platform and Framework
- PlatformIO platform: `espressif32`
- Board target: `esp32-s3-devkitc-1`
- Framework: `arduino`

### Required Libraries (PlatformIO)
These are already declared in [platformio.ini](platformio.ini):
- `mobizt/Firebase Arduino Client Library for ESP8266 and ESP32 @ ^4.4.17`
- `bblanchon/ArduinoJson @ ^6.21.3`
- `adafruit/Adafruit SSD1306 @ ^2.5.7`
- `adafruit/Adafruit GFX Library @ ^1.11.5`
- `adafruit/DHT sensor library @ ^1.4.4`
- `adafruit/Adafruit Unified Sensor @ ^1.1.9`

### Runtime Services
- Firebase project with:
  - Realtime Database enabled
  - API key
  - Database URL
- Valid Wi-Fi SSID and password

## 3. Firmware Configuration Requirements

Before upload, update sensitive credentials in [src/main.cpp](src/main.cpp):
- `WIFI_SSID`
- `WIFI_PASSWORD`
- `API_KEY`
- `DATABASE_URL`

Also verify pin mappings match your wiring in [src/main.cpp](src/main.cpp):
- `SOIL_PIN`
- `DHTPIN`
- `LDR_PIN`
- `RELAY_PIN`
- `OLED_SDA`
- `OLED_SCL`

## 4. Build and Upload Requirements

From project root:

```bash
pio run
pio run --target upload
pio device monitor
```

Monitor speed is configured as `115200` in [platformio.ini](platformio.ini).

## 5. Recommended GitHub Hygiene

Before pushing to GitHub:
- Do not commit real Wi-Fi credentials or Firebase secrets.
- Replace sensitive values with placeholders in source files.
- Keep `.gitignore` updated for build artifacts and local secrets.
