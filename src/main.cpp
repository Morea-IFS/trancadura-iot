// Arduino Librarie
#include <Arduino.h>

// Wifi Librarie
#include <WiFi.h>

// HTTPS Request Libraries
#include <HTTPClient.h>
#include <WiFiClientSecure.h>   
#include <Wire.h>
#include <stdio.h>

// HTTPS Server Libraries
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>

// Json Librarie
#include <ArduinoJson.h>

// RFID Librarie
#include <MFRC522.h>

// OTA and Telnet Libraries
#include <ArduinoOTA.h>
#include <TelnetStream.h>

// OLED Display Libraries
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Bitmaps.h"

#define RST_PIN  25
#define SS_PIN   5
#define relayPin 27
#define buttonPin 15
#define buzzerPin 2

MFRC522 mfrc522(SS_PIN, RST_PIN);

// OTA Config
const char* OTAhostname = "";
const char* OTA_PASSWORD = "";

// WiFi Config
const char *SSID = "";
const char *PASSWORD = "";

// Server Config
String url = "https://trancadura.morea-ifs.org";  
String deviceId;
String apiToken;
String path;
String hexid;

WiFiClientSecure client;   
HTTPClient http;
JsonDocument doc;
AsyncWebServer server(80);

unsigned long buttonLastPressed = 0;
const unsigned long debounceDelay = 200;

bool waitingForCard = false;
unsigned long cardWaitStart = 0;
unsigned long cardWaitDuration = 15000;
unsigned long lastScreenUpdate = 0;
String pendingId = "";

const char* masterCards[] = {
  "D62B24F8"
};
const int masterCardsCount = sizeof(masterCards) / sizeof(masterCards[0]);

bool isMasterCard(const String& uid) {
  for (int i = 0; i < masterCardsCount; i++) {
    if (uid == masterCards[i]) return true;
  }
  return false;
}

enum ScreenState { DEFAULT_SCREEN, WIFI_ERROR, WIFI_CONNECTING, WIFI_CONNECTED, SERVER_CONNECTING, SERVER_CONNECTED, SERVER_OFFLINE, NEW_CARD, SUCCESS, SUCCESS_ADD, MASTER_SUCCESS, DENIED, DENIED_MASTER, ERROR, ERROR_READ };
ScreenState currentScreen = DEFAULT_SCREEN;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void drawLayout(const char* title, const unsigned char* bitmap, uint8_t bmp_width, uint8_t bmp_height, ScreenState state) {
  if (currentScreen == state) return; 
  display.clearDisplay();
  display.drawLine(0, 17, SCREEN_WIDTH, 17, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int16_t text_x = (SCREEN_WIDTH - w) / 2;
  int16_t text_y = 5;
  display.setCursor(text_x, text_y);
  display.print(title);

  int16_t bmp_x = (SCREEN_WIDTH - bmp_width) / 2;
  int16_t bmp_y = 19;
  display.drawBitmap(bmp_x, bmp_y, bitmap, bmp_width, bmp_height, SSD1306_BLACK, SSD1306_WHITE);

  display.display();
  currentScreen = state;
}

void showDefault() { drawLayout("Aproxime um Card...", logoMoreaIcon, 44, 44, DEFAULT_SCREEN); }
void showWifiConnecting() { drawLayout("Conectando no WiFi", wifiIcon, 44, 44, WIFI_CONNECTING); }
void showWifiConnected() { drawLayout("Conectado no WiFi", wifiIcon, 44, 44, WIFI_CONNECTED); }
void showWifierror() { drawLayout("Sem WiFi", wifiIcon, 44, 44, WIFI_ERROR); }
void showServerConnecting() { drawLayout("Conectando no server", serverIcon, 44, 44, SERVER_CONNECTING); }
void showServerConnected() { drawLayout("Conectado no server", serverIcon, 44, 44, SERVER_CONNECTED); }
void showServerOffline() { drawLayout("Servidor Offline", noConnectionIcon, 100, 44, SERVER_OFFLINE); }
void showSuccess(const String& name) { 
  String msg = "Bem-vindo, " + name + "!";

  // Verifica se é o Victor
  if (name.equalsIgnoreCase("Victor") or name.equalsIgnoreCase("Gabrielly")) {
    drawLayout(msg.c_str(), alohomoraIcon, 82, 44, SUCCESS); 
  }else if (name.equalsIgnoreCase("Botao")) {
    drawLayout("Porta Aberta", lockOpenIcon, 44, 44, SUCCESS);
  } else {
    drawLayout(msg.c_str(), lockOpenIcon, 44, 44, SUCCESS); 
  }
}
void showSuccessAdd() { drawLayout("Cartao Adicionado", lockOpenIcon, 44, 44, SUCCESS_ADD); }
void showmasterSuccess() { drawLayout("Cartao Mestre", lockOpenIcon, 44, 44, MASTER_SUCCESS); }
void showDenied() { drawLayout("Acesso Negado!", lockClosedIcon, 44, 44, DENIED); }
void showDeniedmester() { drawLayout("Sem acesso off", lockClosedIcon, 44, 44, DENIED_MASTER); }
void showError() { drawLayout("Erro", errorIcon, 44, 44, ERROR); }
void showErrorRead() { drawLayout("Falha na leitura", errorIcon, 44, 44, ERROR_READ); }
void showNewCard() { drawLayout("Aproxime o NewCard...", cardIcon, 44, 44, NEW_CARD); }

void beepSuccess() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, 2500);
    delay(600);
    noTone(buzzerPin);
    delay(50);
  }
}

