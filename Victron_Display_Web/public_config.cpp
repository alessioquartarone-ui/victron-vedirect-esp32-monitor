#include "public_config.h"

VictronPublicConfig pubCfg;
static Preferences vicPrefs;

static String prefGetString(const char *key, const String &fallback) {
  return vicPrefs.getString(key, fallback);
}

static bool prefGetBool(const char *key, bool fallback) {
  return vicPrefs.getBool(key, fallback);
}

static int prefGetInt(const char *key, int fallback) {
  return vicPrefs.getInt(key, fallback);
}

static uint32_t prefGetUInt(const char *key, uint32_t fallback) {
  return vicPrefs.getUInt(key, fallback);
}

static float prefGetFloat(const char *key, float fallback) {
  return vicPrefs.getFloat(key, fallback);
}

// ======================================================
// Protocol helpers
// ======================================================

bool publicProtocolIsSupported(const String &protocolId) {
  if (protocolId == VIC_PROTOCOL_VEDIRECT) return VIC_SUPPORTS_VEDIRECT;
  if (protocolId == VIC_PROTOCOL_GENERIC_VEDIRECT) return VIC_SUPPORTS_GENERIC_VEDIRECT;
  if (protocolId == VIC_PROTOCOL_EPEVER_MODBUS) return VIC_SUPPORTS_EPEVER_MODBUS;
  if (protocolId == VIC_PROTOCOL_RENOGY_RS485) return VIC_SUPPORTS_RENOGY_RS485;
  if (protocolId == VIC_PROTOCOL_DALY_BMS) return VIC_SUPPORTS_DALY_BMS;
  if (protocolId == VIC_PROTOCOL_JBD_BMS) return VIC_SUPPORTS_JBD_BMS;
  if (protocolId == VIC_PROTOCOL_JK_BMS) return VIC_SUPPORTS_JK_BMS;
  if (protocolId == VIC_PROTOCOL_GENERIC_MODBUS_RTU) return VIC_SUPPORTS_GENERIC_MODBUS_RTU;
  if (protocolId == VIC_PROTOCOL_GENERIC_UART_TEXT) return VIC_SUPPORTS_GENERIC_UART_TEXT;
  return false;
}

String publicProtocolLabelFor(const String &protocolId) {
  if (protocolId == VIC_PROTOCOL_VEDIRECT) return "Victron VE.Direct";
  if (protocolId == VIC_PROTOCOL_GENERIC_VEDIRECT) return "Generic VE.Direct text";
  if (protocolId == VIC_PROTOCOL_EPEVER_MODBUS) return "Epever / Tracer RS485 Modbus";
  if (protocolId == VIC_PROTOCOL_RENOGY_RS485) return "Renogy RS485";
  if (protocolId == VIC_PROTOCOL_DALY_BMS) return "Daly BMS UART/RS485";
  if (protocolId == VIC_PROTOCOL_JBD_BMS) return "JBD BMS UART";
  if (protocolId == VIC_PROTOCOL_JK_BMS) return "JK BMS";
  if (protocolId == VIC_PROTOCOL_GENERIC_MODBUS_RTU) return "Generic Modbus RTU";
  if (protocolId == VIC_PROTOCOL_GENERIC_UART_TEXT) return "Generic UART text";
  return "Unknown protocol";
}

String publicProtocolStatusFor(const String &protocolId) {
  return publicProtocolIsSupported(protocolId) ? "supported" : "planned";
}

String publicProtocolNoteFor(const String &protocolId) {
  if (protocolId == VIC_PROTOCOL_VEDIRECT) {
    return VIC_COMPAT_NOTE_VEDIRECT;
  }

  if (protocolId == VIC_PROTOCOL_GENERIC_VEDIRECT) {
    return "Supported now for devices using a VE.Direct-like text output compatible with the current parser.";
  }

  if (protocolId == VIC_PROTOCOL_EPEVER_MODBUS ||
      protocolId == VIC_PROTOCOL_RENOGY_RS485 ||
      protocolId == VIC_PROTOCOL_GENERIC_MODBUS_RTU) {
    return VIC_COMPAT_NOTE_MODBUS;
  }

  if (protocolId == VIC_PROTOCOL_DALY_BMS ||
      protocolId == VIC_PROTOCOL_JBD_BMS ||
      protocolId == VIC_PROTOCOL_JK_BMS) {
    return VIC_COMPAT_NOTE_BMS;
  }

  return VIC_COMPAT_NOTE_GENERIC;
}

