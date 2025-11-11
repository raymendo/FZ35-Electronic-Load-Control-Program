#include "FZ35_TestLog.h"
#include <time.h>

/**
 * @file FZ35_TestLog.cpp
 * @brief Implements load/save/clear of test results plus JSON serialization.
 */

TestResult testResults[MAX_TEST_RESULTS];
int testResultCount = 0;

void initTestLog() {
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }
    loadTestLog();
}

void loadTestLog() {
    testResultCount = 0;
    if (!LittleFS.exists(TEST_LOG_FILE)) {
        Serial.println("No test log file found, starting fresh");
        return;
    }
    
    File f = LittleFS.open(TEST_LOG_FILE, "r");
    if (!f) {
        Serial.println("Failed to open test log for reading");
        return;
    }
    
    while (f.available() && testResultCount < MAX_TEST_RESULTS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        // CSV format: date,batteryType,finalAh,timeHours
        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);
        int comma3 = line.indexOf(',', comma2 + 1);
        
        if (comma1 > 0 && comma2 > comma1 && comma3 > comma2) {
            TestResult &r = testResults[testResultCount];
            line.substring(0, comma1).toCharArray(r.date, sizeof(r.date));
            line.substring(comma1 + 1, comma2).toCharArray(r.batteryType, sizeof(r.batteryType));
            r.finalAh = line.substring(comma2 + 1, comma3).toFloat();
            r.testTimeHours = line.substring(comma3 + 1).toFloat();
            r.valid = true;
            testResultCount++;
        }
    }
    f.close();
    Serial.printf("Loaded %d test results\n", testResultCount);
}

/**
 * @brief Append result to memory + CSV (FIFO trim when full).
 */
void saveTestResult(const char* batteryName, float finalAh, float timeHours) {
    if (testResultCount >= MAX_TEST_RESULTS) {
        // shift array left to make room (FIFO)
        for (int i = 0; i < MAX_TEST_RESULTS - 1; i++) {
            testResults[i] = testResults[i + 1];
        }
        testResultCount = MAX_TEST_RESULTS - 1;
    }
    
    TestResult &r = testResults[testResultCount];
    
    // Get current time (requires NTP setup or user input)
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    snprintf(r.date, sizeof(r.date), "%04d-%02d-%02d %02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min);
    
    strncpy(r.batteryType, batteryName, sizeof(r.batteryType) - 1);
    r.batteryType[sizeof(r.batteryType) - 1] = '\0';
    r.finalAh = finalAh;
    r.testTimeHours = timeHours;
    r.valid = true;
    testResultCount++;
    
    // Append to file
    File f = LittleFS.open(TEST_LOG_FILE, "a");
    if (f) {
        f.printf("%s,%s,%.3f,%.2f\n", r.date, r.batteryType, r.finalAh, r.testTimeHours);
        f.close();
        Serial.printf("Saved test result: %s - %.3f Ah\n", r.batteryType, r.finalAh);
    } else {
        Serial.println("Failed to save test result");
    }
}

/**
 * @brief Serialize all valid test results to JSON array.
 */
String getTestResultsJson() {
    String json = "{\"results\":[";
    for (int i = 0; i < testResultCount; i++) {
        if (i > 0) json += ",";
        TestResult &r = testResults[i];
        json += "{";
        json += "\"date\":\"" + String(r.date) + "\",";
        json += "\"battery\":\"" + String(r.batteryType) + "\",";
        json += "\"capacity\":" + String(r.finalAh, 3) + ",";
        json += "\"time\":" + String(r.testTimeHours, 2);
        json += "}";
    }
    json += "]}";
    return json;
}

void clearTestLog() {
    testResultCount = 0;
    LittleFS.remove(TEST_LOG_FILE);
    Serial.println("Test log cleared");
}
