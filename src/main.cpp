#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <string.h>
#include <SPI.h>

#include "wifi_cfg.h"
#include "index_html.h"

ESP8266WebServer server(80);

void sendSPIData(String data) {
    // MOSI pin = GPIO13 = pin D7 on D1/D1 mini.
    // SCLK pin = GPIO14 = pin D5 on D1/D1 mini.
    // SS pin = GPIO15 = pin D10 on D1, pin D8 on D1 mini. NB: Boot fails if pulled high.
    digitalWrite(SS, LOW);
    // 1 MHz clock results in 1 us bit length for nice 1 MHz sampling.
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    for (unsigned int i = 0; i < data.length(); i++) {
        SPI.transfer(data[i]);
    }
    SPI.transfer('\0');
    SPI.endTransaction();
    digitalWrite(SS, HIGH);
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

    // Sets SPI pins to work as main.
    SPI.begin();
    pinMode(LED_BUILTIN, OUTPUT);

    IPAddress subnet(255, 255, 255, 0);
    bool is_ap =
        _WIFI_IP.toString() != INADDR_NONE.toString() &&
        _WIFI_GATEWAY.toString() == INADDR_NONE.toString();

    // Note: here WiFi setup only as AP or STA, not both (WIFI_AP_STA).
    if (is_ap) {
        WiFi.mode(WIFI_AP);
        Serial.println("Setting up AP:");
        Serial.print("    SSID: ");
        Serial.println(_WIFI_SSID);
        Serial.print("    Password: ");
        if (strlen(_WIFI_PASSWORD) < 8) {
            Serial.println("");
            WiFi.softAP(_WIFI_SSID);
        } else {
            Serial.println(_WIFI_PASSWORD);
            WiFi.softAP(_WIFI_SSID, _WIFI_PASSWORD);
        }
        Serial.print("    IP/Gateway: ");
        Serial.println(_WIFI_IP);
        WiFi.softAPConfig(_WIFI_IP, _WIFI_IP, subnet);
    } else {
        WiFi.mode(WIFI_STA);
        if (_WIFI_IP.toString() == INADDR_NONE.toString()) {
            Serial.println("Using dynamic IP address.");
        } else {
            WiFi.config(_WIFI_IP, _WIFI_GATEWAY, subnet);
        }
    }

    WiFi.begin(_WIFI_SSID, _WIFI_PASSWORD);

    if (!is_ap) {
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
