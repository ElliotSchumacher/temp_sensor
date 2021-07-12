#include <Arduino.h>
#include "LittleFS.h"
#include "DelayTimer.h"
#include "secret.h"

const uint8_t LED_ON = 0;
const uint8_t LED_OFF = 1;
const uint8_t LED_HB = LED_BUILTIN;

void manageBlink(uint32_t msNow, uint16_t timeOn, uint16_t timeOff);
void readFile(String filePath);

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
    readFile("/config.txt");
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

void readFile(String filePath) {
    File file = LittleFS.open(filePath, "r");
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.println("File Content:");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}