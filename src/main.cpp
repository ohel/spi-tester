#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <string.h>
#include <SPI.h>

#include "wifi_cfg.h"
#include "index_html.h"

ESP8266WebServer server(80);
const uint maxBufferSize = 1000;
const uint maxErrorCount = 10;

#define END_OF_TEXT '\x3'
#define END_OF_TRANSMISSION '\x4'
#define BELL '\x7'
#define BACKSPACE '\b'
#define NULL_BYTE '\0'

const bool enableVerboseLog = true;

inline void verboseLog(String msg) {
    if (enableVerboseLog) Serial.println(msg);
}

// 1 MHz clock results in 1 us bit length for nice 1 or 2 MHz sampling.
// A 100 kHz clock is less error-prone and works better with bad wires.
void transferSPIData(
    String data,
    bool isSending = true,
    bool useNull = false,
    bool useLoop = false,
    ulong rate = 100000,
    uint usDelay = 0,
    bool checkErrors = false,
    bool leadingNulls = false,
    bool generateErrors = false) {

    if (!useNull) {
        data += END_OF_TEXT;
        data += END_OF_TRANSMISSION;
    }

    // Account for null byte at the end.
    byte receiveBuffer[isSending ? (data.length() + 1) : maxBufferSize];
    receiveBuffer[sizeof(receiveBuffer) - 1] = NULL_BYTE;

    if (useLoop) {
        Serial.print("Using loop, delay " + String(usDelay) + "µs, transfer");
        if (!isSending) Serial.print(" (receive)");
        if (checkErrors) Serial.print(" with error checking");
    } else {
        Serial.print("Transfer");
    }
    if (isSending) {
        Serial.println(" " + String(data.length()) + " bytes" + (leadingNulls ? " + 5 leading nulls:" : ":"));
        Serial.println(data);
    } else {
        Serial.println(" until control bytes received.");
    }

    Serial.println("Using clock rate: " + String(rate));

    byte sendBuffer[sizeof(receiveBuffer)];
    if (isSending) data.getBytes(sendBuffer, sizeof(receiveBuffer));
    sendBuffer[sizeof(sendBuffer) - 1] = NULL_BYTE;

    bool receiving = !isSending;
    uint_fast8_t controlEndBytesToGo = 2;
    uint_fast8_t errorCount = 0;
    uint receivedDataSize = sizeof(receiveBuffer);
    uint maxIndex = sizeof(sendBuffer) - (receiving ? 1 : 0);

    digitalWrite(D8, LOW);
    digitalWrite(D2, HIGH);

    SPI.beginTransaction(SPISettings(rate, MSBFIRST, SPI_MODE0));

    // This is for testing if some delay between bytes is required.
    // If using transferBytes, the echoed back data might just be what was received for slow ISR routines.
    if (useLoop) {
        // Leading null bytes can be used at the sub end to detect who is expected to send data.
        if (isSending && leadingNulls) {
            for (uint_fast8_t i = 0; i < 5; i++) {
                byte received = SPI.transfer(NULL_BYTE);
                verboseLog("Sent: " + String(char(NULL_BYTE)) + ", got: " + String(char(received)));
                if (usDelay > 0) delayMicroseconds(usDelay);
            }
        }

        // The first byte is expected to be just noise if receiving,
        // since master (this end) always initiates transfer anyway.
        // Also effectively ignores the first byte on error checking.
        if (receiving) sendBuffer[0] = NULL_BYTE;
        receiveBuffer[0] = SPI.transfer(sendBuffer[0]);
        verboseLog("Sent: " + String(char(sendBuffer[0])));
        byte lastSentByte = sendBuffer[0];

        // Send the received byte back on next round.
        if (receiving) sendBuffer[1] = receiveBuffer[0];

        if (usDelay > 0) delayMicroseconds(usDelay);
        bool error = false;

        for (uint i = 1; i < maxIndex; i++) {

            if (error) {
                verboseLog("Sent: BACKSPACE");
                receiveBuffer[i] = SPI.transfer(BACKSPACE);
                if (isSending) i -= 1;
                error = false;
                errorCount++;
                if (errorCount > maxErrorCount) break;
                lastSentByte = BACKSPACE;
                if (usDelay > 0) delayMicroseconds(usDelay);
                continue;
            } else {
                byte byteToSend = sendBuffer[lastSentByte == BACKSPACE ? i-1 : i];
                if (generateErrors && (random(5) == 0)) byteToSend = BELL;
                receiveBuffer[i] = SPI.transfer(byteToSend);
                verboseLog("Sent: " + String(char(byteToSend)));
            }

            if (receiving) {
                sendBuffer[i+1] = receiveBuffer[i]; // Echoes back bytes next round.

                // Will result in BACKSPACE echoed next round and buffer rolled back.
                if (checkErrors && receiveBuffer[i] == BACKSPACE) {
                    verboseLog("Received BACKSPACE");
                    error = true;
                    i -= 2;
                } else if (receiveBuffer[i] == NULL_BYTE && useNull) {
                    receivedDataSize = i;
                    verboseLog("Null byte received, total byte count: " + String(receivedDataSize));
                    i = maxBufferSize; // End loop.
                } else if (receiveBuffer[i] == END_OF_TEXT && controlEndBytesToGo == 2) {
                    controlEndBytesToGo = 1;
                } else if (receiveBuffer[i] == END_OF_TRANSMISSION && controlEndBytesToGo == 1) {
                    receivedDataSize = i+1;
                    verboseLog("End control bytes received, total byte count: " + String(receivedDataSize));
                    sendBuffer[maxIndex - 1] = END_OF_TRANSMISSION; // Final echo back.
                    i = maxIndex - 2; // End loop after next byte.
                }
            } else if (checkErrors) {
                // On send, if last sent byte was not echoed correctly, send a BACKSPACE and roll back buffers.
                error = lastSentByte != receiveBuffer[i];
                if (error) {
                    verboseLog(String("Error: exp: ") + String(char(lastSentByte)) + String(", got: ") + String(char(receiveBuffer[i])));
                }
                if (error || receiveBuffer[i] == BACKSPACE) i--;
            }

            lastSentByte = sendBuffer[i];
            if (usDelay > 0) delayMicroseconds(usDelay);
        }
    } else {
        SPI.transferBytes(sendBuffer, receiveBuffer, sizeof(sendBuffer));
    }

    SPI.endTransaction();
    digitalWrite(D8, HIGH);
    digitalWrite(D2, LOW);

    if (errorCount > maxErrorCount) {
        Serial.println("Max error count was reached. Received:");
        Serial.println(reinterpret_cast<const char*>(const_cast<const byte*>(&receiveBuffer[1])));
    } else {
        // Don't print added control bytes.
        if (!useNull) receiveBuffer[receivedDataSize - 2] = NULL_BYTE;

        // Skip first byte as that's just noise.
        Serial.println("Trimmed of noise and control bytes, received:");
        Serial.println(reinterpret_cast<const char*>(const_cast<const byte*>(&receiveBuffer[1])));
    }
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
    // is sending, control byte, clock, use loop, delay for loop, check for errors
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
    commaIndex = body.indexOf(',');

    ulong usDelay = body.substring(0, commaIndex).toInt();
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    bool checkErrors = body.substring(0, commaIndex)[0] == '1';
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    bool leadingNulls = body.substring(0, commaIndex)[0] == '1';
    body = body.substring(commaIndex+1);
    commaIndex = body.indexOf(',');

    bool generateErrors = body[0] == '1';

    transferSPIData(data, isSending, useNull, useLoop, rate, usDelay, checkErrors, leadingNulls, generateErrors);
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