// ======================================================
// Defaults
// ======================================================

void applyPublicDefaults() {
  pubCfg.setupDone = false;

  pubCfg.firmwareName = VIC_FW_NAME;
  pubCfg.firmwareVersion = VIC_FW_VERSION;
  pubCfg.hardwareProfile = VIC_DEFAULT_HARDWARE_PROFILE;
  pubCfg.hardwareLabel = VIC_DEFAULT_HARDWARE_LABEL;
  pubCfg.hardwareDescription = VIC_DEFAULT_HARDWARE_DESCRIPTION;

  pubCfg.deviceProtocol = VIC_DEFAULT_PROTOCOL_PROFILE;
  pubCfg.deviceProtocolLabel = VIC_DEFAULT_PROTOCOL_LABEL;
  pubCfg.deviceProtocolStatus = VIC_DEFAULT_PROTOCOL_STATUS;
  pubCfg.deviceProtocolNote = VIC_COMPAT_NOTE_VEDIRECT;

  pubCfg.veDirectRxPin = VIC_USER_DEFAULT_VEDIRECT_RX_PIN;
  pubCfg.espBatteryAdcPin = VIC_USER_DEFAULT_ESP_BAT_ADC_PIN;
  pubCfg.espBatteryMultiplier = VIC_USER_DEFAULT_ESP_BAT_MULTIPLIER;

  pubCfg.shutdownPin = VIC_USER_DEFAULT_SHUTDOWN_PIN;
  pubCfg.statusLedPin = VIC_USER_DEFAULT_STATUS_LED_PIN;

  pubCfg.hostname = VIC_DEFAULT_HOSTNAME;
  pubCfg.setupApSsid = VIC_DEFAULT_SETUP_AP_SSID;
  pubCfg.setupApPassword = VIC_DEFAULT_SETUP_AP_PASSWORD;

  pubCfg.otaEnabled = VIC_DEFAULT_OTA_ENABLED;
  pubCfg.otaChannel = VIC_DEFAULT_OTA_CHANNEL;
  pubCfg.otaVersionUrl = VIC_DEFAULT_OTA_VERSION_URL;
  pubCfg.otaBinUrl = VIC_DEFAULT_OTA_BIN_URL;
  pubCfg.otaSha256Url = VIC_DEFAULT_OTA_SHA256_URL;

  pubCfg.plantName = VIC_DEFAULT_PLANT_NAME;
  pubCfg.batteryName = VIC_DEFAULT_BATTERY_NAME;
  pubCfg.batteryType = VIC_DEFAULT_BATTERY_TYPE;

  pubCfg.systemVoltage = VIC_DEFAULT_SYSTEM_VOLTAGE;
  pubCfg.batteryCapacityAh = VIC_DEFAULT_BATTERY_CAPACITY_AH;
  pubCfg.panelWatts = VIC_DEFAULT_PANEL_WATTS;

  pubCfg.batLowV = VIC_DEFAULT_BAT_LOW_V;
  pubCfg.batMediumV = VIC_DEFAULT_BAT_MEDIUM_V;
  pubCfg.batFullV = VIC_DEFAULT_BAT_FULL_V;

  pubCfg.sdLoggingEnabled = VIC_DEFAULT_SD_LOGGING_ENABLED;
  pubCfg.lfsLoggingEnabled = VIC_DEFAULT_LFS_LOGGING_ENABLED;

  pubCfg.backupConfigMax = VIC_DEFAULT_BACKUP_CONFIG_MAX;
  pubCfg.backupHistoryMax = VIC_DEFAULT_BACKUP_HISTORY_MAX;

  pubCfg.jsonPretty = VIC_DEFAULT_JSON_PRETTY;

  pubCfg.language = VIC_DEFAULT_LANGUAGE;
  pubCfg.theme = VIC_DEFAULT_THEME;

  pubCfg.dashRefreshMs = VIC_DEFAULT_DASH_REFRESH_MS;
  pubCfg.historyRefreshMs = VIC_DEFAULT_HISTORY_REFRESH_MS;

  pubCfg.popupsEnabled = VIC_DEFAULT_POPUPS_ENABLED;
  pubCfg.showEspBattery = VIC_DEFAULT_SHOW_ESP_BATTERY;
  pubCfg.showVeDirectDebug = VIC_DEFAULT_SHOW_VEDIRECT_DEBUG;

  pubCfg.shutdownEnabled = VIC_DEFAULT_SHUTDOWN_ENABLED;
  pubCfg.shutdownTimerSec = VIC_DEFAULT_SHUTDOWN_TIMER_SEC;
  pubCfg.shutdownConfirm = VIC_DEFAULT_SHUTDOWN_CONFIRM;

  pubCfg.allowFactoryReset = VIC_DEFAULT_ALLOW_FACTORY_RESET;
  pubCfg.requireSetupOnFirstBoot = VIC_DEFAULT_REQUIRE_SETUP_ON_FIRSTBOOT;
}

