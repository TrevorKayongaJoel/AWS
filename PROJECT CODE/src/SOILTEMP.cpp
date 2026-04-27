#include <Arduino.h>
#include "SOILTEMP.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 33   // GPIO where DS18B20 DATA is connected

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

SoilTemp::SoilTemp() : soil_temperature_(0.0) {}    

void SoilTemp::sensor_setup() {
    // Serial.begin(9600); // REMOVED: Serial already initialized in main.cpp
    // Setup code for the soil temperature sensor
    sensors.begin();
    
}
float SoilTemp::readSoilTemperature() {
     sensors.requestTemperatures();              // Ask sensor to read temperature
    soil_temperature_ = sensors.getTempCByIndex(0);   // Get temperature in °C

    if (soil_temperature_ == DEVICE_DISCONNECTED_C) {
    Serial.println("Error: DS18B20 not found");
    return -127.0; // Return error value
    } else {
    Serial.print("Temperature: ");
    Serial.print(soil_temperature_);
    Serial.println(" °C");
    delay(100); // brief delay to ensure sensor stability
    return soil_temperature_;
    }
}
