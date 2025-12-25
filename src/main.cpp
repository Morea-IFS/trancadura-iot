// Arduino Libraries
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>  
#include <Wire.h>
#include <stdio.h>

// Server Libraries
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>

// JSON Library 
#include <ArduinoJson.h>

// RFID Library
#include <MFRC522.h>

// OTA and Telnet
#include <ArduinoOTA.h>
#include <TelnetStream.h>

// OLED Display
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Bitmaps.h" 

// Keypad
#include <Keypad.h>

// ================= HARDWARE CONFIGURATIONS =================
#define RST_PIN  25
#define SS_PIN   5
#define relayPin 27
#define buttonPin 15
#define buzzerPin 2

// ================= NETWORK AND SERVER CONFIGURATIONS =================
String serverBaseUrl = "https://trancadura.morea-ifs.org";
String baseUrl = serverBaseUrl + "/api";

const char *SSID = "";
const char *PASSWORD = "";

const char* OTAhostname = "";
const char* OTA_PASSWORD = "";


String deviceIdUuid = ""; 
String apiToken = "";
String deviceMacAddress = ""; 

// Global Objects
MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClientSecure client;      
HTTPClient http;
AsyncWebServer server(80);

// Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Keypad Config
const byte ROWS = 4;
const byte COLS = 3; 
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {32, 26, 13, 33};
byte colPins[COLS] = {14, 4, 12};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// State Variables
String passwordInput = "";
const int passwordLength = 6;
unsigned long lastKeypadPress = 0;
const unsigned long keypadTimeout = 5000; 
unsigned long buttonLastPressed = 0;
const unsigned long debounceDelay = 200;

bool waitingForCard = false;
unsigned long cardWaitStart = 0;
unsigned long cardWaitDuration = 15000;
int pendingUserId = 0; 

// Screen States
enum ScreenState { DEFAULT_SCREEN, WIFI_ERROR, WIFI_CONNECTING, WIFI_CONNECTED, SERVER_CONNECTING, SERVER_CONNECTED, SERVER_OFFLINE, NEW_CARD, SUCCESS, SUCCESS_ADD, MASTER_SUCCESS, DENIED, ERROR, PASSWORD_SCREEN };
ScreenState currentScreen = DEFAULT_SCREEN;

// Master Cards (Offline Backup)
const char* masterCards[] = { "C3786CA5" }; 
const int masterCardsCount = sizeof(masterCards) / sizeof(masterCards[0]);

// ================= UI HELPER FUNCTIONS =================

void drawLayout(const char* title, const unsigned char* bitmap, uint8_t bmp_width, uint8_t bmp_height, ScreenState state) {
  if (currentScreen == state && state != DEFAULT_SCREEN && state != PASSWORD_SCREEN) return;
  display.clearDisplay();
  display.drawLine(0, 17, SCREEN_WIDTH, 17, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 5);
  display.print(title);
  if (bitmap != NULL) {
    int16_t bmp_x = (SCREEN_WIDTH - bmp_width) / 2;
    display.drawBitmap(bmp_x, 19, bitmap, bmp_width, bmp_height, SSD1306_BLACK, SSD1306_WHITE);
  }
  display.display();
  currentScreen = state;
}

void showDefault() { drawLayout("Aproxime o Cartao", logoMoreaIcon, 44, 44, DEFAULT_SCREEN); }
void showWifiConnecting() { drawLayout("Conectando WiFi...", wifiIcon, 44, 44, WIFI_CONNECTING); }
void showWifiConnected() { drawLayout("WiFi Conectado!", wifiIcon, 44, 44, WIFI_CONNECTED); }
void showWifiError() { drawLayout("Erro no WiFi", wifiIcon, 44, 44, WIFI_ERROR); }
void showServerConnecting() { drawLayout("Buscando Servidor", serverIcon, 44, 44, SERVER_CONNECTING); }
void showServerConnected() { drawLayout("Sistema Online", serverIcon, 44, 44, SERVER_CONNECTED); }
void showServerOffline() { drawLayout("Servidor Offline", noConnectionIcon, 100, 44, SERVER_OFFLINE); }
void showSuccessAdd() { drawLayout("Cartao Vinculado!", lockOpenIcon, 44, 44, SUCCESS_ADD); }
void showMasterSuccess() { drawLayout("Acesso Mestre", lockOpenIcon, 44, 44, MASTER_SUCCESS); }
void showDenied() { drawLayout("Acesso Negado", lockClosedIcon, 44, 44, DENIED); }
void showError() { drawLayout("Erro no Sistema", errorIcon, 44, 44, ERROR); }
void showNewCard() { drawLayout("Cadastrar Cartao", cardIcon, 44, 44, NEW_CARD); }
void showSuccess(const String& name) { 
  String msg = name.substring(0, 15); 
  drawLayout(msg.c_str(), lockOpenIcon, 44, 44, SUCCESS);  
}

