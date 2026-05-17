#pragma once

#include <Arduino.h>
#include <Preferences.h>

#if __has_include("config_user.h")
  #include "config_user.h"
#else
  #include "config_user.example.h"
#endif

/*
  Runtime public configuration.

  These values are loaded from ESP32 Preferences/NVS.
  Defaults come from config_user.example.h or optional config_user.h.

  The setup wizard will save user settings here.
*/

struct VictronPublicConfig {
  // Setup state
  bool setupDone;

  // Firmware / target info
  String firmwareName;
  String firmwareVersion;
  String hardwareProfile;
  String hardwareLabel;
  String hardwareDescription;

  // Runtime configurable pins
  int veDirectRxPin;
  int espBatteryAdcPin;
  float espBatteryMultiplier;

  int shutdownPin;
  int statusLedPin;

  // WiFiManager / network
  String hostname;
  String setupApSsid;
  String setupApPassword;

  // OTA
  bool otaEnabled;
  String otaChannel;
  String otaVersionUrl;
  String otaBinUrl;
  String otaSha256Url;

  // Plant / battery
  String plantName;
  String batteryName;
  String batteryType;

  float systemVoltage;
  float batteryCapacityAh;
  float panelWatts;

  // Battery thresholds
  float batLowV;
  float batMediumV;
  float batFullV;

  // Logging / backup
  bool sdLoggingEnabled;
  bool lfsLoggingEnabled;

  int backupConfigMax;
  int backupHistoryMax;

  bool jsonPretty;

  // WebUI
  String language;
  String theme;

  uint32_t dashRefreshMs;
  uint32_t historyRefreshMs;

  bool popupsEnabled;
  bool showEspBattery;
  bool showVeDirectDebug;

  // Shutdown
  bool shutdownEnabled;
  uint32_t shutdownTimerSec;
  bool shutdownConfirm;

  // Safety / reset
  bool allowFactoryReset;
  bool requireSetupOnFirstBoot;
};

extern VictronPublicConfig pubCfg;

// ======================================================
// Core config functions
// ======================================================

void applyPublicDefaults();
void loadPublicConfig();
void savePublicConfig();
void resetPublicConfig();

bool isFirstBootSetupRequired();
void markPublicSetupDone(bool done);

// ======================================================
// HTML helpers
// ======================================================

String publicBoolChecked(bool value);
String publicBoolSelected(bool value, bool expected);
String publicHtmlEscape(const String &input);

// ======================================================
// Safe parsing helpers
// ======================================================

int publicClampInt(int value, int minValue, int maxValue);
float publicClampFloat(float value, float minValue, float maxValue);

String publicSafeStringArg(const String &value, const String &fallback);
int publicSafeIntArg(const String &value, int fallback, int minValue, int maxValue);
uint32_t publicSafeUIntArg(const String &value, uint32_t fallback, uint32_t minValue, uint32_t maxValue);
float publicSafeFloatArg(const String &value, float fallback, float minValue, float maxValue);
bool publicSafeBoolArg(const String &value, bool fallback);

// ======================================================
// Debug / export helpers
// ======================================================

String publicConfigToJson(bool pretty = true);
String publicConfigSummaryText();
