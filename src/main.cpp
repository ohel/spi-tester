#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <string.h>
#include <SPI.h>

#include "wifi_cfg.h"
#include "index_html.h"

ESP8266WebServer server(80);

void sendSPIData(String data, bool onlyNullTerminated) {
    // The idea is the receiver echoes back what we send here so we can test if both MOSI and MISO work.
    byte received[data.length() + (onlyNullTerminated ? 1 : 3)];
    if (!onlyNullTerminated) {
        data += '\x3'; // end of text
        data += '\x4'; // end of transmission
    }
    data += '\0';

    digitalWrite(D8, LOW);
    digitalWrite(D2, HIGH);
    // Here we would wait for a handshake pin to be ready, but just sleep a millisecond instead.
    delay(1);

    // 1 MHz clock results in 1 us bit length for nice 1 or 2 MHz sampling.
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

    // Skip receiving first byte.
    for (unsigned int i = 0; i < data.length(); i++) {
        received[i] = SPI.transfer(data[i]);
    }

    SPI.endTransaction();
    digitalWrite(D8, HIGH);
    digitalWrite(D2, LOW);

    // Don't print added control bytes.
    if (!onlyNullTerminated) received[data.length()-3]='\0';

    Serial.println("Read back the following: ");
    Serial.println(reinterpret_cast<const char*>(const_cast<const byte*>(&received[0])));
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
    bool onlyNullTerminated = body[0] == '0';
    body = body.substring(1);

    Serial.print("Is only null terminated string: ");
    Serial.println(onlyNullTerminated);
    Serial.println("Sending the following text:");
    Serial.println(body);

    sendSPIData(body, onlyNullTerminated);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // MOSI pin = GPIO13 = pin D7 on D1/D1 mini.
    // MISO pin = GPIO12 = pin D6 on D1/D1 mini.
    // SCK pin = GPIO14 = pin D5 on D1/D1 mini.
    // SS pin = GPIO15 = pin D10 on D1, pin D8 on D1 mini. NB: Boot fails if pulled high.

    // Sets SPI pins to work as main.
    // It is required to call this! Otherwise SPI.beginTransaction() does _not_ work.
    // Note: on ESP8266, doesn't seem to set SS high.
    // Also on ESP8266: SCK, MOSI and MISO pin modes must be SPECIAL and not OUTPUT to work. Otherwise nothing is ever transmitted!
    SPI.begin();
    pinMode(D8, OUTPUT); // Use D8 instead of SS to pull down CS pin on sub.
    pinMode(D2, OUTPUT); // Use D2 as D8 complement.
    digitalWrite(D8, HIGH);
    digitalWrite(D2, LOW);
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

    Serial.print("Relevant pins (SS, MOSI, MISO, SCK, D8, D2): ");
    Serial.print(SS); Serial.print(",");
    Serial.print(MOSI); Serial.print(",");
    Serial.print(MISO); Serial.print(",");
    Serial.print(SCK); Serial.print(",");
    Serial.print(D8); Serial.print(",");
    Serial.println(D2);
    Serial.print("Pin status: ");
    Serial.print(digitalRead(SS)); Serial.print(",");
    Serial.print(digitalRead(MOSI)); Serial.print(",");
    Serial.print(digitalRead(MISO)); Serial.print(",");
    Serial.print(digitalRead(SCK)); Serial.print(",");
    Serial.print(digitalRead(D8)); Serial.print(",");
    Serial.println(digitalRead(D2));

    digitalWrite(LED_BUILTIN, HIGH); delay(250);
    digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
    server.handleClient();
}
