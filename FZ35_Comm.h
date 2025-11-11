#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <math.h> // NEW: for roundf
#include "FZ35_Battery.h"

/**
 * @file FZ35_Comm.h
 * @brief Serial communication helpers for XY-FZ35 load. Provides sending with retries,
 *        classification of success/failure tokens, and parsing delegation to parseFZ35().
 */

#define RX_PIN 15
#define TX_PIN 13

extern SoftwareSerial fzSerial;

/**
 * @brief Callback implemented in main .ino to parse each line from device.
 */
void parseFZ35(const String &lineIn);

/**
 * @brief Send a command without newline (raw frame).
 */
inline void sendCommandNoNL(const String &cmd) {
    while (fzSerial.available()) { fzSerial.read(); }
    fzSerial.print(cmd); // no newline
    Serial.printf(">> Sent (no NL): %s\n", cmd.c_str());
}

// NEW helper: classify success
inline bool isSuccessResponse(const String &resp, const String &cmdPrefixLower){
    String r = resp; r.toLowerCase();
    if (r.indexOf("success") >= 0) return true;
    if (r.indexOf("sucess") >= 0) return true; // device typo
    if (r.indexOf("ok") >= 0) return true;
    if (r.indexOf("done") >= 0) return true;
    if (r.indexOf(cmdPrefixLower) >= 0) {
        for (size_t i=0;i<r.length();++i){
            if (isDigit(r[i])) return true;
        }
    }
    return false;
}
inline bool isFailureResponse(const String &resp){
    String r = resp; r.toLowerCase();
    if (r.indexOf("fail") >= 0) return true;
    if (r.indexOf("error") >= 0) return true;
    return false;
}

// NEW: robust send with confirmation & retries (prefer no newline)
inline bool sendCommandWithConfirm(const String &cmd, unsigned long overallTimeoutMs = 1000) {
    const int MAX_RETRIES = 3;
    const unsigned long IDLE_GAP_MS = 80;
    const unsigned long BETWEEN_RETRY_DELAY = 150;

    String keyLower = cmd.substring(0, cmd.indexOf(':'));
    keyLower.toLowerCase();

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        bool sendNoNewline = (attempt % 2 == 1);
        Serial.printf(">> Attempt %d/%d: %s (mode: %s newline)\n",
                      attempt, MAX_RETRIES, cmd.c_str(),
                      sendNoNewline ? "without" : "with");

        while (fzSerial.available()) fzSerial.read();

        if (sendNoNewline) fzSerial.print(cmd);
        else               fzSerial.println(cmd);

        unsigned long tStart = millis();
        unsigned long lastByte = tStart;
        String resp;

        while (millis() - tStart < overallTimeoutMs) {
            bool got = false;
            while (fzSerial.available()) {
                char c = (char)fzSerial.read();
                if (c == '\r') continue;
                if (c == '\n') {
                    if (resp.length() && resp.charAt(resp.length()-1) != '\n')
                        resp += '\n';
                } else {
                    resp += c;
                }
                lastByte = millis();
                got = true;
            }
            if (!got && resp.length() > 0 && (millis() - lastByte) >= IDLE_GAP_MS) break;
            delay(5);
        }

        resp.trim();
        Serial.printf("<< Collected (%u ms): \"%s\"\n", (unsigned)(millis() - tStart), resp.c_str());

        if (isFailureResponse(resp)) {
            Serial.println("   Detected explicit failure token.");
        } else if (isSuccessResponse(resp, keyLower)) {
            Serial.println("   ✓ Confirmed");
            return true;
        } else if (resp.length() == 0) {
            Serial.println("   (No response)");
        } else {
            Serial.println("   (Unclassified response, will retry)");
        }

        if (attempt < MAX_RETRIES) delay(BETWEEN_RETRY_DELAY);
    }

    Serial.printf("   ✗✗ FAILED after %d attempts: %s\n", MAX_RETRIES, cmd.c_str());
    return false;
}

// NEW: try parameter with formatting variants (e.g., OVP tricky cases)
inline bool sendParamVariants(const String &key, const String &value, unsigned long timeoutMs = 1000) {
    String variants[5];
    int count = 0;

    // base
    variants[count++] = key + ":" + value;

    // numeric variants
    float f = value.toFloat();
    bool hasDot = value.indexOf('.') >= 0;
    if (hasDot) variants[count++] = key + ":" + String(f, 1);
    variants[count++] = key + ":" + String(f, 3);

    // as integer millivolts for voltage-like keys
    if (key.equalsIgnoreCase("OVP") || key.equalsIgnoreCase("LVP")) {
        int mv = (int)roundf(f * 1000.0f);
        variants[count++] = key + ":" + String(mv);
    }

    for (int i=0;i<count;i++){
        Serial.printf(".. trying variant: %s\n", variants[i].c_str());
        if (sendCommandWithConfirm(variants[i], timeoutMs)) return true;
        delay(120);
    }
    return false;
}

/**
 * @brief Core blocking send with newline and response aggregation.
 * @param cmd Command text.
 * @param timeout_ms Overall timeout.
 * @return Raw concatenated response lines (trimmed).
 */
inline String sendCommand(const String &cmd, unsigned long timeout_ms) {
    Serial.printf(">> Sending: %s\n", cmd.c_str());
    while (fzSerial.available()) { fzSerial.read(); }
    fzSerial.println(cmd);

    unsigned long t0 = millis();
    String resp = "";
    String line = "";
    bool seenPrefix = false;
    bool seenCSV = false;

    while (millis() - t0 < timeout_ms) {
        while (fzSerial.available()) {
            char c = (char)fzSerial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                line.trim();
                if (line.length() > 0) {
                    if (resp.length()) resp += '\n';
                    resp += line;
                    if (line.indexOf("OVP:") >= 0 || line.indexOf("OCP:") >= 0 || line.indexOf("OPP:") >= 0) seenPrefix = true;
                    if (line.indexOf("V,") >= 0 && line.indexOf("Ah") >= 0) seenCSV = true;
                    line = "";
                    if (seenPrefix && seenCSV) {
                        unsigned long waitExtra = millis() + 10;
                        while (millis() < waitExtra) { while (fzSerial.available()) fzSerial.read(); }
                        goto finished_read;
                    }
                }
            } else {
                line += c;
            }
        }
        delay(5);
    }

finished_read:
    line.trim();
    if (line.length() > 0) {
        if (resp.length()) resp += '\n';
        resp += line;
    }

    resp.trim();
    Serial.printf("<< Received: %s\n", resp.c_str());
    return resp;
}

/**
 * @brief High-level read cycle: sends "read" then feeds each line to parseFZ35().
 */
inline void readFZ35() {
    String raw = sendCommand("read", 900);
    Serial.printf("RAW:\n%s\n", raw.c_str());

    int start = 0;
    while (start < raw.length()) {
        int end = raw.indexOf('\n', start);
        if (end < 0) end = raw.length();
        String line = raw.substring(start, end);
        line.trim();
        if (line.length() > 0) parseFZ35(line);
        start = end + 1;
    }
}
