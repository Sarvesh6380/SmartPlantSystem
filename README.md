# Smart Irrigation System

ESP32-S3 based smart plant care and irrigation project with sensor monitoring, OLED status display, Firebase Realtime Database integration, and relay-controlled water pump automation.

## Features

- Soil moisture monitoring (analog)
- Temperature and humidity sensing (DHT11)
- Light level sensing (LDR)
- OLED plant mood/status display (SSD1306, I2C)
- Automatic pump control with manual override capability
- Firebase Realtime Database data upload and control sync
- Wi-Fi based remote connectivity

## Requirements

Project hardware and software prerequisites are documented here:

- [REQUIREMENTS.md](REQUIREMENTS.md)

## Project Structure

- [src/main.cpp](src/main.cpp): Main PlatformIO firmware source
- [platformio.ini](platformio.ini): Board, framework, serial monitor, and library dependencies
- [smart_plant_care.ino](smart_plant_care.ino): Arduino sketch variant
- [index.html](index.html): Web/dashboard related file (if used)

## Build and Upload (PlatformIO)

```bash
pio run
pio run --target upload
pio device monitor
```

## Security Note

Do not commit real Wi-Fi credentials or Firebase secrets in source files.
Replace them with placeholders before pushing public changes.
