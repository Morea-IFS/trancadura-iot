#include <Arduino.h>

// Wifi Librarie
#include <WiFi.h>

// HTTP Request Libraries
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <stdio.h>

// HTTP Server Libraries
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>

// Json Librarie
#include <ArduinoJson.h>

// RFID Librarie
#include <MFRC522.h>

// OTA e Telnet
#include <ArduinoOTA.h>
#include <TelnetStream.h>

#define RST_PIN  22
#define SS_PIN   5
#define relayPin 27
#define wifiLedPin 21
#define failLedPin 26
#define successLedPin 25
#define buttonPin 15
#define buzzerPin 2

MFRC522 mfrc522(SS_PIN, RST_PIN);
struct Cartao {
  String id;
};

const char* OTA_PASSWORD = "";

const uint8_t fingerprint[20] = {0x1D, 0x67, 0x1B, 0xAD, 0x54, 0xC8, 0xF5, 0xC2, 0x85, 0xA2, 0x4E, 0x21, 0xEF, 0x9D, 0x4A, 0xF8, 0xCF, 0x01, 0x91, 0xB6};

const char *SSID = "";
const char *PASSWORD = "";

String url = "https://trancadura.morea-ifs.org";
String deviceId;
String apiToken;
String path;
String hexid;

WiFiClient client;
HTTPClient http;
JsonDocument doc;
AsyncWebServer server(80);

unsigned long ledTimer = 0;
unsigned long ledDuration = 0;
bool ledActive = false;

unsigned long buttonLastPressed = 0;
const unsigned long debounceDelay = 200;

void printLog(const String& message) {
  Serial.println(message);
  TelnetStream.println(message);
}

void setupOTA() {
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

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void initWiFi() {
  delay(10);
  printLog("Conectando-se em: " + String(SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(wifiLedPin, HIGH);
    delay(50);
    digitalWrite(wifiLedPin, LOW);
    delay(50);
    printLog(".");
  }

  digitalWrite(wifiLedPin, HIGH);
  printLog("\nConectado na Rede " + String(SSID) + " | IP => " + WiFi.localIP().toString());
}

bool checkCard() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    mfrc522.PCD_Init(); 
    return false;
  }
  return true;
}

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
;
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

void activateSuccessLed() {
  digitalWrite(successLedPin, HIGH);
  digitalWrite(relayPin, HIGH);
  digitalWrite(wifiLedPin, LOW);
  beepSuccess();
  ledTimer = millis();
  ledDuration = 3000;
  ledActive = true;
}

void activateFailLed() {
  digitalWrite(failLedPin, HIGH);
  digitalWrite(wifiLedPin, LOW);
  beepDenied(); 
  ledTimer = millis();
  ledDuration = 2000;
  ledActive = true;
}

void handleLedTimer() {
  if (ledActive && millis() - ledTimer >= ledDuration) {
    digitalWrite(successLedPin, LOW);
    digitalWrite(failLedPin, LOW);
    digitalWrite(relayPin, LOW);
    digitalWrite(wifiLedPin, HIGH);
    ledActive = false;
  }
}