void showPasswordScreen() {
  display.clearDisplay();
  display.drawLine(0, 17, SCREEN_WIDTH, 17, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  const char* title = "Digite a Senha";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 5);
  display.print(title);
  display.setTextSize(2); 
  String passDisplay = "";
  for(int i = 0; i < passwordLength; i++) { passDisplay += (i < passwordInput.length()) ? "*" : "_"; }
  display.getTextBounds(passDisplay, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 35);
  display.print(passDisplay);
  display.display();
  currentScreen = PASSWORD_SCREEN;
}

// Sounds
void beepSuccess() { tone(buzzerPin, 2500, 100); delay(150); tone(buzzerPin, 2500, 100); }
void beepError() { tone(buzzerPin, 200, 500); }
void beepDenied() { tone(buzzerPin, 500, 200); delay(250); tone(buzzerPin, 500, 200); }
void beepClick() { tone(buzzerPin, 2000, 50); }

void printLog(const String& message) {
  Serial.println(message);
  TelnetStream.println(message);
}

// ================= NETWORK AND API LOGIC =================

// Forward declaration
void sendDeviceIp();

void identifyDevice() {
  if (WiFi.status() != WL_CONNECTED) return;

  showServerConnecting();
  String url = baseUrl + "/devices/identify";
  deviceMacAddress = WiFi.macAddress(); // Store globally

  // JSON Payload
  JsonDocument doc;
  doc["macAddress"] = deviceMacAddress;
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  client.setInsecure(); // Skip certificate verification
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  printLog("Identifying device...");
  int httpCode = http.POST(jsonPayload);

  if (httpCode == 200 || httpCode == 201) {
    String response = http.getString();
    JsonDocument resDoc;
    deserializeJson(resDoc, response);
    
    deviceIdUuid = resDoc["id"].as<String>();
    apiToken = resDoc["api_token"].as<String>();
    
    printLog("ID: " + deviceIdUuid);
    showServerConnected();
    
    sendDeviceIp();
    
    delay(1000);
    showDefault();
  } else {
    printLog("Error ID: " + String(httpCode));
    showServerOffline();
    delay(2000);
  }
  http.end();
}

void sendDeviceIp() {
  if (deviceIdUuid == "" || apiToken == "") return;

  String url = baseUrl + "/devices/ip";
  String localIp = WiFi.localIP().toString();

  JsonDocument doc;
  doc["deviceId"] = deviceIdUuid;
  doc["deviceIp"] = localIp;
  doc["apiToken"] = apiToken;

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  printLog("Sending IP: " + localIp);
  int httpCode = http.POST(jsonPayload);
  
  if (httpCode != 200 && httpCode != 201) {
    printLog("Failed to update IP: " + http.getString());
  } else {
    printLog("IP Updated successfully.");
  }
  http.end();
}

void openDoor() {
  digitalWrite(relayPin, HIGH);
  beepSuccess();
  delay(2000); 
  digitalWrite(relayPin, LOW);
  showDefault();
}

// Generic function to validate access (Card or Password)
void validateAccess(String type, String value) {
  if (WiFi.status() != WL_CONNECTED) {
    // Offline Backup: Only master cards
    if (type == "card") {
        for (int i=0; i < masterCardsCount; i++) {
            if (value == masterCards[i]) {
                showMasterSuccess(); openDoor(); return;
            }
        }
    }
    showWifiError(); delay(2000); showDefault(); return;
  }

  // If no Token, try to identify
  if (apiToken == "") {
    identifyDevice();
    if (apiToken == "") return; 
  }

  String url;
  JsonDocument doc;

  // Build request based on type (Different routes in new API)
  if (type == "card") {
    // Approximation route
    url = baseUrl + "/approximations/auth";
    doc["hexid"] = value;
    doc["macaddress"] = deviceMacAddress;
  } else {
    // PIN route
    url = baseUrl + "/devices/auth/pin";
    doc["pin"] = value;
    doc["macAddress"] = deviceMacAddress;
  }

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  printLog("Validating " + type + "...");
  int httpCode = http.POST(jsonPayload);
  String response = http.getString();
  http.end();

  printLog("Resp: " + String(httpCode) + " -> " + response);

  // Response Parsing Logic (String: "Authorized?first_name=X" or "Unauthorized")
  if (httpCode == 200 || httpCode == 201) {
    if (response.startsWith("Authorized")) {
      String userName = "Acesso";
      int nameIndex = response.indexOf("first_name=");
      if (nameIndex != -1) {
        userName = response.substring(nameIndex + 11); // 11 is "first_name=" length
      }
      showSuccess(userName);
      openDoor();
    } else {
      showDenied();
      beepDenied();
      delay(2000);
      showDefault();
    }
  } else {
    showDenied();
    beepDenied();
    delay(2000);
    showDefault();
  }
}

