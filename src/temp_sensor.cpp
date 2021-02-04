#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "DelayTimer.h"
#include "secret.h"
#define ONE_WIRE_BUS 5

// Local Testing Constants
// const String LOCAL_HOST = "192.168.0.14";
const String LOCAL_HOST = "10.0.0.16";
const uint16_t LOCAL_PORT = 5000;

const uint8_t LED_ON = 0;
const uint8_t LED_OFF = 1;
const uint8_t LED_HB = LED_BUILTIN;
const String HEROKU_HOST = "frozen-reef-06019.herokuapp.com";
const uint16_t HEROKU_PORT = 80;

const byte COOL_ADDRESS[] = {0x28, 0x0D, 0x5B, 0x07, 0xD6, 0x01, 0x3C, 0x26};
const byte WARM_ADDRESS[] = {0x28, 0xB3, 0x53, 0x07, 0xD6, 0x01, 0x3C, 0x0C}; // RED

uint8_t sendHTTPRequest(String host, uint16_t port, String uri, String body, bool isPost);
float getTemperature(const byte address[8], boolean isCelsius);
void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff);

uint32_t tempCheckInterval = 15000;
uint32_t errorInterval = 15000;
bool crcError = false;
bool webError = false;

WiFiClient client;
OneWire oneWire(ONE_WIRE_BUS);
DelayTimer dtBlink;
DelayTimer dtError;
DelayTimer dtTemp;

void setup() {
    pinMode(LED_HB, OUTPUT);							// Set Request LED as output
    digitalWrite(LED_HB, LED_ON);						// Turn LED on
    Serial.begin(115200);
    WiFi.begin(WEB_SSID, WEB_PASSWORD);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
        digitalWrite(LED_HB, !digitalRead(LED_HB));
        Serial.print(".");
	}
    digitalWrite(LED_HB, LED_ON);
    Serial.println("Connected");
    webError = sendHTTPRequest(LOCAL_HOST, LOCAL_PORT, "/connect", "", false);
}

void loop() {
    unsigned long msNow = millis();

    if (crcError) {
        manageBlink(msNow, 1900, 100);
        if (dtError.tripped(msNow)) {
            String body = "errorType=CRC";
            webError = sendHTTPRequest(LOCAL_HOST, LOCAL_PORT, "/error", body, true);
            if (!webError) {
                dtError.reset(msNow, errorInterval);
            }
        }
    } else if (webError) {
        manageBlink(msNow, 3900, 100);
    } else  {
        manageBlink(msNow, 100, 900);
    }

    if(dtTemp.tripped(msNow)) {
        dtTemp.reset(msNow, tempCheckInterval);
        float warmTemp = getTemperature(WARM_ADDRESS, false);
        if (crcError) {
            return;
        }
        float coolTemp = getTemperature(COOL_ADDRESS, false);
        if (!crcError) {
            Serial.println("Temperature:");
            Serial.print("----Warm: ");
            Serial.println(warmTemp);
            Serial.print("----Cool: ");
            Serial.println(coolTemp);
            String body = "warmTemp=" + String(warmTemp) + "&coolTemp=" + String(coolTemp);
            webError = sendHTTPRequest(LOCAL_HOST, LOCAL_PORT, "/temperature", body, true);
            if (webError) {
                dtTemp.setDelay(0);
            }
        }
    }
}

uint8_t sendHTTPRequest(String host, uint16_t port, String uri, String body, bool isPost) {
    Serial.println();
    Serial.println("host: " + host);
    Serial.println("port: " + String(port));
    Serial.println("uri: " + uri);
    Serial.println("body: " + body);
    Serial.print("isPost: ");
    Serial.println(isPost);
    HTTPClient http;
    uint8_t errnum = 0;
    if (http.begin(client, host, port, uri)) {
        int httpCode;
        if (isPost) {
            http.addHeader("Content-Type", "application/x-www-form-urlencoded");
            httpCode = http.POST(body);
        } else {
            httpCode = http.GET();
        }
        Serial.println("httpCode: " + String(httpCode));
        String payload = http.getString();
        StaticJsonDocument<75> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            errnum = 3;
        } else {
            tempCheckInterval = doc["tempCheckInterval"];
            errorInterval = doc["errorInterval"];
            Serial.println("tempCheckInterval: " + String(tempCheckInterval));
            Serial.println("errorInterval: " + String(errorInterval));
            http.end();   //Close connection
            if (httpCode <= 0) { //Check the returning code
                errnum = 2;
            }
        }
    } else {
        errnum = 1;
    }
    Serial.println("errnum: " + String(errnum));
    return errnum > 0;
}

float getTemperature(const byte address[8], boolean isCelsius) {
    byte data[12];

    oneWire.reset();
    oneWire.select(address);
    oneWire.write(0x44, 1);        // start conversion, with parasite power on at the end
    delay(1000);     // maybe 750ms is enough, maybe not
    oneWire.reset();
    oneWire.select(address);    
    oneWire.write(0xBE);         // Read Scratchpad

    // Serial.print("  Data = ");
    for (byte i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = oneWire.read();
        // Serial.print(data[i], HEX);
        // Serial.print(" ");
    }

    byte crcData = OneWire::crc8( data, 8);
    if (crcData != data[8]) {
        Serial.println("CRC is not valid!");
        crcError = true;
        return NULL;
    }
    if (crcError) {
        crcError = false;
    }
    int16_t raw = (data[1] << 8) | data[0];
    float celsius = (float)raw / 16.0;
    if(!isCelsius) {
        float fahrenheit = celsius * 1.8 + 32.0;
        return fahrenheit;
    } else {
        return celsius;
    }
}

void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff) {
    if (dtBlink.tripped(msNow)) {
        if(digitalRead(LED_HB) == 0) {
            digitalWrite(LED_HB, LED_OFF);
            dtBlink.reset(msNow, timeOff);
        } else {
            digitalWrite(LED_HB, LED_ON);
            dtBlink.reset(msNow, timeOn);
        }
    }   
}