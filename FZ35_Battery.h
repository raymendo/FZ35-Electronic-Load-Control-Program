#pragma once
#include <Arduino.h>

/**
 * @file FZ35_Battery.h
 * @brief Battery profile definitions and selection API. Each BatteryModule carries
 *        nominal parameters and protection limits used to configure the load.
 */

enum class BatteryChemistry {
    LiIon,
    LiFePO4,
    LeadAcid,
    CoinCell
};

/**
 * @struct BatteryModule
 * @brief Defines one test profile.
 * @param nominalVoltage Approx full voltage.
 * @param capacityAh Nominal capacity in Ah.
 * @param maxLoadA Maximum safe current.
 * @param recommendedLoadA Suggested test current (C-rate based).
 * @param lowVoltageProtect Cutoff voltage.
 * @param overAhLimit Amp-hour termination limit.
 * @param overHourLimit Time termination HH:MM.
 */
struct BatteryModule {
    const char* name;
    BatteryChemistry chem;
    float nominalVoltage;
    float capacityAh;
    float maxLoadA;
    float recommendedLoadA; // NEW: recommended test load current (A)
    float lowVoltageProtect; // NEW: LVP cutoff voltage
    float overAhLimit;       // NEW: OAH in Ah (capacity limit)
    const char* overHourLimit; // NEW: OHP in HH:MM format
};

// only declare here, define exactly once in FZ35_Battery.cpp
extern BatteryModule batteryModules[];
extern const size_t batteryModulesCount;
extern BatteryModule currentBattery;

// helpers
const char* chemistryToString(BatteryChemistry c);
void selectBatteryByName(const char* name);

/**
 * @brief Apply profile by index (queues commands; sent later).
 * @return true on valid index.
 */
bool setActiveBattery(int idx);

// simple battery list API used by WebUI
int getBatteryCount();
String getBatteryName(int idx);
bool setActiveBattery(int idx);

// new: pending index (>=0 means pending) and processor called from main loop
extern int pendingBatteryIdx;
void processPendingBattery();
