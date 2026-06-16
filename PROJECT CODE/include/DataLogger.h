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
    
    // Formats data into labeled string and saves to BOTH Archive and Queue
    void logSensorData(String timestamp, SensorData data);
    
    // New: Processes the queue, sending oldest data first
    void uploadPendingData(GSM &gsmModule, unsigned long startTimeMs, unsigned long tonLimitMs);

private:
    int _csPin;
    String _fileName;      // Archive file (permanent)
    String _queueFileName; // Queue file (temporary buffer)
    String _lastDataString; 

    // Helper to extract value from "Label:Value" string
    String getValueFromLog(String logLine, String label);

    // New: Removes the first line from the queue file
    bool popQueue();
    
    // ThingSpeak Config
    const String API_KEY_1 = "0UOU523VQPM2FZXJ"; 
    const String API_KEY_2 = "5N37KH8M7FCF5PU2";
    const String API_KEY_3 = "XH83XCG9LMURW45L";
};

#endif