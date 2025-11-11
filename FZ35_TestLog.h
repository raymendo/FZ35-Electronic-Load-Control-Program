#pragma once
#include <Arduino.h>
#include <LittleFS.h>

/**
 * @file FZ35_TestLog.h
 * @brief Persistent FIFO log of completed discharge tests (LittleFS CSV).
 */

#define MAX_TEST_RESULTS 50
#define TEST_LOG_FILE "/testlog.csv"

/**
 * @struct TestResult
 * @brief One test summary entry.
 * @param date Timestamp (local) start or completion.
 * @param batteryType Profile name.
 * @param finalAh Measured capacity at end.
 * @param testTimeHours Duration from start to end.
 */
struct TestResult {
    char date[20];        // YYYY-MM-DD HH:MM
    char batteryType[50]; // battery name
    float finalAh;        // measured capacity
    float testTimeHours;  // duration in hours
    bool valid;
};

extern TestResult testResults[MAX_TEST_RESULTS];
extern int testResultCount;

void initTestLog();
void saveTestResult(const char* batteryName, float finalAh, float timeHours);
String getTestResultsJson();
void loadTestLog();
void clearTestLog();
