#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <string.h>
#include <SPI.h>

#include "wifi_cfg.h"
#include "index_html.h"

ESP8266WebServer server(80);
const uint maxBufferSize = 1000;

#define END_OF_TEXT '\x3'
#define END_OF_TRANSMISSION '\x4'

// 1 MHz clock results in 1 us bit length for nice 1 or 2 MHz sampling.
// A 100 kHz clock is less error-prone and works better with bad wires.
void transferSPIData(String data,
        bool isSending = true,
        bool useNull = false,
        bool useLoop = false,
        ulong rate = 100000,
        uint msDelay = 0
    ) {

    if (!useNull) {
        data += END_OF_TEXT;
        data += END_OF_TRANSMISSION;
    }
    data += '\0';

    byte receiveBuffer[isSending ? data.length() : maxBufferSize];

    if (useLoop) {
        Serial.print("Using loop with delay ");
        Serial.print(msDelay);
        Serial.print(" to transfer");
        if (!isSending) Serial.print(" and echo back");
    } else {
        Serial.print("Transferring");
    }
    if (isSending) {
        Serial.print(" the following text with length ");
        Serial.print(data.length());
        Serial.println(" bytes:");
        Serial.println(data);
    } else {
        Serial.println(" bytes.");
    }

    Serial.print("Using clock rate: ");
    Serial.println(rate);

    byte sendBuffer[sizeof(receiveBuffer)];
    sendBuffer[0] = '\0';
    if (isSending) data.getBytes(sendBuffer, data.length());

    bool echoBack = !isSending;
    uint_fast8_t controlEndBytesToGo = useNull ? 1 : 3;
    uint receivedDataSize = sizeof(receiveBuffer);

    digitalWrite(D8, LOW);
    digitalWrite(D2, HIGH);

    SPI.beginTransaction(SPISettings(rate, MSBFIRST, SPI_MODE0));

    // This is for testing if some delay between bytes is required.
    // If using transferBytes, the echoed back data might just be what was received for slow ISR routines.
    if (useLoop) {
        for (uint i = 0; i < sizeof(sendBuffer); i++) {
            receiveBuffer[i] = SPI.transfer(sendBuffer[i]);

            if (echoBack) {
                sendBuffer[i+1] = receiveBuffer[i];
                if (i == maxBufferSize - 2) echoBack = false; // Don't try to echo back beyond buffer size.
                if (receiveBuffer[i] == END_OF_TEXT && controlEndBytesToGo == 3) controlEndBytesToGo = 2;
                if (receiveBuffer[i] == END_OF_TRANSMISSION && controlEndBytesToGo == 2) controlEndBytesToGo = 1;
                if (receiveBuffer[i] == '\0' && controlEndBytesToGo == 1) {
                    receivedDataSize = i+1;
                    Serial.print("End control bytes received, total byte count: ");
                    Serial.println(receivedDataSize);
                    i = maxBufferSize; // End loop.
                }
            }
            if (msDelay > 0) delayMicroseconds(msDelay);
        }
    } else {
        SPI.transferBytes(sendBuffer, receiveBuffer, sizeof(sendBuffer));
    }

    SPI.endTransaction();
    digitalWrite(D8, HIGH);
    digitalWrite(D2, LOW);

    // Don't print added control bytes. On send, we don't account for the sent null byte.
    if (!useNull) receiveBuffer[receivedDataSize-(isSending ? 2 : 3)] = '\0';

    // Skip first byte as that's just noise.
    Serial.println("Received the following, trimmed of control bytes:");
    Serial.println(reinterpret_cast<const char*>(const_cast<const byte*>(&receiveBuffer[1])));
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

    // Comma separates configuration parameters, colon the data.
    uint colonIndex = body.indexOf(':');
    const String data = body.substring(colonIndex+1);
    body = body.substring(0, colonIndex);

    // Order of configuration parameters:
    // is sending, control byte, clock, use loop, delay for loop
    uint commaIndex = body.indexOf(',');
    bool isSending = body.substring(0, commaIndex)[0] == '1';
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    bool useNull = body.substring(0, commaIndex)[0] == '1';
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    ulong rate = body.substring(0, commaIndex).toInt();
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    bool useLoop = body.substring(0, commaIndex)[0] == '1';
    body = body.substring(commaIndex+1);

    ulong msDelay = body.toInt();

    transferSPIData(data, isSending, useNull, useLoop, rate, msDelay);
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
