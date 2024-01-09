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

#include <ArduinoJson.h>

// ############# CONSTANTS ############### //
#define relayPin 5

// ############# VARIABLES ############### //

const char *SSID = "Felipe-Network";          // rede wifi
const char *PASSWORD = "Felipe2020"; // senha da rede wifi

const char *url = "http://192.168.82.73";
const int nodeId = 1;
String deviceId;
char path[512];

// ############### OBJECTS ################# //

WiFiClient client;
HTTPClient http;
JsonDocument doc;

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
    delay(100);
    Serial.print(".");
  }
  

  Serial.println();
  Serial.print("Conectado na Rede " + String(SSID) + " | IP => ");
  Serial.println(WiFi.localIP());
}

AsyncWebServer server(80);

void setup()
{
  Serial.begin(115200);

  
  // Init Wifi And Connect
  initWiFi();

  String mac_address = WiFi.macAddress();

  Serial.println(mac_address);

  sprintf(path, "%s/identify-device", url);
  String data = "macAddress=" + mac_address;

  Serial.println(data);

  if(http.begin(client, path)){
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(data);
    String payload = http.getString();

    Serial.println(httpResponseCode);
    Serial.println(payload);

    DeserializationError error = deserializeJson(doc, payload);

    if(error){
      Serial.println("deserialization error");
      return;
    }

    deviceId = doc["id"].as<String>();

    Serial.println(deviceId);

    http.end();
  }

  // Pins Setup
  pinMode(relayPin, OUTPUT);
  

  Serial.println();
  Serial.print("ESP IP Address: http://");
  Serial.println(WiFi.localIP());

  sprintf(path, "%s/receiveIp?nodeId=%d&deviceIp=%s", url, nodeId, WiFi.localIP().toString().c_str());
  Serial.println(path);

  if (http.begin(client, path)){
    int httpCode = http.GET();

    if (httpCode < 0){
      Serial.println("request error - " + httpCode);
    }

    if (httpCode != HTTP_CODE_OK){
      Serial.println("Falha no Envio");
      Serial.println(httpCode);
    }

    String response = http.getString();
    http.end();

    Serial.println("Request Response:" + response);
  }

  // Receive an HTTP GET request
  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "ok"); 

    digitalWrite(relayPin, HIGH);
    delay(250);
    digitalWrite(relayPin, LOW);

    Serial.println("received");
  });

  server.onNotFound(notFound);
  server.begin();
}

void loop(){

}