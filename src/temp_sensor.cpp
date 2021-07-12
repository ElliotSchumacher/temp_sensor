#include <Arduino.h>
#include "LittleFS.h"
#include "DelayTimer.h"
#include "secret.h"

const uint8_t LED_ON = 0;
const uint8_t LED_OFF = 1;
const uint8_t LED_HB = LED_BUILTIN;
const String CONFIG_PATH = "/config.bin";
const uint8_t GUID_LENGTH = 8;

void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff);
void setupConfigVariables();
void printConfigVariables();
void writeConfigFile();
void printFile(String filePath);

struct Node{
    char guid[GUID_LENGTH];
    uint32_t tempCheckInterval;
    uint32_t errorInterval;
    uint8_t sensorCount;
};

struct Sensor {
    byte address[8];
};

Node nodeData;
Sensor sensors[8];
DelayTimer dtBlink;

void setup() {
    pinMode(LED_HB, OUTPUT);							// Set Request LED as output
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