void beepError() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, 250);
    delay(800);
    noTone(buzzerPin);
    delay(50);
  }
}

void beepDenied() {
  for (int i = 0; i < 5; i++) {
    tone(buzzerPin, 400);
    delay(80);
    noTone(buzzerPin);
    delay(80);
  }
}
void printLog(const String& message) {
  Serial.println(message);
  TelnetStream.println(message);
}

void setupOTA() {
  ArduinoOTA.setHostname(OTAhostname);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    printLog("Iniciando OTA...");
  });
  ArduinoOTA.onEnd([]() {
    printLog("\nOTA Finalizado.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char buffer[64];
    sprintf(buffer, "Progresso: %u%%", (progress / (total / 100)));
    printLog(buffer);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    printLog("Erro OTA: " + String(error));
  });

  ArduinoOTA.begin();
  printLog("OTA Pronto.");
}

void setupTelnet() {
  TelnetStream.begin();
  printLog("Telnet pronto.");
}

void initWiFi() {
  delay(10);
  printLog("Conectando-se em: " + String(SSID));
  showWifiConnecting();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    printLog(".");
  }

  showWifiConnected();
  printLog("\nConectado na Rede " + String(SSID) + " | IP => " + WiFi.localIP().toString());
}

bool checkCard() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    mfrc522.PCD_Init();
    return false;
  }
  return true;
}
void readCardUID() {
  hexid = "";
  if (!mfrc522.PICC_ReadCardSerial()) {
    printLog("Falha na leitura do cartão!");
    showErrorRead();
    beepError();
    delay(2000);
    showDefault();
    return;
  }

  for (byte i = 0; i < mfrc522.uid.size; i++) {
    hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    hexid += String(mfrc522.uid.uidByte[i], HEX);
  }
  hexid.toUpperCase();

  printLog("UID do Cartão: " + hexid);
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (WiFi.status() != WL_CONNECTED) {
    if (isMasterCard(hexid) ) {
      printLog("Cartão mestre detectado (offline)!");
      showmasterSuccess();
      digitalWrite(relayPin, HIGH);
      delay(200);
      digitalWrite(relayPin, LOW);
      beepSuccess();
      showDefault();
      return;
    } else {
      printLog("Dispositivo offline. Apenas cartões mestres funcionam.");
      showDeniedmester();
      beepDenied();
      delay(2000);  
      showDefault();
      return;
  }
}
  
  if (WiFi.status() == WL_CONNECTED) {
    String sendPath = url + "/approximation";
    String mac_address = WiFi.macAddress();
    String data = "hexid=" + hexid + "&macaddress=" + mac_address;

    client.setInsecure();   
    if (http.begin(client, sendPath)) {  
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int httpResponseCode = http.POST(data);
      String message = http.getString();

      if (httpResponseCode < 0) {
        if (isMasterCard(hexid)) {
          printLog("Cartão mestre detectado (offline)!");
          showmasterSuccess();
          digitalWrite(relayPin, HIGH);
          delay(200);
          digitalWrite(relayPin, LOW);
          beepSuccess();
          showDefault();
          return;
        } else {
          printLog("Falha de conexão com servidor: " + String(httpResponseCode));
          showServerOffline();
          beepError();
          delay(2000);
          showDefault();
        }

      } else if (httpResponseCode == 500) {
        printLog("Erro interno no servidor.");
        showError();
        beepError();
        delay(2000);
        showDefault();

      } else if (httpResponseCode == HTTP_CODE_OK) {
        printLog("UID enviado com sucesso!");
        printLog("Resposta do Servidor: " + message);

        if (message.startsWith("Authorized")) {
          String name = "Visitante";
          int idx = message.indexOf("?first_name=");
          if (idx != -1) {
            name = message.substring(idx + 12); 
          }

          showSuccess(name);
          digitalWrite(relayPin, HIGH);
          delay(200);
          digitalWrite(relayPin, LOW);
          beepSuccess();
          showDefault();
        } else {
          showDenied();
          beepDenied();
          delay(2000);
          showDefault();
        }

      } else {
        printLog("Falha ao enviar o UID: " + String(httpResponseCode));
        showDenied();
        beepDenied();
        delay(2000);
        showDefault();
      }

      http.end();
    }
  }
}

