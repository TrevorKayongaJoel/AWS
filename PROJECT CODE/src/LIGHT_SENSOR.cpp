#include <Arduino.h>
#include "LIGHT_SENSOR.h"
#define ANALOG_PIN 35  // Example analog pin for light sensor

LightSensor::LightSensor() : _pin(ANALOG_PIN) {}

float LightSensor::readLightLevel() {
    // Serial.begin(9600); // REMOVED: Serial already initialized in main.cpp
    int adc = analogRead(_pin);
    light_voltage_ = (adc/4095.0) * 3.3; // Convert ADC value to voltage
    delay(100); // brief delay to ensure sensor stability
    return light_voltage_;
}

void LightSensor::setupSensor() {
    pinMode(_pin, INPUT);
}