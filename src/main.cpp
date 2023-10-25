#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <string.h>
#include <SPI.h>

#include "wifi_cfg.h"
#include "index_html.h"

ESP8266WebServer server(80);
#define D1MINI_SPI_CS_PIN D3

void sendSPIData(String data) {
    // MOSI pin = D7/GPIO13 on D1 and D1 mini.
    // SS pin = D10/GPIO15 on D1.
    digitalWrite(SS, LOW);
    digitalWrite(D1MINI_SPI_CS_PIN, LOW);
    // 1 MHz clock results in 1 us bit length for nice 1 MHz sampling.
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    for (unsigned int i = 0; i < data.length(); i++) {
        SPI.transfer(data[i]);
    }
    SPI.endTransaction();
    digitalWrite(SS, HIGH);
    digitalWrite(D1MINI_SPI_CS_PIN, HIGH);
}

void serverGet() {
    server.send(200, "text/html", index_html);
}

void serverPost() {
    if (!server.hasArg("plain")) {
        Serial.println("POST with no body.");
        server.send(400, "text/html", "NOT OK");
        return;
    }
    server.send(200, "text/html", "OK");
    Serial.println("POST with body:");
    String body = server.arg("plain");
    Serial.println(body);

    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
    sendSPIData(body);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // Among other SPI pin setup, sets chip select (CS or SS) pin as output and high.
    // SS is not defined on D1 mini, hence need to setup an alternative pin.
    SPI.begin();
    pinMode(D1MINI_SPI_CS_PIN, OUTPUT);
    digitalWrite(D1MINI_SPI_CS_PIN, HIGH);
    pinMode(LED_BUILTIN, OUTPUT);

    IPAddress subnet(255, 255, 255, 0);
    WiFi.mode(WIFI_STA);

    Serial.println("");
    if (_WIFI_IP.toString() == INADDR_NONE.toString()) {
        Serial.println("Using dynamic IP address.");
    } else {
        Serial.print("Using static IP address: ");
        Serial.println(_WIFI_IP.toString());
        WiFi.config(_WIFI_IP, _WIFI_GATEWAY, subnet);
    }

    WiFi.begin(_WIFI_SSID, _WIFI_PASSWORD);

    int timeout = 0;
    Serial.print("Connecting to: ");
    Serial.println(_WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED && timeout < 10) {
        delay(1000);
        Serial.print(".");
        timeout++;
    }
    Serial.println("");
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Unable to connect.");
    } else {
        Serial.print("WiFi connected using IP address: ");
        Serial.println(WiFi.localIP());
    }

    server.on("/", HTTP_GET, serverGet);
    server.on("/", HTTP_POST, serverPost);

    server.begin();
    Serial.println("Server is running.");

    digitalWrite(LED_BUILTIN, HIGH); delay(250);
    digitalWrite(LED_BUILTIN, LOW); delay(250);
    digitalWrite(LED_BUILTIN, HIGH); delay(250);
    digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
    server.handleClient();
}
