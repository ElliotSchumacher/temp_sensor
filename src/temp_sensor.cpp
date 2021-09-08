#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <WebSocketsClient.h>
#include "LittleFS.h"
#include "DelayTimer.h"
#include "secret.h"
#define ONE_WIRE_BUS 5

const uint8_t LED_ON = 0;
const uint8_t LED_OFF = 1;
const uint8_t LED_HB = LED_BUILTIN;
const uint8_t LED_INFO_PIN = 12;
const String CONFIG_PATH = "/config.bin";
const String HOST = "192.168.0.14";
const uint16_t PORT = 5001;
const uint16_t ERROR_INTERVAL = 4000;
const uint8_t MAC_ADDRESS_LENGTH = 6;
const uint8_t SENSOR_ADDRESS_LENGTH = 8;
const uint8_t MAX_SENSOR_COUNT = 8;

void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff);
void setupConfigVariables();
void printConfigVariables();
void writeConfigFile();
void printFile(String filePath);
boolean searchForSensors();
float getTemperature(const byte address[8], boolean isCelsius);
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void sendNodeData();

struct Node{
    uint32_t tempCheckInterval;
    uint8_t sensorCount;
};

bool crcError = false;

WebSocketsClient webSocket;
WiFiServer server(80);
String header;
Node nodeData;
byte sensors[MAX_SENSOR_COUNT][SENSOR_ADDRESS_LENGTH];
OneWire oneWire(ONE_WIRE_BUS);
DelayTimer dtBlink;
DelayTimer dtTemp;
DelayTimer dtWSTest;
DelayTimer dtSensorSearch;
byte macAddress[MAC_ADDRESS_LENGTH];

void setup() {
    pinMode(LED_HB, OUTPUT);							// Set Request LED as output
    pinMode(LED_INFO_PIN, OUTPUT);
    digitalWrite(LED_HB, LED_ON);						// Turn LED on
    Serial.begin(115200);
    delay(2000);
    WiFiManager wifiManager;
    // wifiManager.resetSettings();
    bool res = wifiManager.autoConnect("AutoConnectAP");
    if (res) {
        Serial.println("Connected");
    } else {
        Serial.println("Failed to connect");
        ESP.restart();
    }

    webSocket.begin(HOST, PORT, "/ws");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(10000);
    webSocket.enableHeartbeat(15000, 4000, 2);

    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }
    writeConfigFile();
    setupConfigVariables();
    printConfigVariables();
    printFile(CONFIG_PATH);

    dtWSTest.setDelay(10000);
    dtSensorSearch.setDelay(10000);
    WiFi.macAddress(macAddress);
    searchForSensors();
}

void loop() {
    unsigned long msNow = millis();
    manageBlink(msNow, 100, 1900);
    webSocket.loop();

    if (dtTemp.tripped(msNow)) {
        dtTemp.reset(msNow, nodeData.tempCheckInterval);
        float temperatures[nodeData.sensorCount];
        for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount && !crcError; sensorIndex++) {
            temperatures[sensorIndex] = getTemperature(sensors[sensorIndex], false);
        }
        if (!crcError) {
            Serial.println("Temperature:");
            for (uint8_t index = 0; index < nodeData.sensorCount; index++) {
                Serial.print("-----:");
                Serial.print(index);
                Serial.print(": ");
                Serial.println(temperatures[index]);
             }
        }
    }
    if (dtWSTest.tripped(msNow)) {
        String message = "TEST: " + msNow;
        webSocket.sendTXT("{\"type\":\"test\",\"message\":\"test message\"}");
        Serial.println("{\"type\":\"test\",\"message\":\"test message\"}");
        dtWSTest.reset(msNow);
    }

    if (dtSensorSearch.tripped(msNow)) {
        boolean changed = searchForSensors();
        if (changed) {
            sendNodeData();
        }
        
        Serial.print("changed: ");
        Serial.println(changed);
        // Print out sensor addresses
        for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount; sensorIndex++) {
            Serial.print("Sensor[");
            Serial.print(sensorIndex);
            Serial.print("]: ");
            for (uint8_t addressIndex = 0; addressIndex < SENSOR_ADDRESS_LENGTH; addressIndex++) {
                Serial.print(sensors[sensorIndex][addressIndex]);
                Serial.print(" ");
            }
            Serial.println();
        }
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

