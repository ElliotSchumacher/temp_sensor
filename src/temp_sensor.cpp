#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "DelayTimer.h"
#include "secret.h"

#define ONE_WIRE_BUS 5

const uint16_t CRC_ERROR_CODE = 1000;
const String CRC_ERROR_MSG = "CRC problem with temperature sensors";
const String CONNECTED_MSG = "Arduino device connected";
const uint16_t TEMP_CHECK_INTERVAL = 60000;
const uint32_t TEMP_WARNING_INTERVAL = 3600000; // 1 hour
const uint32_t LOG_INTERVAL = 21600000; // 6 hours 

const uint8_t LED_OFF = 1;
const uint8_t LED_ON = 0;
const uint8_t LED_HB = LED_BUILTIN;
const float UPPER_BOUND_WARM = 110;
const float LOWER_BOUND_WARM = 70;

const String IFTTT_URL = "http://maker.ifttt.com/trigger/";
const String IFTTT_KEY = "/with/key/jtVyseeuuPpAePoO0VdfHahhA6xwZHvaeNGDzfsUsmt";

const byte COOL_ADDRESS[] = {0x28, 0x0D, 0x5B, 0x07, 0xD6, 0x01, 0x3C, 0x26};
const byte WARM_ADDRESS[] = {0x28, 0xB3, 0x53, 0x07, 0xD6, 0x01, 0x3C, 0x0C}; // RED

float getTemperature(const byte address[8], boolean isCelsius);
uint8_t notifierCall(String message);
uint8_t logCall(float temp1, float temp2);
String tempMessage(float temp, String position);

WiFiClient client;
HTTPClient http;
OneWire oneWire(ONE_WIRE_BUS);
DelayTimer dtBlink;
DelayTimer dtTemp;
DelayTimer dtWarning;
DelayTimer dtLog;
uint8_t errnum = 0;
uint8_t state = 0;

void setup() {
    pinMode(LED_HB, OUTPUT);							// Set Request LED as output
    digitalWrite(LED_HB, LED_ON);						// Turn LED on
    Serial.begin(115200);

    WiFi.begin(WEB_SSID, WEB_PASSWORD);
	// Wait for connection
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
        digitalWrite(LED_HB, !digitalRead(LED_HB));
        Serial.print(".");
	}
    digitalWrite(LED_HB, LED_ON);
    Serial.println("Connected");
    errnum = notifierCall(CONNECTED_MSG);
    Serial.println(errnum);
}

void loop() {
    unsigned long msNow = millis();
    float warmTemp;
    float coolTemp;

	if(errnum) {
		if(dtBlink.tripped(msNow)) {
            state = !state;
            digitalWrite(LED_HB, state);
			dtBlink.reset(msNow, 200);
		}
		return;
	}

    if(dtBlink.tripped(msNow)) {
        if(digitalRead(LED_HB) == 0) {
          digitalWrite(LED_HB, LED_OFF);
          dtBlink.reset(msNow, 900);
        } else {
          digitalWrite(LED_HB, LED_ON);
          dtBlink.reset(msNow, 100);
        }
    }

    boolean time2ValidateTemp = dtTemp.tripped(msNow) && dtWarning.tripped(msNow);
    boolean time2Log = dtLog.tripped(msNow);
    if(time2ValidateTemp || time2Log) {
        warmTemp = getTemperature(WARM_ADDRESS, false);
        coolTemp = getTemperature(COOL_ADDRESS, false);
        Serial.println("Temperature:");
        Serial.print("----Warm: ");
        Serial.println(warmTemp);
        Serial.print("----Cool: ");
        Serial.println(coolTemp);
        if(time2ValidateTemp) {
            if (warmTemp < LOWER_BOUND_WARM || UPPER_BOUND_WARM < warmTemp) {
                String message = tempMessage(warmTemp, "warm");
                errnum = notifierCall(message);
                errnum = logCall(warmTemp, coolTemp);
                dtWarning.reset(msNow, TEMP_WARNING_INTERVAL);
            }
            if (coolTemp < LOWER_BOUND_WARM || UPPER_BOUND_WARM < coolTemp) {
                String message = tempMessage(coolTemp, "cool");
                errnum = notifierCall(message);
                errnum = logCall(warmTemp, coolTemp);
                dtWarning.reset(msNow, TEMP_WARNING_INTERVAL);
            }
        }
        if(time2Log) {
            errnum = logCall(warmTemp, coolTemp);
            dtLog.reset(msNow, LOG_INTERVAL);
        }
        dtTemp.reset(msNow, TEMP_CHECK_INTERVAL);
    }
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
        return CRC_ERROR_CODE;
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

uint8_t notifierCall(String message) {
    String url = IFTTT_URL + "notify" + IFTTT_KEY;
    String body = "value1=" + message;
    Serial.println(url);
    Serial.println(body);

    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpCode = http.POST(body);
        Serial.println(httpCode);
        if (httpCode <= 0) { //Check the returning code
            return 2;
        }
        http.end();   //Close connection
    } else {
        return 1;
    }
    return 0;
}

uint8_t logCall(float temp1, float temp2) {
    String url = IFTTT_URL + "temp_log" + IFTTT_KEY;
    String body = "value1=" + String(temp1) + "&value2=" + String(temp2);
    Serial.println(url);
    Serial.println(body);

    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpCode = http.POST(body);
        Serial.println(httpCode);
        if (httpCode <= 0) { //Check the returning code
            return 2;
        }
        http.end();   //Close connection
    } else {
        return 1;
    }
    return 0;
}

String tempMessage(float temp, String position) {
    // "The temperature on the WARM side is too HOT (72.3 F)"
    String adjective;
    if (temp < LOWER_BOUND_WARM) {
        adjective = "cold";
    } else {
        adjective = "hot";
    }
    String message = position + "side is too " + adjective + "(" + temp + ")";
    Serial.println(message);
    return message;
}