// ======================================================
// Load
// ======================================================

void loadPublicConfig() {
  applyPublicDefaults();

  vicPrefs.begin("vicpub", true);

  pubCfg.setupDone = prefGetBool("setupDone", pubCfg.setupDone);

  pubCfg.hardwareProfile = prefGetString("hwProfile", pubCfg.hardwareProfile);
  pubCfg.hardwareLabel = prefGetString("hwLabel", pubCfg.hardwareLabel);
  pubCfg.hardwareDescription = prefGetString("hwDesc", pubCfg.hardwareDescription);

  pubCfg.deviceProtocol = prefGetString("devProto", pubCfg.deviceProtocol);

  pubCfg.veDirectRxPin = prefGetInt("vedRx", pubCfg.veDirectRxPin);
  pubCfg.espBatteryAdcPin = prefGetInt("espAdc", pubCfg.espBatteryAdcPin);
  pubCfg.espBatteryMultiplier = prefGetFloat("espMult", pubCfg.espBatteryMultiplier);

  pubCfg.shutdownPin = prefGetInt("shutPin", pubCfg.shutdownPin);
  pubCfg.statusLedPin = prefGetInt("ledPin", pubCfg.statusLedPin);

  pubCfg.hostname = prefGetString("hostname", pubCfg.hostname);
  pubCfg.setupApSsid = prefGetString("apSsid", pubCfg.setupApSsid);
  pubCfg.setupApPassword = prefGetString("apPass", pubCfg.setupApPassword);

  pubCfg.otaEnabled = prefGetBool("otaEn", pubCfg.otaEnabled);
  pubCfg.otaChannel = prefGetString("otaChan", pubCfg.otaChannel);
  pubCfg.otaVersionUrl = prefGetString("otaVerUrl", pubCfg.otaVersionUrl);
  pubCfg.otaBinUrl = prefGetString("otaBinUrl", pubCfg.otaBinUrl);
  pubCfg.otaSha256Url = prefGetString("otaShaUrl", pubCfg.otaSha256Url);

  pubCfg.plantName = prefGetString("plant", pubCfg.plantName);
  pubCfg.batteryName = prefGetString("batName", pubCfg.batteryName);
  pubCfg.batteryType = prefGetString("batType", pubCfg.batteryType);

  pubCfg.systemVoltage = prefGetFloat("sysV", pubCfg.systemVoltage);
  pubCfg.batteryCapacityAh = prefGetFloat("batAh", pubCfg.batteryCapacityAh);
  pubCfg.panelWatts = prefGetFloat("panelW", pubCfg.panelWatts);

  pubCfg.batLowV = prefGetFloat("batLow", pubCfg.batLowV);
  pubCfg.batMediumV = prefGetFloat("batMed", pubCfg.batMediumV);
  pubCfg.batFullV = prefGetFloat("batFull", pubCfg.batFullV);

  pubCfg.sdLoggingEnabled = prefGetBool("sdLog", pubCfg.sdLoggingEnabled);
  pubCfg.lfsLoggingEnabled = prefGetBool("lfsLog", pubCfg.lfsLoggingEnabled);

  pubCfg.backupConfigMax = prefGetInt("bakCfg", pubCfg.backupConfigMax);
  pubCfg.backupHistoryMax = prefGetInt("bakHist", pubCfg.backupHistoryMax);

  pubCfg.jsonPretty = prefGetBool("jsonPretty", pubCfg.jsonPretty);

  pubCfg.language = prefGetString("lang", pubCfg.language);
  pubCfg.theme = prefGetString("theme", pubCfg.theme);

  pubCfg.dashRefreshMs = prefGetUInt("dashMs", pubCfg.dashRefreshMs);
  pubCfg.historyRefreshMs = prefGetUInt("histMs", pubCfg.historyRefreshMs);

  pubCfg.popupsEnabled = prefGetBool("popups", pubCfg.popupsEnabled);
  pubCfg.showEspBattery = prefGetBool("showEsp", pubCfg.showEspBattery);
  pubCfg.showVeDirectDebug = prefGetBool("showVedDbg", pubCfg.showVeDirectDebug);

  pubCfg.shutdownEnabled = prefGetBool("shutEn", pubCfg.shutdownEnabled);
  pubCfg.shutdownTimerSec = prefGetUInt("shutSec", pubCfg.shutdownTimerSec);
  pubCfg.shutdownConfirm = prefGetBool("shutConf", pubCfg.shutdownConfirm);

  pubCfg.allowFactoryReset = prefGetBool("allowReset", pubCfg.allowFactoryReset);
  pubCfg.requireSetupOnFirstBoot = prefGetBool("reqSetup", pubCfg.requireSetupOnFirstBoot);

  vicPrefs.end();

  pubCfg.deviceProtocolLabel = publicProtocolLabelFor(pubCfg.deviceProtocol);
  pubCfg.deviceProtocolStatus = publicProtocolStatusFor(pubCfg.deviceProtocol);
  pubCfg.deviceProtocolNote = publicProtocolNoteFor(pubCfg.deviceProtocol);

  pubCfg.veDirectRxPin = publicClampInt(pubCfg.veDirectRxPin, -1, 48);
  pubCfg.espBatteryAdcPin = publicClampInt(pubCfg.espBatteryAdcPin, -1, 48);
  pubCfg.espBatteryMultiplier = publicClampFloat(pubCfg.espBatteryMultiplier, 0.10f, 10.00f);

  pubCfg.shutdownPin = publicClampInt(pubCfg.shutdownPin, -1, 48);
  pubCfg.statusLedPin = publicClampInt(pubCfg.statusLedPin, -1, 48);

  pubCfg.systemVoltage = publicClampFloat(pubCfg.systemVoltage, 1.0f, 100.0f);
  pubCfg.batteryCapacityAh = publicClampFloat(pubCfg.batteryCapacityAh, 1.0f, 2000.0f);
  pubCfg.panelWatts = publicClampFloat(pubCfg.panelWatts, 1.0f, 10000.0f);

  pubCfg.backupConfigMax = publicClampInt(pubCfg.backupConfigMax, 0, 50);
  pubCfg.backupHistoryMax = publicClampInt(pubCfg.backupHistoryMax, 0, 50);

  pubCfg.dashRefreshMs = publicSafeUIntArg(String(pubCfg.dashRefreshMs), 3000, 1000, 60000);
  pubCfg.historyRefreshMs = publicSafeUIntArg(String(pubCfg.historyRefreshMs), 10000, 3000, 300000);
  pubCfg.shutdownTimerSec = publicSafeUIntArg(String(pubCfg.shutdownTimerSec), 180, 5, 3600);
}