void setupConfigVariables() {
    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
    } else {
        file.read((byte *)&nodeData, sizeof(nodeData));
        file.close();
    }
}

void printConfigVariables() {
    Serial.println("tempCheckInterval:");
    Serial.println(nodeData.tempCheckInterval);
    Serial.println("sensorCount:");
    Serial.println(nodeData.sensorCount);
}

void writeConfigFile() {
    const Node nodeDataToSave = {
        30500,
        2
    };
    File file = LittleFS.open(CONFIG_PATH, "w+");
    if (!file) {
        Serial.println("Failed to open file for writing");
    } else {
        file.write((byte *)&nodeDataToSave, sizeof(nodeDataToSave));
        file.close();
    }
}

void printFile(String filePath) {
    File file = LittleFS.open(filePath, "r");
    if (!file) {
        Serial.println("Failed to open file for printing");
    } else {
        while (file.available()) {
            Serial.print(file.read(), BIN);
        }
        file.close();
    }
}

// return true if the sensors have changed
boolean searchForSensors() {
    oneWire.reset_search();
    byte newAddresses[MAX_SENSOR_COUNT][SENSOR_ADDRESS_LENGTH];
    byte address[8];
    uint8_t present = nodeData.sensorCount;
    uint8_t added = 0;
    uint8_t newSensorIndex = 0;
    while (oneWire.search(address)) {
        boolean matched = false;
        for (uint8_t index = 0; index < nodeData.sensorCount; index++) {
            if (memcmp(sensors[index], address, sizeof(address)) == 0) {
                matched = true;
            }
        }
        if (matched) {
            present--;
        } else {
            added++;
        }
        memcpy(newAddresses[newSensorIndex], address, sizeof(address));
        newSensorIndex++;
    }
    nodeData.sensorCount = newSensorIndex;
    memcpy(sensors, newAddresses, sizeof(sensors));
    return present != 0 || added != 0;
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

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
		case WStype_DISCONNECTED:
			Serial.printf("[WSc] Disconnected!\n");
			break;
		case WStype_CONNECTED: {
			Serial.printf("[WSc] Connected to url: %s\n", payload);

			// send message to server when Connected
			webSocket.sendTXT("{\"type\":\"connect\"}");
            searchForSensors();
            sendNodeData();
		}
			break;
		case WStype_TEXT:
			Serial.printf("[WSc] get text: %s\n", payload);

			// send message to server
			// webSocket.sendTXT("message here");
			break;
        case WStype_PING:
            // pong will be send automatically
            Serial.printf("[WSc] get ping\n");
            break;
        case WStype_PONG:
            // answer to a ping we send
            Serial.printf("[WSc] get pong\n");
            break;
    }
}

void sendNodeData() {
    StaticJsonDocument<1536> doc;

    doc["type"] = "data";
    JsonArray mac = doc.createNestedArray("mac");
    for (uint8_t index = 0; index < MAC_ADDRESS_LENGTH; index++) {
        mac.add(macAddress[index]);
    }

    JsonArray sensorList = doc.createNestedArray("sensors");
    for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount; sensorIndex++) {
        JsonArray sensor = sensorList.createNestedArray();
        for (uint8_t addressIndex = 0; addressIndex < SENSOR_ADDRESS_LENGTH; addressIndex++) {
            sensor.add(sensors[sensorIndex][addressIndex]);
        }
    }

    // Serial.print("Mac Address: ");
    // Serial.println(macAddress[0]);
    Serial.print("JSON DATA: ");
    serializeJson(doc, Serial);
    Serial.println();
    String serializedJson;
    serializeJson(doc, serializedJson);

    webSocket.sendTXT(serializedJson);

}
