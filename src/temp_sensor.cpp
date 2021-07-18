#include <Arduino.h>
#include <OneWire.h>
#include "LittleFS.h"
#include "DelayTimer.h"
#include "secret.h"
#define ONE_WIRE_BUS 5

const uint8_t LED_ON = 0;
const uint8_t LED_OFF = 1;
const uint8_t LED_HB = LED_BUILTIN;
const uint8_t LED_INFO_PIN = 12;
const String CONFIG_PATH = "/config.bin";
const uint8_t GUID_LENGTH = 8;

void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff);
void setupConfigVariables();
void printConfigVariables();
void writeConfigFile();
void printFile(String filePath);
float getTemperature(const byte address[8], boolean isCelsius);

struct Node{
    char guid[GUID_LENGTH];
    uint32_t tempCheckInterval;
    uint32_t errorInterval;
    uint8_t sensorCount;
};

struct Sensor {
    byte address[8];
};

bool crcError = false;

Node nodeData;
Sensor sensors[8];
OneWire oneWire(ONE_WIRE_BUS);
DelayTimer dtBlink;
DelayTimer dtTemp;

void setup() {
    pinMode(LED_HB, OUTPUT);							// Set Request LED as output
    pinMode(LED_INFO_PIN, OUTPUT);
    digitalWrite(LED_HB, LED_ON);						// Turn LED on
    Serial.begin(115200);
    delay(2000);
    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }
    writeConfigFile();
    setupConfigVariables();
    printConfigVariables();
    printFile(CONFIG_PATH);
}

void loop() {
    unsigned long msNow = millis();
    manageBlink(msNow, 100, 1900);

    if (dtTemp.tripped(msNow)) {
        dtTemp.reset(msNow, nodeData.tempCheckInterval);
        float temperatures[nodeData.sensorCount];
        for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount && !crcError; sensorIndex++) {
            temperatures[sensorIndex] = getTemperature(sensors[sensorIndex].address, false);
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
        for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount; sensorIndex++) {
            file.read((byte *)&sensors[sensorIndex], sizeof(Sensor));
        }
        file.close();
    }
}

void printConfigVariables() {
    Serial.println("nodeGuid:");
    for (uint8_t index = 0; index < sizeof(nodeData.guid); index++) {
        Serial.print(nodeData.guid[index]);
    }
    Serial.println();
    Serial.println("tempCheckInterval:");
    Serial.println(nodeData.tempCheckInterval);
    Serial.println("errorInterval:");
    Serial.println(nodeData.errorInterval);
    Serial.println("sensorCount:");
    Serial.println(nodeData.sensorCount);
    for (uint8_t sensorIndex = 0; sensorIndex < nodeData.sensorCount; sensorIndex++) {
        Serial.print("sensorAddress:");
        Serial.print(sensorIndex);
        Serial.println(":");
        for (int index = 0; index < 8; index++) {
            Serial.print(sensors[sensorIndex].address[index], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }
}

void writeConfigFile() {
    const Node nodeDataToSave = {
        {'n', 'o', 'd', 'e', 'G', 'U', 'I', 'D'},
        30500,
        45050,
        2
    };
    const Sensor warmSensor = {
        {0x28, 0xB3, 0x53, 0x07, 0xD6, 0x01, 0x3C, 0x0C}
    };
    const Sensor coolSensor = {
        {0x28, 0x0D, 0x5B, 0x07, 0xD6, 0x01, 0x3C, 0x26}
    };
    File file = LittleFS.open(CONFIG_PATH, "w+");
    if (!file) {
        Serial.println("Failed to open file for writing");
    } else {
        file.write((byte *)&nodeDataToSave, sizeof(nodeDataToSave));
        file.write((byte *)&warmSensor, sizeof(warmSensor));
        file.write((byte *)&coolSensor, sizeof(coolSensor));
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