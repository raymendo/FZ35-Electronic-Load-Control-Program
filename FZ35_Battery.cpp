#include "FZ35_Battery.h"
#include <Arduino.h>
#include "FZ35_Comm.h"

/**
 * @file FZ35_Battery.cpp
 * @brief Implements battery profile selection, clamping to device limits,
 *        queuing of parameter commands, and staged transmission (processPendingBattery()).
 */

// extern declarations for globals defined in main .ino
extern String TEST_LOAD; // NEW

// ===== battery table with complete protection parameters =====
// Recommended test loads based on typical discharge test standards:
// - Li-ion/LiPo: 0.2C–1C (we use 0.5C conservative)
// - LiFePO4: 0.5C–1C (we use 0.5C)
// - Lead Acid: 0.05C–0.2C (we use 0.1C)
// - Primary lithium (coin cells): 0.1C–0.5C (we use 0.2C)
// - Alkaline: 0.1C–0.5C (we use 0.2C)
// - NiMH: 0.2C–1C (we use 0.5C)

BatteryModule batteryModules[] = {
  // name, chem, nominalV, capacityAh, maxLoadA, recommendedLoadA, LVP, OAH, OHP
  { "24V Generic 5.00A", BatteryChemistry::LiIon, 24.20f, 30.0f, 5.10f, 5.00f, 18.0f, 36.0f, "10:00" }, // 6S Li-ion: 3.0V*6=18V cutoff
  { "24V Lead Acid 5.00A", BatteryChemistry::LeadAcid, 24.00f, 50.0f, 10.00f, 5.00f, 21.0f, 60.0f, "10:00" }, // 12 cells: 1.75V*12=21V
  { "24V LiFePO4 5.00A", BatteryChemistry::LiFePO4, 24.20f, 20.0f, 20.00f, 5.00f, 20.0f, 24.0f, "05:00" }, // 8S: 2.5V*8=20V

  // Single-cell Li-ion (3.0V cutoff)
  { "18650 Li-ion 4.2V 1.30A", BatteryChemistry::LiIon, 4.20f, 2.6f, 3.0f, 1.30f, 3.0f, 3.2f, "02:00" },
  { "21700 Li-ion 4.2V 2.00A", BatteryChemistry::LiIon, 4.20f, 4.0f, 5.0f, 2.00f, 3.0f, 4.8f, "02:30" },
  { "14500 Li-ion 4.2V 0.40A", BatteryChemistry::LiIon, 4.20f, 0.8f, 1.5f, 0.40f, 3.0f, 1.0f, "02:00" },
  { "16340 Li-ion 4.2V 0.35A", BatteryChemistry::LiIon, 4.20f, 0.7f, 1.5f, 0.35f, 3.0f, 0.85f, "02:00" },

  // LiFePO4 cells (2.5V cutoff)
  { "18650 LiFePO4 3.65V 0.75A", BatteryChemistry::LiFePO4, 3.65f, 1.5f, 3.0f, 0.75f, 2.5f, 1.8f, "02:00" },
  { "26650 LiFePO4 3.65V 1.65A", BatteryChemistry::LiFePO4, 3.65f, 3.3f, 5.0f, 1.65f, 2.5f, 4.0f, "02:30" },

  // Primary Lithium (2.0V cutoff)
  { "CR2 3.3V 0.16A", BatteryChemistry::CoinCell, 3.30f, 0.8f, 1.0f, 0.16f, 2.0f, 1.0f, "05:00" },
  { "CR123A 3.3V 0.30A", BatteryChemistry::CoinCell, 3.30f, 1.5f, 1.5f, 0.30f, 2.0f, 1.8f, "05:00" },

  // Alkaline (0.9V cutoff)
  { "AA Alkaline 1.6V 0.48A", BatteryChemistry::LeadAcid, 1.60f, 2.4f, 1.0f, 0.48f, 0.9f, 3.0f, "05:00" },
  { "AAA Alkaline 1.6V 0.24A", BatteryChemistry::LeadAcid, 1.60f, 1.2f, 0.5f, 0.24f, 0.9f, 1.5f, "05:00" },

  // NiMH (0.9V cutoff)
  { "AA NiMH 1.5V 1.00A", BatteryChemistry::LeadAcid, 1.50f, 2.0f, 2.0f, 1.00f, 0.9f, 2.5f, "02:00" },
  { "AAA NiMH 1.5V 0.40A", BatteryChemistry::LeadAcid, 1.50f, 0.8f, 1.0f, 0.40f, 0.9f, 1.0f, "02:00" },

  // 9V batteries
  { "PP3 9V Alkaline 0.06A", BatteryChemistry::LeadAcid, 9.60f, 0.6f, 0.2f, 0.06f, 5.4f, 0.75f, "10:00" }, // 6 cells: 0.9V*6=5.4V
  { "PP3 8.4V Li-ion 0.30A", BatteryChemistry::LiIon, 8.40f, 0.6f, 0.5f, 0.30f, 6.0f, 0.75f, "02:00" }, // 2S: 3.0V*2=6V

  // LiPo packs
  { "2S LiPo 7.4V 1.10A", BatteryChemistry::LiIon, 8.40f, 2.2f, 5.0f, 1.10f, 6.0f, 2.6f, "02:00" }, // 2S: 3.0V*2
  { "3S LiPo 11.1V 1.10A", BatteryChemistry::LiIon, 12.60f, 2.2f, 5.0f, 1.10f, 9.0f, 2.6f, "02:00" }, // 3S: 3.0V*3
  { "4S LiPo 14.8V 1.10A", BatteryChemistry::LiIon, 16.80f, 2.2f, 2.0f, 1.10f, 12.0f, 2.6f, "02:00" }, // 4S: 3.0V*4
  { "5S LiPo 18.5V 1.10A", BatteryChemistry::LiIon, 21.00f, 2.2f, 2.0f, 1.10f, 15.0f, 2.6f, "02:00" }, // 5S: 3.0V*5

  // LiFePO4 pack
  { "4S LiFePO4 12.8V 5.00A", BatteryChemistry::LiFePO4, 14.60f, 10.0f, 5.0f, 5.00f, 10.0f, 12.0f, "02:30" }, // 4S: 2.5V*4

  // Lead Acid (10.5V for 12V battery = 1.75V/cell)
  { "12V SLA 14.4V 0.70A", BatteryChemistry::LeadAcid, 14.40f, 7.0f, 5.0f, 0.70f, 10.5f, 8.5f, "10:00" },
};
const size_t batteryModulesCount = sizeof(batteryModules) / sizeof(batteryModules[0]);
// start with index 0 active
BatteryModule currentBattery = batteryModules[0];

