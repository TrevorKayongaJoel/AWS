#pragma once

// sensor can read preessure, air temp and humidity
// connected via I2C
class AirPressure {
public:
    AirPressure();
    void sensor_setup(); // show sensor information
    float readPressure(); // read air pressure in hPa
    float readTemperature(); // read temperature in Celsius
    float readHumidity(); // read humidity in percentage
    float readAltitude(float seaLevelhPa);
       
private:
    float pressure_; 
    float temperature_;  
    float humidity_;  
    float altitude_;
    bool initialized_;
};