void startCardReadMode(String id) {
  waitingForCard = true;
  cardWaitStart = millis();
  pendingId = id;
  showNewCard();
  printLog("Token válido. Aguardando cartão por 15 segundos...");
}

void processCardReadMode() {
  if (!waitingForCard) return;

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String hexid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      hexid += String(mfrc522.uid.uidByte[i], HEX);
    }
    hexid.toUpperCase();
    printLog("Cartão lido: " + hexid);

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    String sendPath = url + "/newcard";
    String data = "hexid=" + hexid + "&id=" + pendingId;

    client.setInsecure();  
    if (http.begin(client, sendPath)) {  
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int httpResponseCode = http.POST(data);
      String message = http.getString();
      http.end();

      if (httpResponseCode == HTTP_CODE_OK) {
        printLog("UID enviado com sucesso!");
        showSuccessAdd();
        beepSuccess();
      } else {
        printLog("Falha ao enviar UID: " + String(httpResponseCode));
        showError();
        beepDenied();
        delay(2000);
        showDefault();
      }
    }

    waitingForCard = false;
    delay(2000);
    showDefault();
  }

  if (millis() - cardWaitStart >= cardWaitDuration) {
    waitingForCard = false;
    printLog("Tempo esgotado. Nenhum cartão foi aproximado.");
    showDefault();
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  display.clearDisplay();
  showDefault();

  String mac_address = WiFi.macAddress();
  printLog("Endereço Mac: " + mac_address);

  initWiFi();
  setupOTA();
  setupTelnet();

  path = url + "/identify-device";
  String data = "macAddress=" + mac_address;

  client.setInsecure();   
  if (http.begin(client, path)) {   
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode == HTTP_CODE_OK) {
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        deviceId = doc["id"].as<String>();
        apiToken = doc["api_token"].as<String>();
        printLog("Device ID: " + deviceId);
        printLog("API Token: " + apiToken);
        showServerConnected();
        delay(2000);
        showDefault();
      } else {
        showError();
        beepError();
        delay(2000);
        showDefault();
      }
    } else {
      showServerOffline();
      beepError();
    }
    http.end();
  }

  path = url + "/get-device-ip";
  data = "deviceId=" + deviceId + "&deviceIp=" + WiFi.localIP().toString() + "&apiToken=" + apiToken;

  client.setInsecure();  
  if (http.begin(client, path)) {   
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.POST(data);
    http.end();
  }

    server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request) {
      int paramsNr = request->params();
      printLog("Requisição recebida via HTTP GET");

      bool validToken = false;
      String name = "Visitante";  

      for (int i = 0; i < paramsNr; i++) {
        const AsyncWebParameter *p = request->getParam(i);

        if (p->name() == "apiToken" && p->value() == apiToken) {
          validToken = true;
        }

        if (p->name() == "name") {
          name = p->value(); 
        }
      }

      if (validToken) {
        showSuccess(name);  
        digitalWrite(relayPin, HIGH);
        delay(200);
        digitalWrite(relayPin, LOW);
        beepSuccess();
        showDefault();
        request->send(200, "text/plain", "opened");
      } else {
        showDenied();
        beepDenied();
        delay(2000);
        showDefault();
        request->send(400, "text/plain", "wrong api token");
      }
    });


  server.on("/hexid", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool validToken = false;
    String id = "";

    int paramsNr = request->params();
    for (int i = 0; i < paramsNr; i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "apiToken" && p->value() == apiToken) validToken = true;
      if (p->name() == "id") id = p->value();
    }

    if (!validToken || id == "") {
      request->send(400, "text/plain", "Token ou ID inválido.");
      return;
    }

    startCardReadMode(id);
    request->send(200, "text/plain", "Aguardando aproximação do cartão...");
  });

  server.begin();
}

// ===== Loop =====
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentScreen != WIFI_ERROR) showWifierror();
  } else {
    if (currentScreen == DEFAULT_SCREEN) showDefault();
  }

  if (checkCard()) readCardUID();

  if (digitalRead(buttonPin) == LOW && millis() - buttonLastPressed > debounceDelay) {
    showSuccess("Botao");
    digitalWrite(relayPin, HIGH);
    delay(200);
    digitalWrite(relayPin, LOW);
    beepSuccess();
    showDefault();
    buttonLastPressed = millis();
  }

  processCardReadMode();
}

