#include <Arduino.h>
#include "AIR_PRESSURE.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEA_LEVEL_HPA 1013.25

// Initialize the BME280 sensor
Adafruit_BME280 bme;



AirPressure::AirPressure() : pressure_(0.0), temperature_(0.0), humidity_(0.0), initialized_(false) {}

void AirPressure::sensor_setup() {
    Wire.setTimeOut(50); // Set 50ms timeout for I2C to prevent hangs
    
    if (!bme.begin(0x77, &Wire)) {
        Serial.println("Could not find a valid BME280 sensor at 0x77, check wiring!");
        initialized_ = false;
    } else {
        Serial.println("BME280 sensor initialized successfully.");
        initialized_ = true;
    }
}

float AirPressure::readPressure() {
    if (!initialized_) return NAN;
    pressure_ = bme.readPressure() / 100.0F; // Convert Pa to hPa
    delay(50); 
    return pressure_;
}

float AirPressure::readTemperature() {
    if (!initialized_) return NAN;
    temperature_ = bme.readTemperature();
    delay(50); 
    return temperature_;
}

float AirPressure::readHumidity() {
    if (!initialized_) return NAN;
    humidity_ = bme.readHumidity();
    delay(50); 
    return humidity_;
}

float AirPressure::readAltitude(float seaLevelhPa) {
    if (!initialized_) return NAN;
    altitude_ = bme.readAltitude(seaLevelhPa);
    delay(50); 
    return altitude_;
}