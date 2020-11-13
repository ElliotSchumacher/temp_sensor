#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <OneWire.h>
// #include <DallasTemperature.h>
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
const uint32_t LOG_INTERVAL = 3600000; // 1 hours 

const uint8_t LED_OFF = 1;
const uint8_t LED_ON = 0;
const uint8_t LED_HB = LED_BUILTIN;
const float UPPER_BOUND_WARM = 100;
const float LOWER_BOUND_WARM = 55;
const float UPPER_BOUND_COOL = 100;
const float LOWER_BOUND_COOL = 55;

const String IFTTT_URL = "http://maker.ifttt.com/trigger/";

const byte COOL_ADDRESS[] = {0x28, 0x0D, 0x5B, 0x07, 0xD6, 0x01, 0x3C, 0x26};
const byte WARM_ADDRESS[] = {0x28, 0xB3, 0x53, 0x07, 0xD6, 0x01, 0x3C, 0x0C}; // RED

float getTemperature(const byte address[8], boolean isCelsius);
uint8_t notifierCall(String message);
uint8_t logCall(float temp1, float temp2);
uint8_t pingListener();
String tempMessage(float temp, String position);

WiFiClient client;
HTTPClient http;
OneWire oneWire(ONE_WIRE_BUS);
DelayTimer dtBlink;
DelayTimer dtTemp;
DelayTimer dtWarningWarm;
DelayTimer dtWarningCool;
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
    errnum = notifierCall(CONNECTED_MSG); // Send connected IFTTT message
    Serial.println(errnum);
    delay(2000);
    uint8_t temp = pingListener(); // Send a notification to the listener server
    Serial.println(temp);
    Serial.println("Pinged Listener");
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

    boolean time2Warn = dtWarningWarm.tripped(msNow) || dtWarningCool.tripped(msNow);
    boolean time2ValidateTemp = dtTemp.tripped(msNow) && time2Warn;
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
            boolean extremeTempWarm = warmTemp < LOWER_BOUND_WARM || UPPER_BOUND_WARM < warmTemp;
            boolean extremeTempCool = coolTemp < LOWER_BOUND_COOL || UPPER_BOUND_COOL < coolTemp;
            if (extremeTempWarm || extremeTempCool) {
                String message;
                if (extremeTempWarm) {
                    message = tempMessage(warmTemp, "warm");
                    dtWarningWarm.reset(msNow, TEMP_WARNING_INTERVAL);
                }
                if (extremeTempCool) {
                    String message = tempMessage(coolTemp, "cool");
                    dtWarningCool.reset(msNow, TEMP_WARNING_INTERVAL);
                }
                errnum = notifierCall(message);
                errnum = logCall(warmTemp, coolTemp);
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
    Serial.print("|");
    Serial.print(url);
    Serial.println("|");
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
    Serial.print(url);
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

uint8_t pingListener() {
    String url = "http://192.168.0.14:8000/temp_sensors";
    // String url = "http://192.168.0.14";
    String body = "key=" + String(KEY);
    Serial.println(url);
    Serial.println(body);

    if (http.begin(client, "192.168.0.14", 8000, "/temp_sensors")) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpCode = http.POST(body);
        Serial.println(httpCode);
        if (httpCode <= 0) { //Check the returning code
            Serial.println("Bad httpCode");
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
    float upperBound;
    float lowerBound;
    if (position.equals("warm")) {
        upperBound = UPPER_BOUND_WARM;
        lowerBound = LOWER_BOUND_WARM;
    } else if(position.equals("cool")) {
        upperBound = UPPER_BOUND_COOL;
        lowerBound = LOWER_BOUND_COOL;
    }
    if (temp < lowerBound) {
        adjective = "cold";
    } else if (upperBound < temp) {
        adjective = "hot";
    }
    String message = position + "side is too " + adjective + "(" + temp + ")";
    Serial.println(message);
    return message;
}