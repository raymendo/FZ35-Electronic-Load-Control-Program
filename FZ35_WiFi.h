#pragma once
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPAsyncWiFiManager.h>
#include "FZ35_WebUI.h"

/**
 * @file FZ35_WiFi.h
 * @brief WiFi provisioning using AsyncWiFiManager. Falls back to AP if connection fails.
 */

/**
 * @brief Attempt autoConnect; starts AP "FZ35-Lab" if not successful.
 */
inline void setupWiFi(AsyncWebServer &server, DNSServer &dns) {
    AsyncWiFiManager wifiManager(&server, &dns);
    bool res = wifiManager.autoConnect("FZ35-Lab", "12345678");
    if (!res) {
        Serial.println("⚠️ WiFi failed -> AP mode started.");
    } else {
        Serial.printf("✅ Connected to %s\n", WiFi.localIP().toString().c_str());
    }
}

/**
 * @brief Register HTTP endpoints / start server.
 */
inline void setupWebServer(AsyncWebServer &server) {
    setupWebUI(); // call the function in FZ35_WebUI.h
}