// device rated limits (do not exceed)
static constexpr float RATED_VOLTAGE_MAX = 25.0f;
static constexpr float RATED_CURRENT_MAX = 5.0f;
static constexpr float RATED_POWER_MAX   = 35.0f;

// pending index (no apply yet). -1 = none
int pendingBatteryIdx = -1;
bool pendingWasClamped = false;

// NEW: freeze numeric values at selection time (used for sending)
static float pendingOVP = 0.0f;
static float pendingOCP = 0.0f;
static float pendingOPP = 0.0f;
static float pendingLVP = 0.0f;
static float pendingOAH = 0.0f;
static String pendingOHP;

// helpers exposed in header
int getBatteryCount() { return (int)batteryModulesCount; }
String getBatteryName(int idx) {
  if (idx < 0 || (size_t)idx >= batteryModulesCount) return String();
  return String(batteryModules[idx].name);
}
int getActiveBatteryIndex() {
  for (size_t i = 0; i < batteryModulesCount; ++i) {
    if (strcmp(currentBattery.name, batteryModules[i].name) == 0) return (int)i;
  }
  return 0;
}
String getBatteryListJson() {
  String s = "{\"active\":" + String(getActiveBatteryIndex()) + ",\"batteries\":[";
  for (size_t i = 0; i < batteryModulesCount; ++i) {
    if (i) s += ",";
    s += "\"" + String(batteryModules[i].name) + "\"";
  }
  s += "]}";
  return s;
}

void selectBatteryByName(const char* name){
  for (size_t i=0;i<batteryModulesCount;i++){
    if (strcmp(name, batteryModules[i].name) == 0) {
      setActiveBattery((int)i);
      return;
    }
  }
}

/**
 * @brief Queue profile, clamp values (V/I/P), format strings for UI, freeze numeric copies.
 */