// ======================================================
// Save
// ======================================================

void savePublicConfig() {
  pubCfg.deviceProtocolLabel = publicProtocolLabelFor(pubCfg.deviceProtocol);
  pubCfg.deviceProtocolStatus = publicProtocolStatusFor(pubCfg.deviceProtocol);
  pubCfg.deviceProtocolNote = publicProtocolNoteFor(pubCfg.deviceProtocol);

  vicPrefs.begin("vicpub", false);

  vicPrefs.putBool("setupDone", pubCfg.setupDone);

  vicPrefs.putString("hwProfile", pubCfg.hardwareProfile);
  vicPrefs.putString("hwLabel", pubCfg.hardwareLabel);
  vicPrefs.putString("hwDesc", pubCfg.hardwareDescription);

  vicPrefs.putString("devProto", pubCfg.deviceProtocol);
  vicPrefs.putString("devProtoLbl", pubCfg.deviceProtocolLabel);
  vicPrefs.putString("devProtoSt", pubCfg.deviceProtocolStatus);
  vicPrefs.putString("devProtoNt", pubCfg.deviceProtocolNote);

  vicPrefs.putInt("vedRx", pubCfg.veDirectRxPin);
  vicPrefs.putInt("espAdc", pubCfg.espBatteryAdcPin);
  vicPrefs.putFloat("espMult", pubCfg.espBatteryMultiplier);

  vicPrefs.putInt("shutPin", pubCfg.shutdownPin);
  vicPrefs.putInt("ledPin", pubCfg.statusLedPin);

  vicPrefs.putString("hostname", pubCfg.hostname);
  vicPrefs.putString("apSsid", pubCfg.setupApSsid);
  vicPrefs.putString("apPass", pubCfg.setupApPassword);

  vicPrefs.putBool("otaEn", pubCfg.otaEnabled);
  vicPrefs.putString("otaChan", pubCfg.otaChannel);
  vicPrefs.putString("otaVerUrl", pubCfg.otaVersionUrl);
  vicPrefs.putString("otaBinUrl", pubCfg.otaBinUrl);
  vicPrefs.putString("otaShaUrl", pubCfg.otaSha256Url);

  vicPrefs.putString("plant", pubCfg.plantName);
  vicPrefs.putString("batName", pubCfg.batteryName);
  vicPrefs.putString("batType", pubCfg.batteryType);

  vicPrefs.putFloat("sysV", pubCfg.systemVoltage);
  vicPrefs.putFloat("batAh", pubCfg.batteryCapacityAh);
  vicPrefs.putFloat("panelW", pubCfg.panelWatts);

  vicPrefs.putFloat("batLow", pubCfg.batLowV);
  vicPrefs.putFloat("batMed", pubCfg.batMediumV);
  vicPrefs.putFloat("batFull", pubCfg.batFullV);

  vicPrefs.putBool("sdLog", pubCfg.sdLoggingEnabled);
  vicPrefs.putBool("lfsLog", pubCfg.lfsLoggingEnabled);

  vicPrefs.putInt("bakCfg", pubCfg.backupConfigMax);
  vicPrefs.putInt("bakHist", pubCfg.backupHistoryMax);

  vicPrefs.putBool("jsonPretty", pubCfg.jsonPretty);

  vicPrefs.putString("lang", pubCfg.language);
  vicPrefs.putString("theme", pubCfg.theme);

  vicPrefs.putUInt("dashMs", pubCfg.dashRefreshMs);
  vicPrefs.putUInt("histMs", pubCfg.historyRefreshMs);

  vicPrefs.putBool("popups", pubCfg.popupsEnabled);
  vicPrefs.putBool("showEsp", pubCfg.showEspBattery);
  vicPrefs.putBool("showVedDbg", pubCfg.showVeDirectDebug);

  vicPrefs.putBool("shutEn", pubCfg.shutdownEnabled);
  vicPrefs.putUInt("shutSec", pubCfg.shutdownTimerSec);
  vicPrefs.putBool("shutConf", pubCfg.shutdownConfirm);

  vicPrefs.putBool("allowReset", pubCfg.allowFactoryReset);
  vicPrefs.putBool("reqSetup", pubCfg.requireSetupOnFirstBoot);

  vicPrefs.end();
}