// Link new card (Updated route)
void sendNewCard(String hexid) {
  String url = baseUrl + "/approximations/newcard"; 
  
  JsonDocument doc;
  doc["hexid"] = hexid;
  doc["userId"] = pendingUserId;
  doc["deviceId"] = 1; // Technical adjustment
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(jsonPayload);
  http.end();

  if (httpCode == 200 || httpCode == 201) {
    showSuccessAdd();
    beepSuccess();
  } else {
    showError();
    beepDenied();
  }
  delay(2000);
  showDefault();
}

// ================= SETUP AND LOOP =================

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  display.clearDisplay();
  
  // Connect WiFi
  showWifiConnecting();
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500); retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    showWifiConnected();
    TelnetStream.begin();
    ArduinoOTA.setHostname(OTAhostname);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    
    identifyDevice();
    
    // ================= CORS FIX HERE =================
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");

    // Configure Local WebServer Routes
    
    // Route: /open?apiToken=xyz (Remote Opening)
    server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (request->hasParam("apiToken") && request->getParam("apiToken")->value() == apiToken) {
        request->send(200, "text/plain", "OK");
        showSuccess("Remoto");
        openDoor();
      } else {
        request->send(401, "text/plain", "Unauthorized");
      }
    });

    // Route: /hexid (Start card registration)
    server.on("/hexid", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (request->hasParam("apiToken") && request->getParam("apiToken")->value() == apiToken) {
        if (request->hasParam("userId")) {
          pendingUserId = request->getParam("userId")->value().toInt();
          waitingForCard = true;
          cardWaitStart = millis();
          showNewCard();

          AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Aguardando Cartao...");
          response->addHeader("Access-Control-Allow-Origin", "*");
          request->send(response);
          
        } else {
           request->send(400, "text/plain", "Missing userId");
        }
      } else {
        request->send(401, "text/plain", "Unauthorized");
      }
    });

    // OPTIONS handling for Preflight requests
    server.onNotFound([](AsyncWebServerRequest *request) {
      if (request->method() == HTTP_OPTIONS) {
        request->send(200);
      } else {
        request->send(404);
      }
    });

    server.begin();
  } else {
    showWifiError();
  }
}

void loop() {
  ArduinoOTA.handle();

  // 1. Check Physical Button
  if (digitalRead(buttonPin) == LOW && millis() - buttonLastPressed > debounceDelay) {
    buttonLastPressed = millis();
    showSuccess("Botao");
    openDoor();
  }

  // 2. Check Keypad (Password)
  char key = keypad.getKey();
  if (key) {
    beepClick();
    lastKeypadPress = millis();
    
    if (isdigit(key)) {
      if (passwordInput.length() < passwordLength) {
        passwordInput += key;
        showPasswordScreen();
      }
    } else if (key == '*') { // Clear
      passwordInput = "";
      showDefault();
    } else if (key == '#') { // Send
      if (passwordInput.length() > 0) {
        validateAccess("pin", passwordInput);
        passwordInput = "";
      }
    }
  }

  // Keypad Timeout
  if (passwordInput.length() > 0 && (millis() - lastKeypadPress > keypadTimeout)) {
    passwordInput = "";
    showDefault();
  }

  // 3. Check RFID
  if (waitingForCard) {
    // REGISTRATION MODE
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      String hexid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        hexid += String(mfrc522.uid.uidByte[i], HEX);
      }
      hexid.toUpperCase();
      sendNewCard(hexid);
      waitingForCard = false;
      mfrc522.PICC_HaltA();
    }
    if (millis() - cardWaitStart > cardWaitDuration) {
      waitingForCard = false;
      showDefault();
    }
  } else if (passwordInput.length() == 0) {
    // NORMAL READING MODE
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      String hexid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        hexid += String(mfrc522.uid.uidByte[i], HEX);
      }
      hexid.toUpperCase();
      validateAccess("card", hexid);
      
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  }
}