void readCardUID() {
  hexid = "";
  if (!mfrc522.PICC_ReadCardSerial()) {
    printLog("Falha na leitura do cartão!");
    digitalWrite(failLedPin, HIGH);
    digitalWrite(wifiLedPin, LOW);
    beepError(); 
    ledTimer = millis();
    ledDuration = 800;
    ledActive = true;
    return;
  }

  printLog("UID do Cartão: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    hexid += String(mfrc522.uid.uidByte[i], HEX);
    hexid.toUpperCase();
  }
  printLog(hexid);
  mfrc522.PICC_HaltA();     
  mfrc522.PCD_StopCrypto1();
  String sendPath = url + "/approximation";
  String mac_address = WiFi.macAddress();
  String data = "hexid=" + hexid + "&macaddress=" + mac_address;

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();

  if (http.begin(*client, sendPath)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpResponseCode = http.POST(data);
    String message = http.getString();

    if (httpResponseCode == HTTP_CODE_OK) {
      printLog("UID enviado com sucesso!");
      printLog("Resposta do Servidor: " + message);

      DeserializationError error = deserializeJson(doc, message);
      if (error) {
        if (message == "Authorized") {
          activateSuccessLed();
        } else {
          activateFailLed();
        }
      } else {
        printLog("Erro ao processar resposta do servidor.");
      }
    } else {
      printLog("Falha ao enviar o UID: " + String(httpResponseCode));
      activateFailLed();
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  pinMode(relayPin, OUTPUT);
  pinMode(wifiLedPin, OUTPUT);
  pinMode(failLedPin, OUTPUT);
  pinMode(successLedPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP); 
  pinMode(buzzerPin, OUTPUT);     

  String mac_address = WiFi.macAddress();
  printLog("Endereço Mac: " + mac_address);

  initWiFi();
  setupOTA();
  setupTelnet();

  path = url + "/identify-device";
  String data = "macAddress=" + mac_address;

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();

  if (http.begin(*client, path)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0) {
      printLog("request error - " + String(httpResponseCode));
    }

    if (httpResponseCode != HTTP_CODE_OK) {
      printLog("Falha no Envio");
      printLog(String(httpResponseCode));
    }

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      printLog("Deserialization error");
      digitalWrite(failLedPin, HIGH);
      digitalWrite(successLedPin, HIGH);
      return;
    }

    deviceId = doc["id"].as<String>();
    apiToken = doc["api_token"].as<String>();

    printLog("Device ID: " + deviceId);
    printLog("API Token: " + apiToken);

    http.end();
  }

  printLog("ESP IP Address: http://" + WiFi.localIP().toString());

  path = url + "/get-device-ip";
  data = "deviceId=" + deviceId + "&deviceIp=" + WiFi.localIP().toString() + "&apiToken=" + apiToken;

  if (http.begin(*client, path)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0) {
      printLog("request error - " + String(httpResponseCode));
    }

    if (httpResponseCode != HTTP_CODE_OK) {
      printLog("Falha no Envio");
      printLog(String(httpResponseCode));
    }

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      printLog("Deserialization error");
      digitalWrite(failLedPin, HIGH);
      digitalWrite(successLedPin, HIGH);
      return;
    }

    String responseMessage = doc["message"].as<String>();
    printLog("Response Message: " + responseMessage);

    http.end();
  }

  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request) {
    int paramsNr = request->params();
    printLog("Requisição recebida via HTTP GET");

    for (int i = 0; i < paramsNr; i++) {
      const AsyncWebParameter *p = request->getParam(i);

      printLog("Param name: " + p->name());
      printLog("Param value: " + p->value());

      if (p->name() == "apiToken") {
        if (p->value() == apiToken) {
          activateSuccessLed();
          printLog("Opened.");
          request->send(200, "text/plain", "opened");
        } else {
          digitalWrite(failLedPin, HIGH);
          beepDenied(); 
          delay(250);
          digitalWrite(failLedPin, LOW);
          printLog("Wrong API Token.");
          request->send(400, "text/plain", "wrong api token");
        }
      }
    }
  });
  server.on("/hexid", HTTP_GET, [](AsyncWebServerRequest *request) {
    printLog("Requisição recebida via /hexid");

    bool validToken = false;
    int paramsNr = request->params();
    for (int i = 0; i < paramsNr; i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if (p->name() == "apiToken" && p->value() == apiToken) {
        validToken = true;
        break;
      }
    }

    if (!validToken) {
      printLog("Token inválido.");
      digitalWrite(failLedPin, HIGH);
      beepDenied();
      delay(250);
      digitalWrite(failLedPin, LOW);
      request->send(400, "text/plain", "wrong api token");
      return;
    }

    request->send(200, "text/plain", "Aguardando aproximação do cartão...");
    printLog("Token válido. Aguardando cartão por 15 segundos...");

    unsigned long startTime = millis();
    bool cardRead = false;
    bool ledState = false;
    unsigned long lastBlink = 0;

    while (millis() - startTime < 15000 && !cardRead) {
      if (millis() - lastBlink >= 500) {
        ledState = !ledState;
        digitalWrite(successLedPin, ledState ? HIGH : LOW);
        lastBlink = millis();
      }

      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String hexid = "";
        printLog("UID do Cartão: ");
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
          hexid += String(mfrc522.uid.uidByte[i], HEX);
        }
        hexid.toUpperCase();
        printLog(hexid);

        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();


        String sendPath = url + "/newcard"; 
        String mac_address = WiFi.macAddress();
        String data = "hexid=" + hexid + "&macaddress=" + mac_address;

        std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
        client->setInsecure();

        if (http.begin(*client, sendPath)) {
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          int httpResponseCode = http.POST(data);
          String message = http.getString();

          if (httpResponseCode == HTTP_CODE_OK) {
            printLog("UID enviado com sucesso!");
            printLog("Resposta do Servidor: " + message);

            DeserializationError error = deserializeJson(doc, message);
            if (error) {
              if (message == "recepte") {
                activateSuccessLed();
              } else {
                activateFailLed();
              }
            } else {
              printLog("Erro ao processar resposta do servidor.");
              activateFailLed();
            }
          } else {
            printLog("Falha ao enviar o UID: " + String(httpResponseCode));
            activateFailLed();
          }
          http.end();
        }

        cardRead = true;
      }

      delay(10);
    }

 
    digitalWrite(successLedPin, LOW);

    if (!cardRead) {
      printLog("Tempo esgotado. Nenhum cartão foi aproximado.");
    }
  });
  server.onNotFound(notFound);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }
  if (checkCard()) {
    readCardUID();
  }

  if (digitalRead(buttonPin) == LOW) {
    if (millis() - buttonLastPressed > debounceDelay) {
      activateSuccessLed();
      printLog("Botão pressionado: porta aberta.");
      buttonLastPressed = millis();
    }
  }

  handleLedTimer();
}