// ======================================================
// Reset / setup state
// ======================================================

void resetPublicConfig() {
  vicPrefs.begin("vicpub", false);
  vicPrefs.clear();
  vicPrefs.end();
  applyPublicDefaults();
}

bool isFirstBootSetupRequired() {
  if (!pubCfg.requireSetupOnFirstBoot) return false;
  return !pubCfg.setupDone;
}

void markPublicSetupDone(bool done) {
  pubCfg.setupDone = done;
  savePublicConfig();
}

// ======================================================
// HTML helpers
// ======================================================

String publicBoolChecked(bool value) {
  return value ? "checked" : "";
}

String publicBoolSelected(bool value, bool expected) {
  return value == expected ? "selected" : "";
}

String publicHtmlEscape(const String &input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

// ======================================================
// Safe parsing helpers
// ======================================================

int publicClampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float publicClampFloat(float value, float minValue, float maxValue) {
  if (isnan(value) || isinf(value)) return minValue;
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

String publicSafeStringArg(const String &value, const String &fallback) {
  String v = value;
  v.trim();
  if (v.length() == 0) return fallback;
  if (v.length() > 256) v = v.substring(0, 256);
  return v;
}

int publicSafeIntArg(const String &value, int fallback, int minValue, int maxValue) {
  String v = value;
  v.trim();
  if (v.length() == 0) return fallback;

  char *endPtr = nullptr;
  long parsed = strtol(v.c_str(), &endPtr, 10);
  if (endPtr == v.c_str()) return fallback;

  return publicClampInt((int)parsed, minValue, maxValue);
}

uint32_t publicSafeUIntArg(const String &value, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
  String v = value;
  v.trim();
  if (v.length() == 0) return fallback;

  char *endPtr = nullptr;
  unsigned long parsed = strtoul(v.c_str(), &endPtr, 10);
  if (endPtr == v.c_str()) return fallback;

  if (parsed < minValue) return minValue;
  if (parsed > maxValue) return maxValue;

  return (uint32_t)parsed;
}

float publicSafeFloatArg(const String &value, float fallback, float minValue, float maxValue) {
  String v = value;
  v.trim();
  if (v.length() == 0) return fallback;

  char *endPtr = nullptr;
  float parsed = strtof(v.c_str(), &endPtr);
  if (endPtr == v.c_str()) return fallback;
  if (isnan(parsed) || isinf(parsed)) return fallback;

  return publicClampFloat(parsed, minValue, maxValue);
}

bool publicSafeBoolArg(const String &value, bool fallback) {
  String v = value;
  v.trim();
  v.toLowerCase();

  if (v == "1" || v == "true" || v == "on" || v == "yes" || v == "enabled") return true;
  if (v == "0" || v == "false" || v == "off" || v == "no" || v == "disabled") return false;

  return fallback;
}

// ======================================================
// Export helpers
// ======================================================

String publicConfigToJson(bool pretty) {
  String nl = pretty ? "\n" : "";
  String sp = pretty ? "  " : "";

  String json;
  json.reserve(5200);

  json += "{" + nl;
  json += sp + "\"firmwareName\":\"" + publicHtmlEscape(pubCfg.firmwareName) + "\"," + nl;
  json += sp + "\"firmwareVersion\":\"" + publicHtmlEscape(pubCfg.firmwareVersion) + "\"," + nl;
  json += sp + "\"hardwareProfile\":\"" + publicHtmlEscape(pubCfg.hardwareProfile) + "\"," + nl;
  json += sp + "\"hardwareLabel\":\"" + publicHtmlEscape(pubCfg.hardwareLabel) + "\"," + nl;

  json += sp + "\"deviceProtocol\":\"" + publicHtmlEscape(pubCfg.deviceProtocol) + "\"," + nl;
  json += sp + "\"deviceProtocolLabel\":\"" + publicHtmlEscape(pubCfg.deviceProtocolLabel) + "\"," + nl;
  json += sp + "\"deviceProtocolStatus\":\"" + publicHtmlEscape(pubCfg.deviceProtocolStatus) + "\"," + nl;
  json += sp + "\"deviceProtocolNote\":\"" + publicHtmlEscape(pubCfg.deviceProtocolNote) + "\"," + nl;

  json += sp + "\"setupDone\":" + String(pubCfg.setupDone ? "true" : "false") + "," + nl;

  json += sp + "\"veDirectRxPin\":" + String(pubCfg.veDirectRxPin) + "," + nl;
  json += sp + "\"espBatteryAdcPin\":" + String(pubCfg.espBatteryAdcPin) + "," + nl;
  json += sp + "\"espBatteryMultiplier\":" + String(pubCfg.espBatteryMultiplier, 3) + "," + nl;

  json += sp + "\"hostname\":\"" + publicHtmlEscape(pubCfg.hostname) + "\"," + nl;

  json += sp + "\"otaEnabled\":" + String(pubCfg.otaEnabled ? "true" : "false") + "," + nl;
  json += sp + "\"otaChannel\":\"" + publicHtmlEscape(pubCfg.otaChannel) + "\"," + nl;
  json += sp + "\"otaVersionUrl\":\"" + publicHtmlEscape(pubCfg.otaVersionUrl) + "\"," + nl;
  json += sp + "\"otaBinUrl\":\"" + publicHtmlEscape(pubCfg.otaBinUrl) + "\"," + nl;
  json += sp + "\"otaSha256Url\":\"" + publicHtmlEscape(pubCfg.otaSha256Url) + "\"," + nl;

  json += sp + "\"plantName\":\"" + publicHtmlEscape(pubCfg.plantName) + "\"," + nl;
  json += sp + "\"batteryName\":\"" + publicHtmlEscape(pubCfg.batteryName) + "\"," + nl;
  json += sp + "\"batteryType\":\"" + publicHtmlEscape(pubCfg.batteryType) + "\"," + nl;

  json += sp + "\"systemVoltage\":" + String(pubCfg.systemVoltage, 2) + "," + nl;
  json += sp + "\"batteryCapacityAh\":" + String(pubCfg.batteryCapacityAh, 2) + "," + nl;
  json += sp + "\"panelWatts\":" + String(pubCfg.panelWatts, 2) + "," + nl;

  json += sp + "\"backupConfigMax\":" + String(pubCfg.backupConfigMax) + "," + nl;
  json += sp + "\"backupHistoryMax\":" + String(pubCfg.backupHistoryMax) + "," + nl;

  json += sp + "\"language\":\"" + publicHtmlEscape(pubCfg.language) + "\"," + nl;
  json += sp + "\"theme\":\"" + publicHtmlEscape(pubCfg.theme) + "\"," + nl;

  json += sp + "\"dashRefreshMs\":" + String(pubCfg.dashRefreshMs) + "," + nl;
  json += sp + "\"historyRefreshMs\":" + String(pubCfg.historyRefreshMs) + "," + nl;

  json += sp + "\"shutdownEnabled\":" + String(pubCfg.shutdownEnabled ? "true" : "false") + "," + nl;
  json += sp + "\"shutdownTimerSec\":" + String(pubCfg.shutdownTimerSec) + nl;
  json += "}" + nl;

  return json;
}

String publicConfigSummaryText() {
  String out;

  out += "Firmware: " + pubCfg.firmwareName + " " + pubCfg.firmwareVersion + "\n";
  out += "Target: " + pubCfg.hardwareProfile + "\n";
  out += "Protocol: " + pubCfg.deviceProtocolLabel + " (" + pubCfg.deviceProtocolStatus + ")\n";
  out += "VE.Direct RX GPIO: " + String(pubCfg.veDirectRxPin) + "\n";
  out += "ESP Battery ADC GPIO: " + String(pubCfg.espBatteryAdcPin) + "\n";
  out += "Hostname: " + pubCfg.hostname + "\n";
  out += "OTA: " + String(pubCfg.otaEnabled ? "enabled" : "disabled") + "\n";
  out += "Plant: " + pubCfg.plantName + "\n";
  out += "Battery: " + pubCfg.batteryName + " / " + pubCfg.batteryType + "\n";

  return out;
}
