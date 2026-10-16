# CapriGuard 🐐

**CapriGuard** is an AI-powered smart goat health monitoring and sorting system built on the **ESP32-S3-EYE**. It automatically screens each goat as it passes through a gate, checking for diseases via Gemini AI vision analysis and measuring body temperature with a contactless IR sensor — then routes the goat to the correct farm pen by opening the appropriate servo-controlled gate.

---

## ✨ Features

| Feature | Description |
|---|---|
| 🏷️ **NFC Ear Tag ID** | Reads MIFARE Classic / NTAG213 NFC ear tags (PN532) to uniquely identify each goat |
| 🌡️ **IR Temperature Sensing** | Measures contactless body temperature via GY-614V3 (UART) to detect fever or hypothermia |
| 📸 **AI Health Screening** | Captures a photo and sends it to **Gemini 2.5 Flash** for visual disease detection |
| 🚪 **Servo Gate Control** | Bit-banged SG90 servo motors automatically open the correct farm gate based on diagnosis & location |
| 💡 **Traffic Light Status** | Red / Yellow / Green LED module reflects system state at a glance |
| 🖥️ **TFT Display** | ST7735 128×160 screen shows real-time tag UID, health status, diagnosis, and gate info |
| ☁️ **Cloud API Integration** | Pushes health status and reads goat home-farm location from a REST API |
| 📶 **Live Camera Stream** | Streams camera feed over HTTP on the local network |

---

## 🩺 Health Status Codes

| Code | Status | Condition |
|:---:|---|---|
| `0` | ✅ Healthy | Temperature 37.7–40 °C, no AI-detected disease |
| `1` | 🔴 Fever | Temperature > 40 °C |
| `2` | ⚠️ Disease | Gemini AI detects **Anemia** and/or **Orf (Contagious Ecthyma)** |
| `3` | 🥶 Hypothermia | Temperature < 37.7 °C |

> **Note:** Status `2` combined with an abnormal temperature (fever or hypothermia) triggers a **Caprine Pneumonia** suspected diagnosis.

---

## 🛠️ Hardware

| Component | Role |
|---|---|
| **ESP32-S3-EYE** | Main microcontroller with onboard camera |
| **PN532** (I2C) | NFC reader for MIFARE / NTAG213 ear tags |
| **GY-614V3** (UART) | Contactless infrared body temperature sensor |
| **ST7735 TFT** (Software SPI) | 128×160 status display |
| **SG90 Servo ×4** | Gate motors — 1 main gate + 3 farm gates |
| **Traffic Light Module** | Red / Yellow / Green LEDs for operator status |
| **Momentary Button** | Operator "close gates" button (GPIO 2) |
| **Toggle Switch** | System enable/disable + demo reset (GPIO 46) |

### Pin Map

<details>
<summary>Click to expand pin assignments</summary>

**Camera (ESP32-S3-EYE built-in)**
```
XCLK=15, SIOD=4, SIOC=5
Y2–Y9 = 11,9,8,10,12,18,17,16
VSYNC=6, HREF=7, PCLK=13
```

**TFT Display (Software SPI)**
```
CS=0, DC=47, SCLK=3, MOSI=41, RST=EN pin
```

**PN532 NFC (I2C)**
```
SDA=19, SCL=20
```

**GY-614V3 Temperature Sensor (UART)**
```
RX=40 (sensor TX → ESP32 RX)
```

**Servo Motors**
```
Main Gate=21, Farm 1=14, Farm 2=38, Farm 3=39
```

**Traffic Light**
```
RED=45, YELLOW=42, GREEN=48
```

**Buttons**
```
Close Button=2, System Toggle=46
```

</details>

---

## 🚀 Getting Started

### Prerequisites

