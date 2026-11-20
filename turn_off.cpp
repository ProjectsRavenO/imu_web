#include <Arduino.h>
#include <LSM6DS3.h> // Include the LSM6DS3 library
#include <MadgwickAHRS.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_LittleFS.h>
#include <CRC32.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
// sleeping pins
#include <nrf_gpio.h>
#include <nrf_power.h>

const int pins[] = {
    D0, D1, D2, D3, D4, D5,
    D6, D7, D8, D9, D10
};

void setup() {
    Serial.begin(115200);
    delay(2000);

    for (int i = 0; i < 11; i++) {
        pinMode(pins[i], INPUT_PULLUP);
    }

    Serial.println("Pin scanner started");
}

void loop() {
    for (int i = 0; i < 11; i++) {
        if (digitalRead(pins[i]) == LOW) {
            Serial.print("LOW detected on D");
            Serial.println(i);
        }
    }

    delay(200);
}