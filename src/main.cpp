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

// Display Libraries
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

// Json Librarie
#include <ArduinoJson.h>

//RFID Librarie
#include <MFRC522.h>

// ############# CONSTANTS ############### //
#define RST_PIN  22
#define SS_PIN   5
#define relayPin 27
#define wifiLedPin 21
#define failLedPin 26
#define successLedPin 25

MFRC522 mfrc522(SS_PIN, RST_PIN);
struct Cartao {
  String id;
};

// ############# VARIABLES ############### //

// WebSite URL (using HTTP and not HTTPS)
const uint8_t fingerprint[20] = {}; // Server fingerprint

const char *SSID = "Morea-Mobile";     // rede wifi
const char *PASSWORD = "p@ssw0rd1234**"; // senha da rede wifi

String url = "http://trancadura.morea-ifs.org";
String deviceId;
String apiToken;
String path;
String hexid;
// ############### OBJECTS ################# //

WiFiClient client;
HTTPClient http;
JsonDocument doc;

// ############### FUNCTIONS ################# //

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void initWiFi() {
  delay(10);
  Serial.println("Conectando-se em: " + String(SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(wifiLedPin, HIGH);
    delay(50);
    Serial.print(".");
    digitalWrite(wifiLedPin, LOW);
    delay(50);
  }

  digitalWrite(wifiLedPin, HIGH);
  Serial.println();
  Serial.print("Conectado na Rede " + String(SSID) + " | IP => ");
  Serial.println(WiFi.localIP());
}

bool checkCard() {
  return mfrc522.PICC_IsNewCardPresent();
}

void readCardUID() {
  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Falha na leitura do cartão!");
    return;
  }

  Serial.print("UID do Cartão: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    hexid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    hexid += String(mfrc522.uid.uidByte[i], HEX); 
    hexid.toUpperCase();
  }
  Serial.println(hexid);

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
      Serial.println("UID enviado com sucesso!");
      Serial.println("Resposta do Servidor: " + message);

      DeserializationError error = deserializeJson(doc, message);
      if (error) {

        if (message == "Authorized") {
          digitalWrite(successLedPin, HIGH);
          digitalWrite(relayPin, HIGH);
          digitalWrite(wifiLedPin, LOW);
          delay(5000);
          digitalWrite(relayPin, LOW);
          digitalWrite(successLedPin, LOW);
          digitalWrite(wifiLedPin, HIGH);
        } else {
          digitalWrite(failLedPin, HIGH);
          digitalWrite(wifiLedPin, LOW);
          delay(5000);
          digitalWrite(failLedPin, LOW);
          digitalWrite(wifiLedPin, HIGH);
        }
      } else {
        Serial.println("Erro ao processar resposta do servidor.");
      }
    } else {
      Serial.println("Falha ao enviar o UID: " + String(httpResponseCode));
      digitalWrite(failLedPin, HIGH);
      delay(5000);
      digitalWrite(failLedPin, LOW);
    }
    http.end();
  }


  hexid = "";
}

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  // Pins Setup
  pinMode(relayPin, OUTPUT);
  pinMode(wifiLedPin, OUTPUT);
  pinMode(failLedPin, OUTPUT);
  pinMode(successLedPin, OUTPUT);

  // Get Mac Address
  String mac_address = WiFi.macAddress();
  Serial.println("Endereço Mac: " + mac_address);

  // Init Wifi And Connect
  initWiFi();

  path = url + "/identify-device";
  String data = "macAddress=" + mac_address;

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();

  //  Sending Mac Address to the Server
  if (http.begin(*client, path)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0) {
      Serial.println("request error - " + httpResponseCode);
    }

    if (httpResponseCode != HTTP_CODE_OK) {
      Serial.println("Falha no Envio");
      Serial.println(httpResponseCode);
    }

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Deserialization error");

      digitalWrite(failLedPin, HIGH);
      digitalWrite(successLedPin, HIGH);

      return;
    }

    deviceId = doc["id"].as<String>();
    apiToken = doc["api_token"].as<String>();

    Serial.println("Device ID: " + deviceId);
    Serial.println("API Token: " + apiToken);

    http.end();
  }

  Serial.println();
  Serial.print("ESP IP Address: http://" + WiFi.localIP().toString());

  path = url + "/get-device-ip";
  data = "deviceId=" + deviceId + "&deviceIp=" + WiFi.localIP().toString() + "&apiToken=" + apiToken;
  Serial.println();

  //  Sending Ip Address to the Server
  if (http.begin(*client, path)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0) {
      Serial.println("request error - " + httpResponseCode);
    }

    if (httpResponseCode != HTTP_CODE_OK) {
      Serial.println("Falha no Envio");
      Serial.println(httpResponseCode);
    }

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Deserialization error");

      digitalWrite(failLedPin, HIGH);
      digitalWrite(successLedPin, HIGH);

      return;
    }

    String responseMessage = doc["message"].as<String>();
    Serial.println("Response Message: " + responseMessage);

    http.end();
  }

  // Receive an HTTP GET request
  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request) {
    int paramsNr = request->params();
    Serial.println();

    for (int i = 0; i < paramsNr; i++) {
      const AsyncWebParameter *p = request->getParam(i);

      Serial.print("Param name: ");
      Serial.println(p->name());

      Serial.print("Param value: ");
      Serial.println(p->value());

      if (p->name() == "apiToken") {
        if (p->value() == apiToken) {
          digitalWrite(successLedPin, HIGH);
          digitalWrite(relayPin, HIGH);
          delay(250);
          digitalWrite(relayPin, LOW);
          digitalWrite(successLedPin, LOW);

          Serial.println("Opened.");
          request->send(200, "text/plain", "opened");
        } else {
          digitalWrite(failLedPin, HIGH);
          delay(250);
          digitalWrite(failLedPin, LOW);

          Serial.println("Wrong API Token.");
          request->send(400, "text/plain", "wrong api token");
        }
      }
    }
  });

  server.onNotFound(notFound);
  server.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }
  if (checkCard()) {
    readCardUID();
  }
  delay(500);
}