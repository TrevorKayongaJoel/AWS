#ifndef DATALOGGER_H
#define DATALOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "SensorData.h"
#include "GSM.h"

class DataLogger {
public:
    DataLogger(int csPin);
    void begin();
    
    // Formats data into labeled string and saves to SD
    void logSensorData(String timestamp, SensorData data);
    
    // Reads the last written line and sends to ThingSpeak via GSM
    void uploadLastDataToThingspeak(GSM &gsmModule);

private:
    int _csPin;
    String _fileName;
    String _lastDataString; // Caches the last written line for efficiency

    // Helper to extract value from "Label:Value" string
    String getValueFromLog(String logLine, String label);
    
    // ThingSpeak Config (Update these)
    const String API_KEY_1 = "0UOU523VQPM2FZXJ"; 
    const String API_KEY_2 = "5N37KH8M7FCF5PU2";
    const String API_KEY_3 = "XH83XCG9LMURW45L";
};

#endif