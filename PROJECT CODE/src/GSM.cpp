#include "GSM.h"

// ESP32 WROVER: Use 26/27 or 4/13. DO NOT use 16/17 if using PSRAM.
#define RX_GSM 16 
#define TX_GSM 17

HardwareSerial SerialG = Serial2;

GSM::GSM() {}

void GSM::setupGSM() {
    SerialG.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM); 
    delay(1000);
    Serial.println("Initializing GSM...");

    // Handshake
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

void GSM::sendThingSpeakRequest(String url) {
    // Terminate any stuck previous sessions just in case
    sendCommand("AT+HTTPTERM", 500, false);
    sendCommand("AT+HTTPINIT", 500, false);

    Serial.println("Uploading: " + url);
    
    // Set URL
    String cmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    sendCommand(cmd, 2000, false);

    // GET Request (Action 0)
    // We wait longer (8000ms) because network can be slow
    sendCommand("AT+HTTPACTION=0", 8000, true);

    // Read Response to see if it worked
    sendCommand("AT+HTTPREAD", 2000, true);
    
    // Close this specific HTTP session to prevent memory leaks in modem
    sendCommand("AT+HTTPTERM", 500, false); 
}

void GSM::sendCommand(const String& command, int timeout, boolean debug) {
    while(SerialG.available()) SerialG.read(); // Clear buffer
    SerialG.println(command);
    
    long int time = millis();
    while((time + timeout) > millis()) {
        while(SerialG.available()) {
            char c = SerialG.read();
            if(debug) Serial.write(c);
        }
    }
    if(debug) Serial.println();
}