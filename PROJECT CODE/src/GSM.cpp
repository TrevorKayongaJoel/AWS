#include "GSM.h"
#include <SD.h>
// ESP32 WROVER: Use 26/27 or 4/13. DO NOT use 16/17 if using PSRAM.
#define RX_GSM 16 
#define TX_GSM 17

HardwareSerial SerialG = Serial2;

GSM::GSM() {}

void GSM::setupGSM() {
    SerialG.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM); 
    delay(1000);
    Serial.println("Initializing GSM...");

    // Handshake (log responses for debugging)
    bool gsmReady = false;
    int attempts = 0;
    while (!gsmReady && attempts < 10) {
        SerialG.println("AT"); 
        delay(500);
        if (SerialG.available()) {
            String response = SerialG.readString();
            if (response.indexOf("OK") != -1) {
                gsmReady = true;
                Serial.println("GSM Ready.");
            }
        }
        attempts++;
    }
    
    if(gsmReady) {
        connectGPRS();
    } else {
        Serial.println("GSM Failure (Check Wiring/Power).");
    }
}

void GSM::connectGPRS() {
    Serial.println("Configuring GPRS...");
    
    // 1. CLEAN UP START: Close previous connections to stop "ERROR"
    sendCommand("AT+HTTPTERM", 500, true); // Close HTTP if open
    sendCommand("AT+SAPBR=0,1", 500, true); // Close Bearer if open

    // 2. Start Connection
    sendCommand("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", 1000, true);
    sendCommand("AT+SAPBR=3,1,\"APN\",\"internet\"", 1000, true); 
    sendCommand("AT+SAPBR=1,1", 3000, true); // Enable GPRS
    
    // 3. Verify IP
    sendCommand("AT+SAPBR=2,1", 3000, true); 
    
    // 4. Initialize HTTP Service once
    sendCommand("AT+HTTPINIT", 1000, true);  
}

bool GSM::sendThingSpeakRequest(String url) {
    // Terminate any stuck previous sessions just in case
    sendCommand("AT+HTTPTERM", 500, false);
    sendCommand("AT+HTTPINIT", 500, false);

    Serial.println("Uploading: " + url);
    
    // Set URL
    String cmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    sendCommand(cmd, 2000, false);

    // GET Request (Action 0)
    SerialG.println("AT+HTTPACTION=0");
    
    String actionResp = "";
    unsigned long start = millis();
    // Wait for BOTH "OK" (immediate) and then "+HTTPACTION:" (async)
    bool seenOK = false;
    bool seenAction = false;
    
    while (millis() - start < 15000) { // Increase timeout to 15s for slow GPRS
        while (SerialG.available()) {
            char c = SerialG.read();
            actionResp += c;
            Serial.write(c);
        }
        
        if (actionResp.indexOf("OK") != -1) seenOK = true;
        
        // We need the code after HTTPACTION: 0,200,length
        // So we wait until we see a newline after the action string
        if (actionResp.indexOf("+HTTPACTION:") != -1) {
            if (actionResp.endsWith("\n") || actionResp.endsWith("\r")) {
                seenAction = true;
                break;
            }
        }
        delay(1); 
    }
    Serial.println();

    bool success = false;
    if (actionResp.indexOf(",200,") != -1) { // More specific check for 200 OK
        success = true;
        Serial.println("[GSM] Upload Success (200 OK)");
    } else {
        Serial.println("[GSM] Upload Failed or Timed Out");
    }

    // Read Response
    sendCommand("AT+HTTPREAD", 2000, true);
    
    // Close session
    sendCommand("AT+HTTPTERM", 500, false); 

    return success;
}

void GSM::sendCommand(const String& command, int timeout, boolean debug) {
    while(SerialG.available()) SerialG.read(); // Clear buffer
    SerialG.println(command);

    String resp = "";
    unsigned long start = millis();
    while (millis() - start < (unsigned long)timeout) {
        while (SerialG.available()) {
            char c = SerialG.read();
            resp += c;
            if (debug) Serial.write(c);
        }
        // Early exit if we see common terminators
        if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) {
            break; 
        }
        delay(1); // Feed the watchdog
    }
    if (debug) Serial.println();
}