// setActiveBattery: queue profile (update UI globals immediately)
// now clamps values to device rated limits
bool setActiveBattery(int idx){
  if (idx < 0 || (size_t)idx >= batteryModulesCount) return false;
  currentBattery = batteryModules[idx];

  // requested values from profile
  float reqOVP = currentBattery.nominalVoltage;
  float reqOCP = currentBattery.maxLoadA;
  float reqOPP = reqOVP * reqOCP;

  bool clamped = false;

  // clamp voltage
  if (reqOVP > RATED_VOLTAGE_MAX) { reqOVP = RATED_VOLTAGE_MAX; clamped = true; }

  // clamp current
  if (reqOCP > RATED_CURRENT_MAX) { reqOCP = RATED_CURRENT_MAX; clamped = true; }

  // ensure power limit: if product still > max, reduce current to fit power budget
  if (reqOVP * reqOCP > RATED_POWER_MAX) {
    float allowedI = RATED_POWER_MAX / reqOVP;
    if (allowedI < reqOCP) {
      reqOCP = allowedI;
      clamped = true;
    }
  }

  // Quantize OVP to 0.1V steps (device accepts 0.1V resolution) and recompute OPP
  reqOVP = roundf(reqOVP * 10.0f) / 10.0f;
  reqOPP = reqOVP * reqOCP;

  // format strings for UI and sending
  extern String OVP, OCP, OPP, LVP, OAH, OHP, TEST_LOAD;
  OVP = String(reqOVP, 1);
  OCP = String(reqOCP, 2);
  OPP = String(reqOPP, 2);

  // NEW: use chemistry-specific protection values from profile
  LVP = String(currentBattery.lowVoltageProtect, 1);
  OAH = String(currentBattery.overAhLimit, 3);
  OHP = String(currentBattery.overHourLimit);

  // NEW: use pre-defined recommended test load
  float recommendedI = currentBattery.recommendedLoadA;
  if (recommendedI > RATED_CURRENT_MAX) recommendedI = RATED_CURRENT_MAX;
  if (recommendedI < 0.05f) recommendedI = 0.05f;
  TEST_LOAD = String(recommendedI, 2);

  // NEW: freeze numeric values for sending
  pendingOVP = reqOVP;
  pendingOCP = reqOCP;
  pendingOPP = reqOPP;
  pendingLVP = currentBattery.lowVoltageProtect;
  pendingOAH = currentBattery.overAhLimit;
  pendingOHP = String(currentBattery.overHourLimit);

  // queue actual device commands for later (do NOT send now)
  pendingBatteryIdx = idx;
  pendingWasClamped = clamped;

  Serial.printf("Queued battery [%d] %s -> will apply when comm idle (clamped=%s)\n",
                idx, currentBattery.name, clamped ? "YES" : "NO");
  if (clamped) {
    Serial.println("Warning: profile values were clamped to device rated limits (V<=25.0V, I<=5.0A, P<=35W).");
  }
  return true;
}

/**
 * @brief Send queued profile parameters in ordered sequence with confirmation.
 * Stops measurement, applies parameters, restarts measurement.
 */
// processPendingBattery() uses frozen numeric values above
void processPendingBattery() {
  if (pendingBatteryIdx < 0) return;
  int idx = pendingBatteryIdx;
  pendingBatteryIdx = -1;

  int successCount = 0;
  const int totalCommands = 6;

  Serial.printf("\n=== Applying battery[%d] profile (clamped=%s) ===\n",
                idx, pendingWasClamped ? "YES" : "NO");

  // STEP 1: stop measurements to avoid interference
  Serial.println("Stopping measurements...");
  sendCommandNoNL("stop");
  delay(300);

  auto doCmd = [&](int order, const String &payload){
    Serial.printf("\n[%d/%d] Setting %s\n", order, totalCommands, payload.c_str());
    if (sendCommandWithConfirm(payload, 1000)) successCount++;
    delay(120);
  };

  // NEW: send test load current (format: x.xxA without any prefix)
  Serial.println("\n[0/6] Setting LOAD (test current)");
  String loadCmd = TEST_LOAD + "A"; // e.g., "1.30A"
  if (sendCommandWithConfirm(loadCmd, 1000)) {
    Serial.println("Test load current applied.");
  } else {
    Serial.println("Failed to apply test load current.");
  }
  delay(150);

  // STEP 2: send parameters
  doCmd(1, "OCP:" + String(pendingOCP, 2));
  doCmd(2, "OPP:" + String(pendingOPP, 2));
  doCmd(3, "LVP:" + String(pendingLVP, 1));
  doCmd(4, "OAH:" + String(pendingOAH, 3));
  doCmd(5, "OHP:" + pendingOHP);

  // OVP last, try variants
  {
    String ovpStr = String(pendingOVP, 1);
    Serial.printf("\n[6/%d] Setting OVP:%s (with variants)\n", totalCommands, ovpStr.c_str());
    if (sendParamVariants("OVP", ovpStr, 1200)) successCount++;
  }

  Serial.printf("\n=== Battery[%d] Apply Complete: %d/%d successful (clamped=%s) ===\n\n",
                idx, successCount, totalCommands, pendingWasClamped ? "YES" : "NO");

  if (successCount < totalCommands) {
    Serial.println("WARNING: Some parameters not confirmed. Consider checking wiring or increasing timeout.");
  }

  // STEP 3: restart measurements
  Serial.println("Restarting measurements...");
  sendCommandNoNL("start");
  delay(300);

  pendingWasClamped = false;
}
