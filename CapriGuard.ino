/**
 * CapriGuard — Smart Goat Monitoring System
 * 
 * Features:
 *  - HC-SR04 ultrasonic distance trigger
 *  - GY-614V3 infrared temperature sensor (UART)
 *  - Camera capture → Gemini AI image analysis
 *  - MIFARE/NTAG213 NFC ear tag breed detection
 *  - SG90 servo gate control
 * 
 * Health Status Codes:
 *  0 = Healthy        (temp 37.7–40°C, no disease)
 *  1 = Fever          (temp > 40°C)
 *  2 = Disease        (AI detects Anemia or Orf)
 *  3 = Hypothermia    (temp < 37.7°C)
 *  Status 2 + abnormal temp → Caprine Pneumonia suspected
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "secrets.h"    // ← WiFi & API credentials (git-ignored)

// ===========================
// WiFi Credentials  (defined in secrets.h)
// ===========================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// ===========================
// Gemini API  (key defined in secrets.h)
// ===========================
// GEMINI_API_KEY is pulled from secrets.h
#define GEMINI_HOST     "generativelanguage.googleapis.com"
#define GEMINI_MODEL    "gemini-2.5-flash"

// ===========================
// Camera Pins (ESP32-S3-EYE)
// ===========================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// ===========================
// Close Gates Button
// ===========================
#define CLOSE_BUTTON_PIN    2   // Momentary button: GPIO 2 → GND (GPIO 1 now unused)

// ===========================
// ST7735 TFT Display Pins (Software SPI)
// ===========================
#define TFT_CS    0    // Safe strapping pin (pull-up at boot)
#define TFT_RST   -1   // Connect display RES pin to ESP32-S3 EN pin (resets with ESP32)
#define TFT_DC    47
#define TFT_SCLK  3    // Moved from 35 (PSRAM conflict)
#define TFT_MOSI  41   // Moved from 36 (PSRAM conflict)

// ===========================
// Traffic Light Module Pins
// ===========================
#define LIGHT_RED    45   // Moved to 45 (output capable; acts as boot pull-down to force VDD_SPI to 3.3V)
#define LIGHT_YELLOW 42
#define LIGHT_GREEN  48

// ===========================
// Cloud APIs
// ===========================
#define RESET_BUTTON_PIN 46 // External Toggle Switch (Wire to Pin 46 and GND) (input-only pin, safe at boot)

// ===========================
// GY-614V3 Temperature Sensor (UART)
// ===========================
#define TEMP_RX_PIN  40   // Wire TD (sensor TX) here
#define TEMP_TX_PIN  -1   // No transmit connection needed (frees up GPIO 41)

// ===========================
// PN532 NFC (I2C)
// ===========================
#define NFC_SDA     19
#define NFC_SCL     20

// ===========================
// Servo Motor Pins
// ===========================
#define SERVO_M_PIN  21
#define SERVO_1_PIN  14
#define SERVO_2_PIN  38
#define SERVO_3_PIN  39

// Track current angles to prevent sudden jumping
int angleM = 0;
int angle1 = 0;
int angle2 = 0;
int angle3 = 0;

// ===========================
// Settings
// ===========================
#define COOLDOWN_MS          5000
#define GATE_OPEN_MS         10000
#define NFC_TIMEOUT_MS       1000
#define POST_PASS_DELAY_MS   7000
#define SERVO_CLOSED         0
#define SERVO_OPEN           90

// Temperature thresholds (°C)
#define TEMP_FEVER        40.0
#define TEMP_HYPOTHERMIA  37.7

// ===========================
// MIFARE Classic Key A
// ===========================
uint8_t mifareKeyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===========================
// Goat UID → Default Farm Mapping
// Used as fallback when the cloud DB cannot return a location
// ===========================
struct GoatDefaultFarm {
  const char* uid;
  int         farmNumber;
};

const GoatDefaultFarm GOAT_DEFAULT_FARMS[] = {
  { "99:33:F3:05", 1 },  // FARM1
  { "F7:BA:29:07", 3 },  // FARM3
  { "97:23:EF:05", 2 },  // FARM2
};
const int GOAT_DEFAULT_FARMS_COUNT =
  sizeof(GOAT_DEFAULT_FARMS) / sizeof(GOAT_DEFAULT_FARMS[0]);

// Returns the default farm number for a given UID, or 0 if not found
int getDefaultFarm(const String& uid) {
  for (int i = 0; i < GOAT_DEFAULT_FARMS_COUNT; i++) {
    if (uid.equalsIgnoreCase(GOAT_DEFAULT_FARMS[i].uid)) {
      return GOAT_DEFAULT_FARMS[i].farmNumber;
    }
  }
  return 0; // Unknown UID
}

// ===========================
// Objects & State Constants
// ===========================
#define STATE_HALTED        0
#define STATE_RESETTING     1
#define STATE_STANDBY       2
#define STATE_TAG_DETECTED  3
#define STATE_COUNTDOWN     4
#define STATE_AI_ANALYSIS   5
#define STATE_REPORT        6
#define STATE_TRANSIT       7
#define STATE_COMPLETE      8

Adafruit_PN532  nfc(-1, -1);  // I2C mode
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

String latestDisease = "None";

void startCameraServer();
void updateDisplayAndLights(int state, String tagUid = "", String healthStr = "", String diseaseStr = "", String tempStr = "", String gateStr = "");
void setTrafficLight(bool r, bool y, bool g);

// ===========================
// Servo Bit-Bang Functions
// ===========================
void pulseServos() {
  int pulseM = map(angleM, 0, 180, 500, 2500);
  int pulse1 = map(angle1, 0, 180, 500, 2500);
  int pulse2 = map(angle2, 0, 180, 500, 2500);
  int pulse3 = map(angle3, 0, 180, 500, 2500);

  int maxPulse = max(max(pulseM, pulse1), max(pulse2, pulse3));

  digitalWrite(SERVO_M_PIN, HIGH);
  digitalWrite(SERVO_1_PIN, HIGH);
  digitalWrite(SERVO_2_PIN, HIGH);
  digitalWrite(SERVO_3_PIN, HIGH);

  unsigned long startUs = micros();
  
  while (micros() - startUs < maxPulse) {
    unsigned long elapsed = micros() - startUs;
    if (elapsed >= pulseM) digitalWrite(SERVO_M_PIN, LOW);
    if (elapsed >= pulse1) digitalWrite(SERVO_1_PIN, LOW);
    if (elapsed >= pulse2) digitalWrite(SERVO_2_PIN, LOW);
    if (elapsed >= pulse3) digitalWrite(SERVO_3_PIN, LOW);
  }

  digitalWrite(SERVO_M_PIN, LOW);
  digitalWrite(SERVO_1_PIN, LOW);
  digitalWrite(SERVO_2_PIN, LOW);
  digitalWrite(SERVO_3_PIN, LOW);

  unsigned long totalElapsed = micros() - startUs;
  if (totalElapsed < 20000) {
    delayMicroseconds(20000 - totalElapsed);
  }
}

void driveServos(int milliseconds) {
  int frames = milliseconds / 20; 
  for (int i = 0; i < frames; i++) {
    pulseServos();
  }
}

// ===========================
// Servo Setup (Bit-Banging)
// ===========================
void servoSetup() {
  pinMode(SERVO_M_PIN, OUTPUT);
  pinMode(SERVO_1_PIN, OUTPUT);
  pinMode(SERVO_2_PIN, OUTPUT);
  pinMode(SERVO_3_PIN, OUTPUT);

  // Initialize all to closed (0 degrees)
  angleM = SERVO_CLOSED;
  angle1 = SERVO_CLOSED;
  angle2 = SERVO_CLOSED;
  angle3 = SERVO_CLOSED;
  driveServos(1000); // Actively close them for 1 second
}

// ===========================
// Close Button: Check if operator has pressed to close gates
// ===========================
bool closeButtonPressed() {
  if (digitalRead(CLOSE_BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    return (digitalRead(CLOSE_BUTTON_PIN) == LOW);
  }
  return false;
}

// ===========================
// Latching Switch & Display Control
// ===========================
void setTrafficLight(bool r, bool y, bool g) {
  digitalWrite(LIGHT_RED, r ? HIGH : LOW);
  digitalWrite(LIGHT_YELLOW, y ? HIGH : LOW);
  digitalWrite(LIGHT_GREEN, g ? HIGH : LOW);
}

void updateDisplayAndLights(int state, String tagUid, String healthStr, String diseaseStr, String tempStr, String gateStr) {
  // 1. Update Traffic Lights based on state
  switch (state) {
    case STATE_HALTED:
      setTrafficLight(true, false, false); // RED
      break;
    case STATE_RESETTING:
    case STATE_TAG_DETECTED:
    case STATE_COUNTDOWN:
    case STATE_AI_ANALYSIS:
      setTrafficLight(false, true, false); // YELLOW
      break;
    case STATE_STANDBY:
    case STATE_REPORT:
    case STATE_TRANSIT:
      setTrafficLight(false, false, true); // GREEN
      break;
    case STATE_COMPLETE:
      setTrafficLight(false, true, false); // YELLOW
      break;
  }

  // 2. Update LCD Screen
  tft.fillScreen(ST7735_BLACK);
  
  // Title "CapriGuard" is always Green for all states
  tft.setTextColor(ST7735_GREEN);
  tft.setTextSize(2);
  tft.setCursor(5, 10);
  tft.println("CapriGuard");
  
  // Draw divider line
  tft.drawFastHLine(0, 30, 128, ST7735_WHITE);
  
  // Content text size
  tft.setTextSize(1);
  
  switch (state) {
    case STATE_HALTED:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 60);
      tft.println("Start by fliping");
      tft.setCursor(5, 75);
      tft.println("switch to 1");
      break;
      
    case STATE_RESETTING:
      tft.setTextColor(ST7735_YELLOW);
      tft.setCursor(5, 70);
      tft.println("Clearing database...");
      break;
      
    case STATE_STANDBY:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 70);
      tft.println("Scan ear tag");
      break;
      
    case STATE_TAG_DETECTED:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 70);
      tft.printf("Tag UID: %s\n", tagUid.c_str());
      break;
      
    case STATE_COUNTDOWN:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 45);
      tft.println("Hold goat still");
      break;
      
    case STATE_AI_ANALYSIS:
      tft.setTextColor(ST7735_YELLOW);
      tft.setCursor(5, 70);
      tft.println("AI analyzing...");
      break;
      
    case STATE_REPORT:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 40);
      tft.printf("Tag UID: %s\n", tagUid.c_str());
      tft.setCursor(5, 55);
      tft.printf("Health Status: %s\n", healthStr.c_str());
      tft.setCursor(5, 70);
      tft.printf("Disease: %s\n", diseaseStr.c_str());
      tft.setCursor(5, 85);
      tft.printf("Temperature: %s\n", tempStr.c_str());
      tft.setCursor(5, 105);
      tft.setTextColor(ST7735_YELLOW);
      tft.printf("Gate FARM%s\n", gateStr.c_str());
      tft.setCursor(5, 120);
      tft.println("is opening...");
      break;

    case STATE_TRANSIT:
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(5, 40);
      tft.printf("Tag UID: %s\n", tagUid.c_str());
      tft.setCursor(5, 55);
      tft.printf("Health Status: %s\n", healthStr.c_str());
      tft.setCursor(5, 70);
      tft.printf("Disease: %s\n", diseaseStr.c_str());
      tft.setCursor(5, 85);
      tft.printf("Temperature: %s\n", tempStr.c_str());
      tft.setCursor(5, 105);
      tft.setTextColor(ST7735_YELLOW);
      tft.printf("Gate FARM%s open\n", gateStr.c_str());
      tft.setCursor(5, 120);
      tft.setTextColor(ST7735_GREEN);
      tft.println("Press btn to close");
      break;
      
    case STATE_COMPLETE:
      tft.setTextColor(ST7735_YELLOW);
      tft.setCursor(5, 70);
      tft.println("Closing gates...");
      break;
  }
}

void closeAllGates() {
  Serial.println("[GATE] Closing all gates simultaneously for halt...");
  angleM = SERVO_CLOSED;
  angle1 = SERVO_CLOSED;
  angle2 = SERVO_CLOSED;
  angle3 = SERVO_CLOSED;
  driveServos(1500);
  Serial.println("[GATE] All gates closed.");
}

bool checkSystemSwitch() {
  if (digitalRead(RESET_BUTTON_PIN) == HIGH) {
    Serial.println();
    Serial.println("[SYSTEM] Switch flipped to 0 (Disabled). Halting system...");
    
    // Update display and lights to Halted (Red light solid)
    updateDisplayAndLights(STATE_HALTED);
    closeAllGates();
    
    while (digitalRead(RESET_BUTTON_PIN) == HIGH) {
      delay(100);
    }
    
    Serial.println("[SYSTEM] Switch flipped to 1. Initializing demo...");
    updateDisplayAndLights(STATE_RESETTING);
    apiReset();
    Serial.println("[SYSTEM] Database reset complete. System is ACTIVE.");
    return true;
  }
  return false;
}

// ===========================
// Cloud APIs
// ===========================
void apiReset() {
  Serial.println("[API] Sending Reset (place=ALL)...");
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, "https://api2.iotservic.com/apireset2/?place=ALL");
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.printf("[API] Reset Response: %s\n", http.getString().c_str());
  } else {
    Serial.printf("[API] Reset failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void apiWriteStatus(String goatId, int status) {
  Serial.printf("[API] Writing goat_id=%s status=%d...\n", goatId.c_str(), status);
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://api2.iotservic.com/apiwrite2/?goat_id=" + goatId + "&status=" + String(status);
  http.begin(secureClient, url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.printf("[API] Write Response: %s\n", http.getString().c_str());
  } else {
    Serial.printf("[API] Write failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

int apiReadLocation(String goatId) {
  Serial.printf("[API] Reading location for goat_id=%s...\n", goatId.c_str());
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://api2.iotservic.com/apiread2/?goat_id=" + goatId;
  http.begin(secureClient, url);
  int httpCode = http.GET();
  int targetGate = 0; // Will be resolved from DB or UID default
  
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.printf("[API] Read Response: %s\n", payload.c_str());
    // Payload format: "F7:BA:29:07,FARM2,3"
    if (payload.indexOf("FARM1") != -1 || payload.indexOf("FARM01") != -1) targetGate = 1;
    else if (payload.indexOf("FARM2") != -1 || payload.indexOf("FARM02") != -1) targetGate = 2;
    else if (payload.indexOf("FARM3") != -1 || payload.indexOf("FARM03") != -1) targetGate = 3;
    else {
      Serial.println("[API] Unknown farm location in response.");
    }
  } else {
    Serial.printf("[API] Read failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();

  // If DB returned no valid gate, fall back to the UID-based default farm
  if (targetGate == 0) {
    int defaultFarm = getDefaultFarm(goatId);
    if (defaultFarm != 0) {
      Serial.printf("[API] Using default farm for UID %s → FARM%d\n", goatId.c_str(), defaultFarm);
      targetGate = defaultFarm;
    } else {
      Serial.printf("[API] No default farm found for UID %s.\n", goatId.c_str());
    }
  }

  return targetGate;
}

// ===========================
// Capture image + Gemini AI analysis + health status
// ===========================
int captureAndAnalyze(float objTemp, float ambTemp) {
  Serial.println("[CAMERA] Capturing photo...");

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("[CAMERA] Capture failed!");
    return 0;
  }
  Serial.printf("[CAMERA] Photo: %d bytes\n", fb->len);

  // --- Base64 encode image (in PSRAM) ---
  size_t b64BufSize = ((fb->len + 2) / 3) * 4 + 1;
  unsigned char* b64Buf = (unsigned char*)ps_malloc(b64BufSize);
  if (!b64Buf) {
    Serial.println("[MEM] Base64 alloc failed!");
    esp_camera_fb_return(fb);
    return 0;
  }
  size_t b64Len = 0;
  mbedtls_base64_encode(b64Buf, b64BufSize, &b64Len, fb->buf, fb->len);
  b64Buf[b64Len] = '\0';
  esp_camera_fb_return(fb);
  Serial.printf("[GEMINI] Image encoded (%d bytes base64)\n", b64Len);

  // --- Build JSON request body in PSRAM ---
  // Prompt (no internal double-quotes — safe to embed in JSON string)
  const char* promptText =
    "You are a veterinary assistant. Analyze this goat photo for disease signs. "
    "Check for: (1) Anemia - look for pale or white gums, pale conjunctiva inner eyelids, visible weakness or lethargy. "
    "(2) Contagious Ecthyma also known as Orf - look for crusty scab-like lesions on lips, around mouth, nose, or face. "
    "Respond with JSON only containing three fields: "
    "anemia as a boolean true or false, "
    "orf as a boolean true or false, "
    "notes as a short string describing what you observe in the goat.";

  const char* bodyPrefix =
    "{\"contents\":[{\"parts\":["
    "{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  const char* bodyMiddle =
    "\"}},{\"text\":\"";
  const char* bodySuffix =
    "\"}]}],"
    "\"generationConfig\":{\"temperature\":0,\"responseMimeType\":\"application/json\"}}";

  size_t totalLen = strlen(bodyPrefix) + b64Len
                  + strlen(bodyMiddle) + strlen(promptText)
                  + strlen(bodySuffix);

  char* body = (char*)ps_malloc(totalLen + 1);
  if (!body) {
    Serial.println("[MEM] Body alloc failed!");
    free(b64Buf);
    return 0;
  }

  // Assemble body
  strcpy(body, bodyPrefix);
  memcpy(body + strlen(bodyPrefix), b64Buf, b64Len);
  free(b64Buf);
  size_t pos = strlen(bodyPrefix) + b64Len;
  strcpy(body + pos, bodyMiddle);    pos += strlen(bodyMiddle);
  strcpy(body + pos, promptText);    pos += strlen(promptText);
  strcpy(body + pos, bodySuffix);

  // --- Send to Gemini API ---
  Serial.println("[GEMINI] Sending to Gemini AI...");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();  // Skip TLS cert verification

  HTTPClient http;
  String url = "https://";
  url += GEMINI_HOST;
  url += "/v1beta/models/";
  url += GEMINI_MODEL;
  url += ":generateContent?key=";
  url += GEMINI_API_KEY;

  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(25000);  // 25 second timeout

  int httpCode = http.POST((uint8_t*)body, totalLen);
  free(body);

  // --- Parse response ---
  bool   anemia = false;
  bool   orf    = false;
  String notes  = "Analysis unavailable";

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("[GEMINI] Response received.");

    // Parse Gemini envelope
    DynamicJsonDocument envelope(8192);
    DeserializationError err = deserializeJson(envelope, response);

    if (!err) {
      const char* innerText =
        envelope["candidates"][0]["content"]["parts"][0]["text"];

      if (innerText) {
        // Parse inner JSON (the model's JSON response)
        DynamicJsonDocument inner(512);
        DeserializationError err2 = deserializeJson(inner, String(innerText));

        if (!err2) {
          anemia = inner["anemia"] | false;
          orf    = inner["orf"]    | false;
          notes  = inner["notes"]  | String("No details provided");
        } else {
          Serial.println("[GEMINI] Inner JSON parse error. Raw: " + String(innerText));
        }
      }
    } else {
      Serial.println("[GEMINI] Envelope JSON parse error.");
    }
  } else {
    Serial.printf("[GEMINI] HTTP error: %d\n", httpCode);
    if (httpCode > 0) Serial.println(http.getString());
  }
  http.end();

  // --- Determine temperature status ---
  int tempStatus;
  String tempLabel;
  if (objTemp > TEMP_FEVER) {
    tempStatus = 1;
    tempLabel  = "Fever";
  } else if (objTemp < TEMP_HYPOTHERMIA) {
    tempStatus = 3;
    tempLabel  = "Hypothermia";
  } else {
    tempStatus = 0;
    tempLabel  = "Normal";
  }

  // --- Determine overall health status ---
  int    healthStatus;
  String diagnosis;

  bool diseaseDetected = anemia || orf;

  if (diseaseDetected) {
    healthStatus = 2;
    if (anemia && orf)   diagnosis = "Anemia + Contagious Ecthyma (Orf)";
    else if (anemia)     diagnosis = "Anemia";
    else                 diagnosis = "Contagious Ecthyma (Orf)";

    if (tempStatus != 0) {
      // Disease + abnormal temperature = Caprine Pneumonia
      diagnosis += " + " + tempLabel + " -> Caprine Pneumonia suspected!";
    }
  } else if (tempStatus == 1) {
    healthStatus = 1;
    diagnosis    = "Fever only — no visible disease detected";
  } else if (tempStatus == 3) {
    healthStatus = 3;
    diagnosis    = "Hypothermia — no visible disease detected";
  } else {
    healthStatus = 0;
    diagnosis    = "Healthy";
  }

  // Set global disease string for display
  if (diseaseDetected) {
    if (anemia && orf) latestDisease = "Anemia + Orf";
    else if (anemia)   latestDisease = "Anemia";
    else               latestDisease = "Orf";
  } else {
    latestDisease = "None";
  }

  // --- Print health report ---
  Serial.println();
  Serial.println("==============================================");
  Serial.println("        CAPRIGUARD HEALTH REPORT");
  Serial.println("==============================================");
  Serial.printf( "  Object Temp  : %.1f C  (%s)\n", objTemp, tempLabel.c_str());
  Serial.printf( "  Ambient Temp : %.1f C\n",        ambTemp);
  Serial.println("----------------------------------------------");
  Serial.printf( "  Anemia       : %s\n",  anemia ? "YES - detected" : "No");
  Serial.printf( "  Orf          : %s\n",  orf    ? "YES - detected" : "No");
  Serial.printf( "  AI Notes     : %s\n",  notes.c_str());
  Serial.println("----------------------------------------------");
  Serial.printf( "  Health Status: %d\n",  healthStatus);
  Serial.printf( "  Diagnosis    : %s\n",  diagnosis.c_str());
  Serial.println("==============================================");
  Serial.println();

  return healthStatus;
}

// ===========================
// NFC: Read UID from ear tag as goat_id
// ===========================
String scanGoatID(bool printPrompt) {
  if (printPrompt) {
    Serial.println("[NFC] Scanning for ear tag...");
  }

  uint8_t uid[7];
  uint8_t uidLength = 0;
  bool found = nfc.readPassiveTargetID(
    PN532_MIFARE_ISO14443A, uid, &uidLength, NFC_TIMEOUT_MS
  );

  if (!found) {
    return "";
  }

  String uidStr = "";
  for (int i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
    if (i < uidLength - 1) uidStr += ":";
  }
  uidStr.toUpperCase();

  Serial.println();
  Serial.println("[NFC] Tag UID detected: " + uidStr);
  return uidStr;
}

// ===========================
// Gates: Open
// ===========================
void openGates(int breed) {
  Serial.printf("[GATE] Opening main gate + gate %d simultaneously...\n", breed);
  // Reset ALL farm gates to closed first to prevent stale angles from previous cycles
  angle1 = SERVO_CLOSED;
  angle2 = SERVO_CLOSED;
  angle3 = SERVO_CLOSED;
  angleM = SERVO_OPEN;
  switch (breed) {
    case 1: angle1 = SERVO_OPEN; break;
    case 2: angle2 = SERVO_OPEN; break;
    case 3: angle3 = SERVO_OPEN; break;
  }
  driveServos(1000); // actively pulse to open
}

// ===========================
// Gates: Close
// ===========================
void closeGates(int breed) {
  Serial.printf("[GATE] Closing all gates (breed=%d)...\n", breed);
  // Close ALL gates to ensure nothing is left open from previous cycles
  angleM = SERVO_CLOSED;
  angle1 = SERVO_CLOSED;
  angle2 = SERVO_CLOSED;
  angle3 = SERVO_CLOSED;
  driveServos(1500); // Actively pulse to close firmly
  Serial.println("[GATE] All gates closed.");
}

// ===========================
// TIME: Get and print timestamp
// ===========================
void printTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[TIME] Warning: Could not obtain local time (syncing...)");
    return;
  }
  char timeStr[64];
  // Format: YYYY-MM-DD HH:MM:SS
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("[TIME] Gate closed at: %s (GMT+8)\n", timeStr);
}

// ===========================
// SETUP
// ===========================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Button
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Close Gates Button (momentary, active LOW)
  pinMode(CLOSE_BUTTON_PIN, INPUT_PULLUP);

  // Initialize Traffic Light Pins
  pinMode(LIGHT_RED, OUTPUT);
  pinMode(LIGHT_YELLOW, OUTPUT);
  pinMode(LIGHT_GREEN, OUTPUT);
  setTrafficLight(false, false, false);

  // Initialize ST7735 TFT Screen
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0); // Vertical layout
  tft.fillScreen(ST7735_BLACK);

  // I2C — shared by PN532 and MLX90614
  Wire.begin(NFC_SDA, NFC_SCL);

  // PN532 NFC
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.println("[NFC] PN532 not found! Check wiring.");
  } else {
    nfc.SAMConfig();
    Serial.println("[NFC] PN532 ready!");
  }

  // GY-614V3 Temperature Sensor (UART)
  Serial2.begin(9600, SERIAL_8N1, TEMP_RX_PIN, TEMP_TX_PIN);
  Serial.println("[TEMP] GY-614V3 UART ready!");

  Serial.println("[SETUP] Initializing camera...");

  // Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  // Use PSRAM for frame buffer if available, otherwise fall back to internal DRAM
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  config.jpeg_quality = 12;
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_QVGA;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.fb_count     = 1;
    Serial.println("[CAMERA] PSRAM found — using QVGA in PSRAM.");
  } else {
    config.frame_size   = FRAMESIZE_QQVGA;  // Smaller frame to fit in DRAM
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.fb_count     = 1;
    Serial.println("[CAMERA] No PSRAM — using QQVGA in DRAM (reduced quality).");
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[CAMERA] Init failed!");
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_hmirror(s, 1);
  s->set_vflip(s, 0);
  s->set_brightness(s, 1);
  s->set_saturation(s, 0);
  s->set_ae_level(s, -3);
  Serial.println("[CAMERA] Camera ready!");

  // Servo motors (after camera — configured to bypass Timer 0 to avoid conflict)
  Serial.println("[SETUP] Attaching servo motors...");
  servoSetup();
  delay(500);
  Serial.println("[SERVO] All gates closed and ready.");

  // WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("[WiFi] Connecting ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("[WiFi] Connected!");

  // Sync time via NTP (GMT+8: 8 hours * 3600 seconds = 28800)
  Serial.println("[TIME] Syncing time with NTP server...");
  configTime(28800, 0, "pool.ntp.org", "time.nist.gov");

  // Camera HTTP stream
  startCameraServer();

  Serial.println();
  Serial.println("==============================================");
  Serial.printf("  Camera: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.println("  Gemini AI analysis: ACTIVE");
  Serial.println("  STATUS: System Ready");
  Serial.println("==============================================");

  // If switch is 0 (HIGH), wait until flipped to 1 (LOW)
  if (digitalRead(RESET_BUTTON_PIN) == HIGH) {
    Serial.println("[SYSTEM] Switch is currently at 0 (Disabled). Flip to 1 to start...");
    updateDisplayAndLights(STATE_HALTED);
    while (digitalRead(RESET_BUTTON_PIN) == HIGH) {
      delay(100);
    }
  }

  // Switch is now at 1 (LOW) - perform initial database reset
  updateDisplayAndLights(STATE_RESETTING);
  apiReset();
  Serial.println("[SYSTEM] Database reset complete. Starting goat watcher loop!");
}

// ===========================
// LOOP
// ===========================
void loop() {
  if (checkSystemSwitch()) return;

  Serial.println();
  Serial.println("==============================================");
  Serial.println("  [CYCLE START] Standby: Scanning for ear tag...");
  Serial.println("==============================================");

  // Step 1: Scan ear tag (as long as no ear tag is scan, no further action is continue)
  String goat_id = "";
  unsigned long lastPrintTime = 0;
  
  // Set display to Standby status
  updateDisplayAndLights(STATE_STANDBY);
  
  while (goat_id == "") {
    if (checkSystemSwitch()) return;

    unsigned long now = millis();
    // Print "Scanning for ear tag..." prompt once every 5 seconds to prevent spam
    bool printPrompt = (lastPrintTime == 0) || (now - lastPrintTime >= 5000);
    if (printPrompt) {
      lastPrintTime = now;
    }
    
    goat_id = scanGoatID(printPrompt);
    if (goat_id == "") {
      delay(200); // brief pause before trying again
    }
  }

  // Tag scanned successfully - show tag detected layout
  updateDisplayAndLights(STATE_TAG_DETECTED, goat_id);

  // Standby pacing delay: let user register the scan
  delay(1500);

  // Step 2: 10 second preparation
  Serial.println();
  Serial.println("[PREP] Entering 10-second preparation phase...");
  
  // Draw basic countdown screen (hold still text)
  updateDisplayAndLights(STATE_COUNTDOWN);
  
  for (int i = 10; i > 0; i--) {
    if (checkSystemSwitch()) return;
    Serial.printf("[PREP] Ready in %d seconds...\n", i);
    
    // Clear countdown area below title/hold text
    tft.fillRect(20, 70, 88, 40, ST7735_BLACK);
    
    // 10-4 is White, 3-1 is Red
    if (i >= 4) {
      tft.setTextColor(ST7735_WHITE);
    } else {
      tft.setTextColor(ST7735_RED);
    }
    tft.setTextSize(4);
    if (i >= 10) {
      tft.setCursor(40, 75);
    } else {
      tft.setCursor(52, 75);
    }
    tft.print(i);
    
    delay(1000);
  }
  Serial.println("[PREP] Preparation complete.");
  
  // Flash Traffic Lights (Camera flash simulation: all 3 ON for 1s, then OFF for 1s)
  Serial.println("[CAMERA] Flash light simulation!");
  setTrafficLight(true, true, true);
  delay(1000);
  setTrafficLight(false, false, false);
  delay(1000);
  
  // Restore Yellow light during processing
  setTrafficLight(false, true, false);

  // Step 3: Scan temperature
  Serial.println();
  Serial.println("[TEMP] Scanning temperature...");
  float objTemp = 0.0;
  float ambTemp = 0.0;
  Serial.println("[TEMP] Reading from GY-614V3...");
  
  // Flush stale bytes and wait for a fresh 12-byte packet (header 0xA4)
  unsigned long tempTimeout = millis() + 3000; // 3 second timeout
  while (millis() < tempTimeout) {
    if (Serial2.available() >= 12) {
      if (Serial2.read() == 0xA4) {
        uint8_t buf[11];
        Serial2.readBytes(buf, 11);
        
        // Calculate checksum: sum of first 11 bytes (0xA4 + buf[0..9])
        uint16_t sum = 0xA4;
        for (int i = 0; i < 10; i++) {
          sum += buf[i];
        }
        uint8_t calculatedChecksum = sum & 0xFF;
        uint8_t receivedChecksum = buf[10];
        
        if (calculatedChecksum == receivedChecksum) {
          uint16_t rawObj = ((uint16_t)buf[4] << 8) | buf[5]; // Byte 5,6: Target Temp (TO)
          uint16_t rawAmb = ((uint16_t)buf[6] << 8) | buf[7]; // Byte 7,8: Ambient Temp (TA)
          objTemp = rawObj / 100.0;
          ambTemp = rawAmb / 100.0;
          break;
        }
      }
    }
    delay(10);
  }

  if (objTemp == 0.0) {
    int availableBytes = Serial2.available();
    Serial.printf("[TEMP] Warning: No valid packet received. Available bytes on Serial2: %d\n", availableBytes);
    if (availableBytes > 0) {
      Serial.print("[TEMP] Raw bytes in buffer (Hex): ");
      while (Serial2.available() > 0) {
        Serial.printf("0x%02X ", Serial2.read());
      }
      Serial.println();
    }
  }
  Serial.printf("[TEMP] Object: %.1f C  |  Ambient: %.1f C\n", objTemp, ambTemp);
  delay(1500); // pause so user can read temperature on Serial before camera capture

  // Step 4: Take photo of goat (and run Gemini AI analysis)
  updateDisplayAndLights(STATE_AI_ANALYSIS);
  int healthStatus = captureAndAnalyze(objTemp, ambTemp);
  delay(2000); // pause to read the Gemini report and results

  // Step 4.5: Cloud APIs
  apiWriteStatus(goat_id, healthStatus);
  int targetGate = apiReadLocation(goat_id);

  if (targetGate == 0) {
    Serial.println("[SYSTEM] ERROR: No valid target gate from DB or UID lookup! Defaulting to Gate 1.");
    targetGate = 1;
  }

  // Update display to State 6 (Report & Open Gates)
  updateDisplayAndLights(STATE_REPORT, goat_id, String(healthStatus), latestDisease, String(objTemp, 1) + " C", String(targetGate));

  // Step 5: Open respected gates based on location
  openGates(targetGate);

  // Step 6: Wait for operator to press close button after goat has passed
  Serial.println();
  Serial.println("[BUTTON] Gates open. Waiting for operator to press CLOSE button...");

  // Show State 7 (Goat Transit) — display prompts operator to press button
  updateDisplayAndLights(STATE_TRANSIT, goat_id, String(healthStatus), latestDisease, String(objTemp, 1) + " C", String(targetGate));

  bool passed = false;
  while (!passed) {
    if (checkSystemSwitch()) return;
    if (closeButtonPressed()) {
      Serial.println();
      Serial.println("[BUTTON] Close button pressed! Goat confirmed to have passed. Closing gates...");
      passed = true;
    }
    driveServos(20); // Actively hold gates open (one 20ms servo frame), then re-check button
  }

  // Step 7: Close the gate after the goat pass through
  updateDisplayAndLights(STATE_COMPLETE);
  Serial.printf("[GATE] Delaying %d seconds before closing gates...\n", POST_PASS_DELAY_MS / 1000);
  driveServos(POST_PASS_DELAY_MS);
  closeGates(targetGate);
  printTimestamp();

  // Step 8: Start a new cycle after the gate close
  Serial.println();
  Serial.println("[CYCLE END] Gates closed. Cycle completed successfully!");
  delay(3000); // 3-second cooldown before starting a new ear tag scanning cycle
}
