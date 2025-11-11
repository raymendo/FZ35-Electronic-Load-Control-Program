#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <time.h> // NEW: for NTP time

// Ensure GRAPH_POINTS and graph-related declarations are available before WebUI includes
#include "FZ35_Graph.h"
#include "FZ35_Battery.h"
#include "FZ35_WebUI.h"
#include "FZ35_Comm.h"
#include "FZ35_WiFi.h"
#include "FZ35_TestLog.h"

#define RX_PIN 15
#define TX_PIN 13

SoftwareSerial fzSerial(RX_PIN, TX_PIN);
AsyncWebServer server(80);
DNSServer dns;

String voltage="0", current="0", power="0", capacityAh="0", energyWh="0", status="UNKNOWN";
String OVP = "", OCP = "", OPP = "", LVP = "", OAH = "", OHP = "";
// NEW: recommended test load current (formatted x.xx)
String TEST_LOAD = "";

// Device command strings for enabling/disabling the load.
// Replace these placeholder strings with the exact commands from the PDF manual.
String LOAD_ENABLE_CMD  = "on";   // <<-- set exact command from PDF
String LOAD_DISABLE_CMD = "off";  // <<-- set exact command from PDF

// define buffers once here using GRAPH_POINTS from FZ35_Graph.h
float voltageBuffer[GRAPH_POINTS];
float currentBuffer[GRAPH_POINTS];
float powerBuffer[GRAPH_POINTS];

// remove static large arrays and use heap-allocated buffers
uint16_t *voltageBufScaled = nullptr;
uint16_t *currentBufScaled = nullptr;
uint16_t *powerBufScaled   = nullptr;
// new: timestamps (seconds since boot) for each sample
uint32_t *timestampBuf     = nullptr;
int graphIndex = 0;

// new: how many samples we actually have (0..GRAPH_POINTS)
int samplesStored = 0;

unsigned long lastRead = 0;
// sample interval (ms) — set to fastest practical (matches sendCommand timeout)
const unsigned long readInterval = 1000;

// new: track if we're applying battery profile (don't read during apply)
unsigned long lastBatteryApply = 0;

// NEW: track if test is running
bool testInProgress = false;
unsigned long testStartTime = 0;
String currentTestBattery = "";

// NEW: NTP configuration
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define GMT_OFFSET_SEC 0           // Adjust for your timezone (e.g., -18000 for EST, 3600 for CET)
#define DAYLIGHT_OFFSET_SEC 3600      // Adjust for DST if needed

/**
 * @file FZ35_Lab.ino
 * @brief Main entry: initializes serial, WiFi captive portal, web UI, NTP time,
 *        allocates sample buffers, handles periodic read + graph update,
 *        manages automatic test start/finish and logging.
 */

