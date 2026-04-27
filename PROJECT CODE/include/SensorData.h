#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

struct SensorData {
    float airPressure = 0.0;
    float altitude = 0.0;
    float temperature = 0.0; // From DHT or BME
    float humidity = 0.0;
    float lightLevel = 0.0;
    float soilMoisture = 0.0;
    float soilTemp = 0.0; // Assuming you have this
    int rainCount = 0;
    float windSpeed = 0.0;
    int windDirection = 0;
    
    // Power Monitoring (5 rails + Battery + Solar + Currents)
    float volt_3v3 = 0.0;
    float volt_5v = 0.0;
    float volt_batt = 0.0;
    float volt_solar = 0.0;
    float volt_dc = 0.0;
    float curr_batt = 0.0; // Battery current
    float curr_solar = 0.0; // Solar current
};

#endif