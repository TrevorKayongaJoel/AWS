#include <Arduino.h>
#include <Wire.h>
#include "WIFI_CONNECTION.h" // Even if unused, keeping your include
#include "DHT22.h"
#include "AIR_PRESSURE.h"
#include "LIGHT_SENSOR.h"
#include "SOILMOISTURE.h"
#include "RTC.h"
#include "GSM.h"
#include "DAVIS.h"
#include "WINDSPEED.h"
#include "WIND_DIRECTION.h"
#include "POWER_MONITORING.h"
#include "DataLogger.h"   // The new file handler class
#include "SensorData.h"   // The data struct

// Objects
// (Assumed your sensor constructors are correct as per your snippet)
DHTSensor dhtsensor;
AirPressure airpressure;
LightSensor lightsensor;
Soilmoisture soilmoisture;
Rtc rtc1;
Davis davisrain;
WindSpeedSensor windspeedsensor;
WindDirectionSensor winddirectionsensor;
PowerMonitoring powermonitoring;

GSM simmodule;
DataLogger dataLogger(4); // CS pin 4 for SD card

// Timer variables
unsigned long lastUploadTime = 0;
const long uploadInterval = 15000; // 15 seconds

void setup() {
    Serial.begin(9600);
    
    // 1. Initialize RTC
    rtc1.setupRTC();
    // (Optional: sync NTP if Wifi available, code omitted for brevity)

    // 2. Initialize Sensors
    // Note: Add logic inside your specific sensor setup() functions 
    // to handle missing hardware gracefully, or we handle it in loop.
    dhtsensor.getsensor(); // Assuming this is setup
    airpressure.sensor_setup();
    davisrain.setupRainGauge();
    windspeedsensor.setupSensor();
    winddirectionsensor.setupSensor();
    lightsensor.setupSensor();
    soilmoisture.setupSensor();
    powermonitoring.begin(21, 22);

    // 3. Initialize SD and GSM
    dataLogger.begin();
    simmodule.setupGSM(); // Includes GPRS setup
}

void loop() {
    SensorData currentData;

    // --- READ SENSORS (With Error Checking) ---

    // 1. Air Pressure / BME280
    // If your library returns NAN or 0 on failure, we sanitize it here.
    float p = airpressure.readPressure();
    currentData.airPressure = isnan(p) ? 0.0 : p;
    
    float alt = airpressure.readAltitude(1013.25);
    currentData.altitude = isnan(alt) ? 0.0 : alt;

    float t = airpressure.readTemperature(); // Using BME temp
    currentData.temperature = isnan(t) ? 0.0 : t;
    
    float h = airpressure.readHumidity();
    currentData.humidity = isnan(h) ? 0.0 : h;

    // 2. Power Monitoring
    if (powermonitoring.readData()) {
        VoltageData v = powermonitoring.getData();
        currentData.volt_3v3 = v.v1;
        currentData.volt_5v = v.v2;
        currentData.volt_batt = v.v3;
        currentData.volt_solar = v.v4;
        currentData.volt_dc = v.v5;
        currentData.curr_batt = v.v6;
        currentData.curr_solar = v.v7;
    } else {
        // Init to 0 if fails
        currentData.volt_3v3 = 0.0;
        currentData.volt_5v = 0.0;
        currentData.volt_batt = 0.0;
        currentData.volt_solar = 0.0;
        currentData.volt_dc = 0.0;
        currentData.curr_batt = 0.0;
        currentData.curr_solar = 0.0;
    }

    // 3. Other Sensors (Add basic checks if your libraries support it)
    currentData.lightLevel = lightsensor.readLightLevel(); // Ensure library returns 0 on fail
    currentData.soilMoisture = soilmoisture.readSoilMoisture();
    currentData.rainCount = davisrain.readRainGauge();
    currentData.windSpeed = windspeedsensor.readWindSpeedKPH();
    currentData.windDirection = winddirectionsensor.readWindDirectionDeg();

    // --- LOGGING ---

    // Get current time string
    String timeStr = String(rtc1.getDateTime().c_str());

    // Save to SD (DataLogger class handles formatting and error checking)
    dataLogger.logSensorData(timeStr, currentData);

    // --- UPLOAD ---
    
    if (millis() - lastUploadTime >= uploadInterval) {
        lastUploadTime = millis();
        
        Serial.println("Triggering Upload Sequence...");
        
        // This function retrieves the last logged line, 
        // deconstructs it, and sends it to the GSM module
        dataLogger.uploadLastDataToThingspeak(simmodule);
    }
    
    delay(1000); // 1 sec loop delay
}