- [Arduino IDE](https://www.arduino.cc/en/software) with **ESP32 board support** (Espressif ESP32 Arduino Core)
- The following Arduino libraries (install via Library Manager):
  - `Adafruit PN532`
  - `Adafruit GFX Library`
  - `Adafruit ST7735 and ST7789 Library`
  - `ArduinoJson`
  - `ESP32 Camera` (included in Espressif core)

### Configuration

1. **Copy the secrets template:**
   ```bash
   cp secrets.h.template secrets.h
   ```

2. **Fill in your credentials** in `secrets.h`:
   ```cpp
   #define WIFI_SSID       "your_wifi_ssid"
   #define WIFI_PASSWORD   "your_wifi_password"
   #define GEMINI_API_KEY  "your_gemini_api_key"
   ```
   > ⚠️ `secrets.h` is listed in `.gitignore` and will **never** be committed.

3. **Open `CapriGuard.ino`** in Arduino IDE, select the **ESP32S3 Dev Module** board, and flash.

---

## 🔄 System Operation Flow

```
Power ON
   │
   ▼
Toggle Switch = 0 → ┌─── HALTED (Red light) ───┐
                     │   Waiting for switch flip  │
Toggle Switch = 1 → └────────────────────────────┘
   │
   ▼
Database Reset (cloud API)
   │
   ▼
STANDBY — Scanning for NFC ear tag...
   │
   ▼  (tag detected)
10-second Countdown — "Hold goat still"
   │
   ▼  (flash lights → camera capture)
Temperature Reading (GY-614V3)
   │
   ▼
Gemini AI Analysis (photo → anemia / orf detection)
   │
   ▼
Health Status Determined → Written to Cloud API
   │
   ▼
Target Farm Gate Read from Cloud API
   │
   ▼
Gate Opens → TFT shows report
   │
   ▼  (operator presses close button)
Gates Close → Timestamp logged
   │
   ▼
New cycle begins
```

---

## 📡 Cloud API Endpoints

| Endpoint | Method | Purpose |
|---|---|---|
| `/apireset2/?place=ALL` | GET | Reset all goat statuses in the database |
| `/apiwrite2/?goat_id=&status=` | GET | Write health status for a goat |
| `/apiread2/?goat_id=` | GET | Read the target farm location for a goat |

Response format for read: `<UID>,FARM<N>,<gate>`

---

## 🤖 Gemini AI Prompt

The system prompts Gemini with a JPEG image and asks it to return structured JSON:

```json
{
  "anemia": false,
  "orf": true,
  "notes": "Crusty scab-like lesions visible around the mouth and nose."
}
```

Detected conditions are combined with the IR temperature reading to produce a final diagnosis.

---

## 📁 File Structure

```
CapriGuard/
├── CapriGuard.ino      # Main Arduino sketch
├── app_httpd.cpp       # Camera HTTP stream server
├── camera_index.h      # Camera web UI (binary)
├── index_html.h        # Web dashboard HTML
├── secrets.h           # WiFi & API credentials (git-ignored)
├── secrets.h.template  # Template for secrets.h
└── .gitignore
```

---

## ⚙️ Configuration Constants

Key timing and threshold values defined at the top of `CapriGuard.ino`:

| Constant | Default | Description |
|---|---|---|
| `TEMP_FEVER` | `40.0 °C` | Fever threshold |
| `TEMP_HYPOTHERMIA` | `37.7 °C` | Hypothermia threshold |
| `GATE_OPEN_MS` | `10 000 ms` | Maximum time a gate stays open |
| `POST_PASS_DELAY_MS` | `7 000 ms` | Delay before closing after button press |
| `NFC_TIMEOUT_MS` | `1 000 ms` | NFC scan timeout per attempt |
| `SERVO_OPEN` | `90°` | Servo angle for open position |
| `SERVO_CLOSED` | `0°` | Servo angle for closed position |

---

## 🔒 Security Notes

- TLS certificate verification is currently **disabled** (`setInsecure()`) for the Gemini and cloud API connections. For production use, consider pinning the certificate.
- `secrets.h` is git-ignored. Never commit credentials.

---

## 📄 License

See [LICENSE](CapriGuard/LICENSE) for details.
