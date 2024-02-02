#include <Arduino.h>

// Wifi Librarie
#include <ESP8266WiFi.h>

// HTTP Request Libraries
#include <ESP8266HTTPClient.h>
#include <stdio.h>
#include <Wire.h>

// HTTP Server Libraries
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>


// Display Libraries
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Json Librarie
#include <ArduinoJson.h>

// ############# CONSTANTS ############### //
#define relayPin 14
#define wifiLedPin 0
#define failLedPin 5
#define successLedPin 4
#define buzzerPin 2

// ############# VARIABLES ############### //

const char *SSID = "Morea-Mobile";  // rede wifi
const char *PASSWORD = "p@ssw0rd1234**"; // senha da rede wifi

String url = "http://trancadura.morea-ifs.org";
String deviceId;
String apiToken;
String path;

// ############### OBJECTS ################# //

WiFiClient client;
HTTPClient http;
JsonDocument doc;

// ############### FUNCTIONS ################# //

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

void initWiFi()
{
  delay(10);
  Serial.println("Conectando-se em: " + String(SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
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

AsyncWebServer server(80);

void setup()
{
  Serial.begin(115200);
  
  // Pins Setup
  pinMode(relayPin, OUTPUT);
  pinMode(wifiLedPin, OUTPUT);
  pinMode(failLedPin, OUTPUT);
  pinMode(successLedPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  // Get Mac Address
  String mac_address = WiFi.macAddress();
  Serial.println("Endereço Mac: " + mac_address);

  // Init Wifi And Connect
  initWiFi();

  path = url + "/identify-device";
  String data = "macAddress=" + mac_address;

  //  Sending Mac Address to the Server
  if(http.begin(client, path)){
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0){
      Serial.println("request error - " + httpResponseCode);
    }

    if (httpResponseCode != HTTP_CODE_OK){
      Serial.println("Falha no Envio");
      Serial.println(httpResponseCode);
    }

    DeserializationError error = deserializeJson(doc, payload);

    if(error){
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
  if (http.begin(client, path)){
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    if (httpResponseCode < 0){
      Serial.println("request error - " + httpResponseCode);
    }

    if (httpResponseCode != HTTP_CODE_OK){
      Serial.println("Falha no Envio");
      Serial.println(httpResponseCode);
    }
    
    DeserializationError error = deserializeJson(doc, payload);
    
    if(error){
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
  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request){

    int paramsNr = request->params();
    Serial.println();

    for (int i = 0; i < paramsNr; i++)
    {
      AsyncWebParameter* p = request->getParam(i);
      Serial.print("Param name: ");
      Serial.println(p->name());
      
      Serial.print("Param value: ");
      Serial.println(p->value());
      
      if(p->name() == "apiToken") {
        if(p->value() == apiToken) {
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

void loop(){
  if (WiFi.status() != WL_CONNECTED){
    initWiFi();
  }
}