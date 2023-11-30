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

// ############# CONSTANTS ############### //
#define relayPin 5

// ############# VARIABLES ############### //

const char *SSID = "Morea";          // rede wifi
const char *PASSWORD = "morea123**"; // senha da rede wifi

const char *url = "http://192.168.1.37";
const int nodeId = 1;
char path[512];

// ############### OBJECTS ################# //

WiFiClient client;
HTTPClient http;

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

  // Pins Setup
  pinMode(relayPin, OUTPUT);
  
  // Init Wifi And Connect
  initWiFi();

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