void setup() {
    Serial.begin(115200);
    fzSerial.begin(9600);
    delay(2000);
    Serial.println("\n[XY-FZ35 Lab] Starting...");

    // allocate scaled buffers on the heap
    size_t points = GRAPH_POINTS;
    voltageBufScaled = (uint16_t*)calloc(points, sizeof(uint16_t));
    currentBufScaled = (uint16_t*)calloc(points, sizeof(uint16_t));
    powerBufScaled   = (uint16_t*)calloc(points, sizeof(uint16_t));
    timestampBuf     = (uint32_t*)calloc(points, sizeof(uint32_t));

    if (!voltageBufScaled || !currentBufScaled || !powerBufScaled || !timestampBuf) {
        Serial.println("ERROR: buffer allocation failed. Reduce GRAPH_POINTS.");
        while(true) { delay(1000); } // halt for debug
    }

    // WiFi + Web Server
    setupWiFi(server, dns);
    
    // NEW: Configure and sync NTP time
    Serial.println("Configuring time via NTP...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    
    // Wait for time to be set (up to 10 seconds)
    int ntpRetries = 0;
    while (time(nullptr) < 100000 && ntpRetries < 20) {
        Serial.print(".");
        delay(500);
        ntpRetries++;
    }
    Serial.println();
    
    time_t now = time(nullptr);
    if (now > 100000) {
        Serial.println("Time synchronized successfully!");
        Serial.println(ctime(&now));
    } else {
        Serial.println("Warning: NTP sync failed, timestamps will be incorrect");
    }
    
    setupWebServer(server);

    // NEW: initialize test log after time sync
    initTestLog();
    
    graphIndex = 0;
    
    // No auto-start here; user must either:
    // 1. Select a battery profile (which sends start), or
    // 2. Manually click "Start Measure" button in web UI
}

/**
 * @brief Capture latest sample, translate into scaled buffers (for graph / JSON).
 */
void updateGraphBuffersScaled(float v, float i, float p) {
    if (!voltageBufScaled) return;
    uint16_t vs = (uint16_t)constrain((int)roundf(v * 100.0f), 0, 65535);
    uint16_t cs = (uint16_t)constrain((int)roundf(i * 100.0f), 0, 65535);
    uint16_t ps = (uint16_t)constrain((int)roundf(p * 10.0f), 0, 65535);
    voltageBufScaled[graphIndex] = vs;
    currentBufScaled[graphIndex] = cs;
    powerBufScaled[graphIndex]   = ps;
    timestampBuf[graphIndex]     = (uint32_t)(millis() / 1000UL); // seconds

    // increment stored count up to GRAPH_POINTS
    if (samplesStored < GRAPH_POINTS) samplesStored++;

    graphIndex = (graphIndex + 1) % GRAPH_POINTS;
}

// accessors required by WebUI
float scaledVoltageAt(int idx) {
    if (!voltageBufScaled) return 0.0f;
    return voltageBufScaled[idx] / 100.0f;
}
float scaledCurrentAt(int idx) {
    if (!currentBufScaled) return 0.0f;
    return currentBufScaled[idx] / 100.0f;
}
float scaledPowerAt(int idx) {
    if (!powerBufScaled) return 0.0f;
    return powerBufScaled[idx] / 10.0f;
}
uint32_t sampleTimestampAt(int idx) {
    if (!timestampBuf) return 0;
    return timestampBuf[idx];
}

/**
 * @brief Device parse callback. Extracts protection values and live CSV measurement line.
 */
void parseFZ35(const String &lineIn) {
    String s = lineIn;
    s.trim();
    if (s.length() == 0) return;

    Serial.printf("Parsing line: \"%s\"\n", s.c_str());
    bool parsedSummary = false;
    bool parsedCSV = false;

    auto extractNumber = [](const String &tok)->String {
        String out;
        for (size_t i=0;i<tok.length();++i) {
            char c = tok.charAt(i);
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' ) out += c;
        }
        out.trim();
        return out;
    };

    // summary tokens (OVP:, OCP:, OPP:, LVP:, OAH:, OHP:)
    if (s.indexOf("OVP:") >= 0 || s.indexOf("OCP:") >= 0 || s.indexOf("OPP:") >= 0) {
        int pos = 0;
        while (pos < s.length()) {
            int comma = s.indexOf(',', pos);
            if (comma < 0) comma = s.length();
            String token = s.substring(pos, comma);
            token.trim();
            int colon = token.indexOf(':');
            if (colon > 0) {
                String key = token.substring(0, colon);
                String val = token.substring(colon + 1);
                key.trim(); val.trim();
                for (size_t k=0;k<key.length();++k) key.setCharAt(k, toupper(key.charAt(k)));
                if (key == "OVP") OVP = val;
                else if (key == "OCP") OCP = val;
                else if (key == "OPP") OPP = val;
                else if (key == "LVP") LVP = val;
                else if (key == "OAH") OAH = val;
                else if (key == "OHP") OHP = val;
            }
            pos = comma + 1;
        }
        parsedSummary = true;
        Serial.println("Parsed summary parameters.");
    }

    // CSV measurement: look for V, A and Ah tokens (keep previous values if not present)
    if ((s.indexOf('V') >= 0) && (s.indexOf('A') >= 0) && (s.indexOf("Ah") >= 0)) {
        int pos = 0;
        while (pos < s.length()) {
            int comma = s.indexOf(',', pos);
            if (comma < 0) comma = s.length();
            String tok = s.substring(pos, comma);
            tok.trim();
            if (tok.length() > 0) {
                String lowerTok = tok;
                lowerTok.toLowerCase();
                if (lowerTok.endsWith("ah")) {
                    String num = extractNumber(tok);
                    if (num.length()) capacityAh = num;
                } else if (lowerTok.endsWith("v")) {
                    String num = extractNumber(tok);
                    if (num.length()) voltage = num;
                } else if (lowerTok.endsWith("a")) {
                    // exclude 'ah' case (already handled)
                    if (! (lowerTok.endsWith("ah")) ) {
                        String num = extractNumber(tok);
                        if (num.length()) current = num;
                    }
                } else {
                    // fallback: treat as time string
                    energyWh = tok;
                }
            }
            pos = comma + 1;
        }
        // compute power from latest numeric values
        float vf = voltage.toFloat();
        float cf = current.toFloat();
        power = String(vf * cf, 2);
        parsedCSV = true;
        Serial.println("Parsed CSV measurement.");
    }

    if (!parsedSummary && !parsedCSV) {
        Serial.println("Line not recognized, ignored.");
    }

    // status remains whatever logic sets it elsewhere; don't zero fields here
    Serial.printf("== Parsed Data ==\nOVP=%s OCP=%s OPP=%s LVP=%s OAH=%s OHP=%s\n",
                  OVP.c_str(), OCP.c_str(), OPP.c_str(), LVP.c_str(), OAH.c_str(), OHP.c_str());
    Serial.printf("meas V=%s I=%s Ah=%s T=%s P=%s STATUS=%s\n",
                  voltage.c_str(), current.c_str(), capacityAh.c_str(), energyWh.c_str(), power.c_str(), status.c_str());
}

/**
 * @brief Main scheduler: apply pending battery profile, throttle reads, detect
 *        test start/end for logging.
 */
void loop() {
    // check if battery profile is being applied
    if (pendingBatteryIdx >= 0) {
        processPendingBattery(); // sends stop, applies params, sends start
        lastBatteryApply = millis();
        delay(500);
        return;
    }

    // don't read if we just applied battery settings
    if (millis() - lastBatteryApply < 2000) {
        return;
    }

    if (millis() - lastRead > readInterval) {
        readFZ35();
        updateGraphBuffersScaled(voltage.toFloat(), current.toFloat(), power.toFloat());
        
        // NEW: check if test just started
        float measI = current.toFloat();
        if (measI > 0.05f && !testInProgress) {
            testInProgress = true;
            testStartTime = millis();
            extern BatteryModule currentBattery;
            currentTestBattery = String(currentBattery.name);
            Serial.println("Test started: " + currentTestBattery);
        }
        
        // NEW: check if test completed (current dropped to ~0)
        if (testInProgress && measI < 0.01f) {
            float finalCap = capacityAh.toFloat();
            float testDuration = (millis() - testStartTime) / 3600000.0f; // hours
            if (finalCap > 0.001f) {
                saveTestResult(currentTestBattery.c_str(), finalCap, testDuration);
            }
            testInProgress = false;
            Serial.printf("Test completed: %.3f Ah in %.2f hours\n", finalCap, testDuration);
        }
        
        lastRead = millis();
    }
}
