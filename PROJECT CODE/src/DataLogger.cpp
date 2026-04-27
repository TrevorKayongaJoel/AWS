#include "DataLogger.h"

// USE IP INSTEAD OF DOMAIN TO FIX ERROR 601
String THINGSPEAK_IP = "http://184.106.153.149"; 

DataLogger::DataLogger(int csPin) {
    _csPin = csPin;
    _fileName = "/datalog.txt";
    _lastDataString = "";
}

void DataLogger::begin() {
    // Initialize SPI before SD (ensure pins are correct for your board)
    if (!SD.begin(_csPin)) {
        Serial.println("SD Card Mount Failed");
        return;
    }
    Serial.println("SD Card Initialized");
}

void DataLogger::logSensorData(String timestamp, SensorData data) {
    String dataStr = "";
    // Note: Ensure your sensor code handles failures so these aren't all "0"
    dataStr += "Time:" + timestamp + ",";
    dataStr += "Press:" + String(data.airPressure, 2) + ",";
    dataStr += "Alt:" + String(data.altitude, 2) + ",";
    dataStr += "Temp:" + String(data.temperature, 2) + ",";
    dataStr += "Hum:" + String(data.humidity, 2) + ",";
    dataStr += "Light:" + String(data.lightLevel, 2) + ",";
    dataStr += "SoilM:" + String(data.soilMoisture, 2) + ",";
    dataStr += "Rain:" + String(data.rainCount) + ",";
    dataStr += "WSpd:" + String(data.windSpeed, 2) + ",";
    dataStr += "WDir:" + String(data.windDirection) + ",";
    dataStr += "V33:" + String(data.volt_3v3, 2) + ",";
    dataStr += "V5:" + String(data.volt_5v, 2) + ",";
    dataStr += "VBatt:" + String(data.volt_batt, 2) + ",";
    dataStr += "VSol:" + String(data.volt_solar, 2) + ",";
    dataStr += "VDC:" + String(data.volt_dc, 2) + ",";
    dataStr += "CBatt:" + String(data.curr_batt, 2) + ",";
    dataStr += "CSol:" + String(data.curr_solar, 2);

    File file = SD.open(_fileName, FILE_APPEND);
    if (file) {
        file.println(dataStr);
        file.close();
        Serial.println("Logged: " + dataStr);
        _lastDataString = dataStr; 
    } else {
        Serial.println("Error writing to SD");
    }
}

String DataLogger::getValueFromLog(String logLine, String label) {
    String searchKey = label + ":";
    int startIndex = logLine.indexOf(searchKey);
    if (startIndex == -1) return "0"; 
    
    startIndex += searchKey.length();
    int endIndex = logLine.indexOf(",", startIndex);
    if (endIndex == -1) endIndex = logLine.length(); 
    
    return logLine.substring(startIndex, endIndex);
}

void DataLogger::uploadLastDataToThingspeak(GSM &gsmModule) {
    if (_lastDataString == "") {
        Serial.println("No data to upload.");
        return;
    }

    Serial.println("Starting Upload Sequence (Approx 45 seconds)...");

    // CHANNEL 1
    String url1 = THINGSPEAK_IP + "/update?api_key=" + API_KEY_1;
    url1 += "&field1=" + getValueFromLog(_lastDataString, "Temp");
    url1 += "&field2=" + getValueFromLog(_lastDataString, "Hum");
    url1 += "&field3=" + getValueFromLog(_lastDataString, "Press");
    url1 += "&field4=" + getValueFromLog(_lastDataString, "Rain");
    url1 += "&field5=" + getValueFromLog(_lastDataString, "WSpd");
    url1 += "&field6=" + getValueFromLog(_lastDataString, "WDir");
    url1 += "&field7=" + getValueFromLog(_lastDataString, "Light");
    url1 += "&field8=" + getValueFromLog(_lastDataString, "SoilM");

    gsmModule.sendThingSpeakRequest(url1);
    
    Serial.println("Waiting 16s for ThingSpeak Rate Limit...");
    delay(16000); // CRITICAL: ThingSpeak blocks if < 15s

    // CHANNEL 2 (Voltage)
    String url2 = THINGSPEAK_IP + "/update?api_key=" + API_KEY_2;
    url2 += "&field1=" + getValueFromLog(_lastDataString, "V33");
    url2 += "&field2=" + getValueFromLog(_lastDataString, "V5");
    url2 += "&field3=" + getValueFromLog(_lastDataString, "VBatt");
    url2 += "&field4=" + getValueFromLog(_lastDataString, "VSol");
    url2 += "&field5=" + getValueFromLog(_lastDataString, "VDC");
    
    gsmModule.sendThingSpeakRequest(url2);

    Serial.println("Waiting 16s for ThingSpeak Rate Limit...");
    delay(16000); // CRITICAL: ThingSpeak blocks if < 15s

    // CHANNEL 3 (Current)
    String url3 = THINGSPEAK_IP + "/update?api_key=" + API_KEY_3;
    url3 += "&field1=" + getValueFromLog(_lastDataString, "CBatt");
    url3 += "&field2=" + getValueFromLog(_lastDataString, "CSol");

    gsmModule.sendThingSpeakRequest(url3);
    Serial.println("All channels updated.");
}