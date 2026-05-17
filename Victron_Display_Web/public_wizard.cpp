#include "public_wizard.h"

// ======================================================
// Route registration
// ======================================================

void registerPublicWizardRoutes(WebServer &server) {
  server.on("/setup", HTTP_GET, [&server]() {
    handlePublicSetup(server);
  });

  server.on("/setup-save", HTTP_POST, [&server]() {
    handlePublicSetupSave(server);
  });

  server.on("/setup-reset", HTTP_GET, [&server]() {
    handlePublicSetupReset(server);
  });

  server.on("/setup-json", HTTP_GET, [&server]() {
    handlePublicSetupJson(server);
  });
}

bool publicWizardShouldRedirectToSetup() {
  return isFirstBootSetupRequired();
}

// ======================================================
// HTTP handlers
// ======================================================

void handlePublicSetup(WebServer &server) {
  server.send(200, "text/html", buildPublicWizardHtml());
}

void handlePublicSetupJson(WebServer &server) {
  server.send(200, "application/json", publicConfigToJson(true));
}

void handlePublicSetupReset(WebServer &server) {
  if (!pubCfg.allowFactoryReset) {
    server.send(403, "text/plain", "Factory reset disabled");
    return;
  }

  resetPublicConfig();

  String html;
  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3;url=/setup'>";
  html += "<title>Config reset</title>";
  html += "<style>body{font-family:Arial;background:#0f172a;color:#e5e7eb;padding:24px}a{color:#38bdf8}</style>";
  html += "</head><body>";
  html += "<h2>Configuration reset completed</h2>";
  html += "<p>The device configuration has been reset to public defaults.</p>";
  html += "<p>Redirecting to setup wizard...</p>";
  html += "<p><a href='/setup'>Open setup wizard</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handlePublicSetupSave(WebServer &server) {
  // Runtime pins
  pubCfg.veDirectRxPin = publicSafeIntArg(
    server.arg("veDirectRxPin"),
    pubCfg.veDirectRxPin,
    -1,
    48
  );

  pubCfg.espBatteryAdcPin = publicSafeIntArg(
    server.arg("espBatteryAdcPin"),
    pubCfg.espBatteryAdcPin,
    -1,
    48
  );

  pubCfg.espBatteryMultiplier = publicSafeFloatArg(
    server.arg("espBatteryMultiplier"),
    pubCfg.espBatteryMultiplier,
    0.10f,
    10.00f
  );

  pubCfg.shutdownPin = publicSafeIntArg(
    server.arg("shutdownPin"),
    pubCfg.shutdownPin,
    -1,
    48
  );

  pubCfg.statusLedPin = publicSafeIntArg(
    server.arg("statusLedPin"),
    pubCfg.statusLedPin,
    -1,
    48
  );

  // Network
  pubCfg.hostname = publicSafeStringArg(server.arg("hostname"), pubCfg.hostname);
  pubCfg.setupApSsid = publicSafeStringArg(server.arg("setupApSsid"), pubCfg.setupApSsid);
  pubCfg.setupApPassword = publicSafeStringArg(server.arg("setupApPassword"), pubCfg.setupApPassword);

  // OTA
  pubCfg.otaEnabled = server.hasArg("otaEnabled");
  pubCfg.otaChannel = publicSafeStringArg(server.arg("otaChannel"), pubCfg.otaChannel);
  pubCfg.otaVersionUrl = publicSafeStringArg(server.arg("otaVersionUrl"), pubCfg.otaVersionUrl);
  pubCfg.otaBinUrl = publicSafeStringArg(server.arg("otaBinUrl"), pubCfg.otaBinUrl);
  pubCfg.otaSha256Url = publicSafeStringArg(server.arg("otaSha256Url"), pubCfg.otaSha256Url);

  // Plant
  pubCfg.plantName = publicSafeStringArg(server.arg("plantName"), pubCfg.plantName);
  pubCfg.batteryName = publicSafeStringArg(server.arg("batteryName"), pubCfg.batteryName);
  pubCfg.batteryType = publicSafeStringArg(server.arg("batteryType"), pubCfg.batteryType);

  pubCfg.systemVoltage = publicSafeFloatArg(
    server.arg("systemVoltage"),
    pubCfg.systemVoltage,
    1.0f,
    100.0f
  );

  pubCfg.batteryCapacityAh = publicSafeFloatArg(
    server.arg("batteryCapacityAh"),
    pubCfg.batteryCapacityAh,
    1.0f,
    2000.0f
  );

  pubCfg.panelWatts = publicSafeFloatArg(
    server.arg("panelWatts"),
    pubCfg.panelWatts,
    1.0f,
    10000.0f
  );

  // Thresholds
  pubCfg.batLowV = publicSafeFloatArg(
    server.arg("batLowV"),
    pubCfg.batLowV,
    0.0f,
    100.0f
  );

  pubCfg.batMediumV = publicSafeFloatArg(
    server.arg("batMediumV"),
    pubCfg.batMediumV,
    0.0f,
    100.0f
  );

  pubCfg.batFullV = publicSafeFloatArg(
    server.arg("batFullV"),
    pubCfg.batFullV,
    0.0f,
    100.0f
  );

  // Logging / backup
  pubCfg.sdLoggingEnabled = server.hasArg("sdLoggingEnabled");
  pubCfg.lfsLoggingEnabled = server.hasArg("lfsLoggingEnabled");

  pubCfg.backupConfigMax = publicSafeIntArg(
    server.arg("backupConfigMax"),
    pubCfg.backupConfigMax,
    0,
    50
  );

  pubCfg.backupHistoryMax = publicSafeIntArg(
    server.arg("backupHistoryMax"),
    pubCfg.backupHistoryMax,
    0,
    50
  );

  pubCfg.jsonPretty = server.hasArg("jsonPretty");

  // WebUI
  pubCfg.language = publicSafeStringArg(server.arg("language"), pubCfg.language);
  pubCfg.theme = publicSafeStringArg(server.arg("theme"), pubCfg.theme);

  pubCfg.dashRefreshMs = publicSafeUIntArg(
    server.arg("dashRefreshMs"),
    pubCfg.dashRefreshMs,
    1000,
    60000
  );

  pubCfg.historyRefreshMs = publicSafeUIntArg(
    server.arg("historyRefreshMs"),
    pubCfg.historyRefreshMs,
    3000,
    300000
  );

  pubCfg.popupsEnabled = server.hasArg("popupsEnabled");
  pubCfg.showEspBattery = server.hasArg("showEspBattery");
  pubCfg.showVeDirectDebug = server.hasArg("showVeDirectDebug");

  // Shutdown
  pubCfg.shutdownEnabled = server.hasArg("shutdownEnabled");

  pubCfg.shutdownTimerSec = publicSafeUIntArg(
    server.arg("shutdownTimerSec"),
    pubCfg.shutdownTimerSec,
    5,
    3600
  );

  pubCfg.shutdownConfirm = server.hasArg("shutdownConfirm");

  // Safety
  pubCfg.allowFactoryReset = server.hasArg("allowFactoryReset");
  pubCfg.requireSetupOnFirstBoot = server.hasArg("requireSetupOnFirstBoot");

  pubCfg.setupDone = true;
  savePublicConfig();

  String html;
  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3;url=/'>";
  html += "<title>Setup saved</title>";
  html += "<style>body{font-family:Arial;background:#0f172a;color:#e5e7eb;padding:24px}a{color:#38bdf8}.box{background:#111827;border:1px solid #334155;border-radius:14px;padding:18px;max-width:760px}</style>";
  html += "</head><body>";
  html += "<div class='box'>";
  html += "<h2>Setup saved</h2>";
  html += "<p>Configuration has been saved. Some changes, such as WiFi hostname, VE.Direct pin or OTA URLs, may require a reboot.</p>";
  html += "<p>Redirecting to dashboard...</p>";
  html += "<p><a href='/'>Open dashboard</a> | <a href='/setup'>Back to setup</a> | <a href='/setup-json'>View JSON</a></p>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ======================================================
// HTML builder
// ======================================================

String buildPublicWizardHtml() {
  String html;
  html.reserve(24000);

  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Victron ESP32 Monitor Setup</title>";

  html += "<style>";
  html += ":root{--bg:#020617;--card:#0f172a;--card2:#111827;--line:#334155;--txt:#e5e7eb;--muted:#94a3b8;--accent:#38bdf8;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444}";
  html += "*{box-sizing:border-box}";
  html += "body{margin:0;background:linear-gradient(180deg,#020617,#0f172a);color:var(--txt);font-family:Arial,Helvetica,sans-serif}";
  html += "header{padding:22px 18px;border-bottom:1px solid var(--line);background:#020617;position:sticky;top:0;z-index:5}";
  html += "header h1{margin:0;font-size:22px}";
  html += "header p{margin:6px 0 0;color:var(--muted);font-size:13px}";
  html += "main{max-width:980px;margin:0 auto;padding:18px}";
  html += ".notice{border:1px solid var(--line);background:#111827;border-radius:16px;padding:14px;margin-bottom:16px;color:var(--muted);line-height:1.45}";
  html += ".notice b{color:var(--txt)}";
  html += ".section{background:rgba(15,23,42,.96);border:1px solid var(--line);border-radius:18px;margin:16px 0;overflow:hidden;box-shadow:0 10px 30px rgba(0,0,0,.25)}";
  html += ".section h2{font-size:18px;margin:0;padding:15px 16px;background:#111827;border-bottom:1px solid var(--line)}";
  html += ".section .body{padding:16px}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}";
  html += ".field{margin-bottom:14px}";
  html += "label{display:block;font-weight:700;margin-bottom:6px;font-size:14px}";
  html += "input,select{width:100%;padding:11px 12px;border-radius:12px;border:1px solid #475569;background:#020617;color:var(--txt);font-size:14px}";
  html += "input[type=checkbox]{width:auto;transform:scale(1.2);margin-right:8px}";
  html += ".help{font-size:12px;color:var(--muted);margin-top:5px;line-height:1.35}";
  html += ".readonly{padding:11px 12px;border-radius:12px;border:1px solid #334155;background:#020617;color:#cbd5e1;font-size:14px}";
  html += ".actions{display:flex;gap:10px;flex-wrap:wrap;margin:22px 0}";
  html += "button,.btn{appearance:none;border:0;border-radius:12px;padding:12px 16px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block}";
  html += "button.primary{background:var(--accent);color:#001018}";
  html += ".btn{background:#1e293b;color:var(--txt);border:1px solid #475569}";
  html += ".danger{background:#7f1d1d;color:#fee2e2}";
  html += ".pill{display:inline-block;border:1px solid #334155;border-radius:999px;padding:5px 9px;color:#cbd5e1;background:#020617;font-size:12px;margin:2px}";
  html += "@media(max-width:760px){.grid{grid-template-columns:1fr}header h1{font-size:19px}}";
  html += "</style>";

  html += "</head><body>";

  html += "<header>";
  html += "<h1>Victron VE.Direct ESP32 Monitor - Setup Wizard</h1>";
  html += "<p>" + publicHtmlEscape(pubCfg.firmwareVersion) + " · " + publicHtmlEscape(pubCfg.hardwareProfile) + "</p>";
  html += "</header>";

  html += "<main>";

  html += "<div class='notice'>";
  html += "<b>Public edition.</b> This wizard configures runtime settings such as VE.Direct pin, battery ADC, WiFiManager, OTA, plant data, logging and WebUI. ";
  html += "Display/touch/SPI are compile-time target settings and are not changed here.";
  html += "</div>";

  html += "<form method='POST' action='/setup-save'>";

  // Hardware section
  String hardwareBody;
  hardwareBody += "<div class='grid'>";
  hardwareBody += "<div class='field'><label>Firmware</label><div class='readonly'>" + publicHtmlEscape(pubCfg.firmwareName) + " " + publicHtmlEscape(pubCfg.firmwareVersion) + "</div></div>";
  hardwareBody += "<div class='field'><label>Hardware target</label><div class='readonly'>" + publicHtmlEscape(pubCfg.hardwareLabel) + "</div></div>";
  hardwareBody += "<div class='field'><label>Target profile</label><div class='readonly'>" + publicHtmlEscape(pubCfg.hardwareProfile) + "</div></div>";
  hardwareBody += "<div class='field'><label>Description</label><div class='readonly'>" + publicHtmlEscape(pubCfg.hardwareDescription) + "</div></div>";
  hardwareBody += "</div>";
  hardwareBody += "<p class='help'>To use another display or touch controller, flash the matching firmware target. Runtime pin changes below do not alter TFT/touch/SPI compile-time configuration.</p>";
  html += wizardSection("Hardware target", hardwareBody);

  // Pins
  String pinsBody;
  pinsBody += "<div class='grid'>";
  pinsBody += wizardInputNumber("veDirectRxPin", "VE.Direct RX GPIO", String(pubCfg.veDirectRxPin), "Default for CYD public target: GPIO27. Connect Victron VE.Direct TX to this ESP32 RX pin.", "1");
  pinsBody += wizardInputNumber("espBatteryAdcPin", "ESP battery ADC GPIO", String(pubCfg.espBatteryAdcPin), "Default for CYD public target: GPIO34. Use -1 to disable.", "1");
  pinsBody += wizardInputNumber("espBatteryMultiplier", "ESP battery voltage multiplier", String(pubCfg.espBatteryMultiplier, 3), "Voltage divider multiplier. CYD default around 2.14.", "0.001");
  pinsBody += wizardInputNumber("shutdownPin", "Shutdown / relay GPIO", String(pubCfg.shutdownPin), "Optional external shutdown/relay pin. Use -1 to disable.", "1");
  pinsBody += wizardInputNumber("statusLedPin", "Status LED GPIO", String(pubCfg.statusLedPin), "Optional status LED pin. Use -1 to disable.", "1");
  pinsBody += "</div>";
  html += wizardSection("Runtime pins", pinsBody);

  // Network
  String netBody;
  netBody += "<div class='grid'>";
  netBody += wizardInputText("hostname", "Hostname", pubCfg.hostname, "Example: victron-esp32-monitor");
  netBody += wizardInputText("setupApSsid", "WiFiManager setup AP SSID", pubCfg.setupApSsid, "Fallback AP name used when WiFi is not configured.");
  netBody += wizardInputText("setupApPassword", "WiFiManager setup AP password", pubCfg.setupApPassword, "Minimum 8 characters recommended.");
  netBody += "</div>";
  html += wizardSection("Network / WiFiManager", netBody);

  // OTA
  String otaBody;
  otaBody += wizardCheckbox("otaEnabled", "Enable OTA updates", pubCfg.otaEnabled, "OTA URLs are configurable and should point to your public OTA repository.");
  otaBody += "<div class='grid'>";
  otaBody += wizardInputText("otaChannel", "OTA channel", pubCfg.otaChannel, "Example: stable, beta, custom");
  otaBody += wizardInputText("otaVersionUrl", "OTA version URL", pubCfg.otaVersionUrl, "Raw URL to version.txt");
  otaBody += wizardInputText("otaBinUrl", "OTA binary URL", pubCfg.otaBinUrl, "Raw URL to latest.bin");
  otaBody += wizardInputText("otaSha256Url", "OTA SHA256 URL", pubCfg.otaSha256Url, "Raw URL to latest.sha256");
  otaBody += "</div>";
  html += wizardSection("OTA update", otaBody);

  // Plant
  String plantBody;
  plantBody += "<div class='grid'>";
  plantBody += wizardInputText("plantName", "Plant name", pubCfg.plantName, "Example: Camper, Boat, Solar shed");
  plantBody += wizardInputText("batteryName", "Battery name", pubCfg.batteryName, "Example: Service Battery");
  plantBody += wizardInputText("batteryType", "Battery type", pubCfg.batteryType, "Example: AGM, GEL, Lead Acid, LiFePO4, Custom");
  plantBody += wizardInputNumber("systemVoltage", "System voltage", String(pubCfg.systemVoltage, 2), "Example: 12, 24, 48", "0.1");
  plantBody += wizardInputNumber("batteryCapacityAh", "Battery capacity Ah", String(pubCfg.batteryCapacityAh, 2), "Example: 75", "0.1");
  plantBody += wizardInputNumber("panelWatts", "Solar panel watts", String(pubCfg.panelWatts, 2), "Example: 100", "0.1");
  plantBody += "</div>";
  html += wizardSection("Plant / battery", plantBody);

  // Thresholds
  String threshBody;
  threshBody += "<div class='grid'>";
  threshBody += wizardInputNumber("batLowV", "Battery low voltage", String(pubCfg.batLowV, 2), "Used for WebUI labels/health logic.", "0.01");
  threshBody += wizardInputNumber("batMediumV", "Battery medium voltage", String(pubCfg.batMediumV, 2), "Used for WebUI labels/health logic.", "0.01");
  threshBody += wizardInputNumber("batFullV", "Battery full voltage", String(pubCfg.batFullV, 2), "Used for WebUI labels/health logic.", "0.01");
  threshBody += "</div>";
  html += wizardSection("Battery thresholds", threshBody);

  // Logging / backup
  String logBody;
  logBody += "<div class='grid'>";
  logBody += wizardCheckbox("sdLoggingEnabled", "Enable SD logging", pubCfg.sdLoggingEnabled, "Target must support SD hardware.");
  logBody += wizardCheckbox("lfsLoggingEnabled", "Enable LittleFS logging", pubCfg.lfsLoggingEnabled, "Recommended for history and small config backups.");
  logBody += wizardCheckbox("jsonPretty", "Pretty JSON output", pubCfg.jsonPretty, "Makes JSON easier to read.");
  logBody += wizardInputNumber("backupConfigMax", "Max config backups", String(pubCfg.backupConfigMax), "Default: 5", "1");
  logBody += wizardInputNumber("backupHistoryMax", "Max history backups", String(pubCfg.backupHistoryMax), "Default: 5", "1");
  logBody += "</div>";
  html += wizardSection("Logging / backup", logBody);

  // WebUI
  String webBody;
  webBody += "<div class='grid'>";
  webBody += wizardInputText("language", "Language", pubCfg.language, "Example: en, it");
  webBody += wizardInputText("theme", "Theme", pubCfg.theme, "Example: dark, light, auto");
  webBody += wizardInputNumber("dashRefreshMs", "Dashboard refresh ms", String(pubCfg.dashRefreshMs), "Default: 3000", "100");
  webBody += wizardInputNumber("historyRefreshMs", "History refresh ms", String(pubCfg.historyRefreshMs), "Default: 10000", "100");
  webBody += wizardCheckbox("popupsEnabled", "Enable dashboard/history popups", pubCfg.popupsEnabled, "");
  webBody += wizardCheckbox("showEspBattery", "Show ESP battery", pubCfg.showEspBattery, "");
  webBody += wizardCheckbox("showVeDirectDebug", "Show VE.Direct debug", pubCfg.showVeDirectDebug, "");
  webBody += "</div>";
  html += wizardSection("WebUI", webBody);

  // Shutdown
  String shutBody;
  shutBody += "<div class='grid'>";
  shutBody += wizardCheckbox("shutdownEnabled", "Enable software shutdown controls", pubCfg.shutdownEnabled, "");
  shutBody += wizardInputNumber("shutdownTimerSec", "Default shutdown timer seconds", String(pubCfg.shutdownTimerSec), "Example: 60, 120, 180, 300", "1");
  shutBody += wizardCheckbox("shutdownConfirm", "Require shutdown confirmation", pubCfg.shutdownConfirm, "");
  shutBody += "</div>";
  html += wizardSection("Shutdown", shutBody);

  // Safety
  String safeBody;
  safeBody += "<div class='grid'>";
  safeBody += wizardCheckbox("allowFactoryReset", "Allow factory reset from WebUI", pubCfg.allowFactoryReset, "");
  safeBody += wizardCheckbox("requireSetupOnFirstBoot", "Require setup wizard on first boot", pubCfg.requireSetupOnFirstBoot, "");
  safeBody += "</div>";
  html += wizardSection("Safety", safeBody);

  html += "<div class='actions'>";
  html += "<button class='primary' type='submit'>Save setup</button>";
  html += "<a class='btn' href='/setup-json'>View config JSON</a>";
  html += "<a class='btn' href='/'>Dashboard</a>";
  html += "<a class='btn danger' href='/setup-reset' onclick=\"return confirm('Reset all public configuration to defaults?')\">Factory reset config</a>";
  html += "</div>";

  html += "</form>";

  html += "<div class='notice'>";
  html += "<b>Wiring default for CYD public target:</b> VE.Direct TX → ESP32 GPIO27, VE.Direct GND → ESP32 GND. ";
  html += "Do not power the ESP32 from VE.Direct 5V unless your hardware design explicitly supports it.";
  html += "</div>";

  html += "</main>";
  html += "</body></html>";

  return html;
}

// ======================================================
// HTML helpers
// ======================================================

String wizardInputText(
  const String &name,
  const String &label,
  const String &value,
  const String &help
) {
  String html;
  html += "<div class='field'>";
  html += "<label for='" + publicHtmlEscape(name) + "'>" + publicHtmlEscape(label) + "</label>";
  html += "<input id='" + publicHtmlEscape(name) + "' name='" + publicHtmlEscape(name) + "' type='text' value='" + publicHtmlEscape(value) + "'>";
  if (help.length()) html += "<div class='help'>" + publicHtmlEscape(help) + "</div>";
  html += "</div>";
  return html;
}

String wizardInputNumber(
  const String &name,
  const String &label,
  const String &value,
  const String &help,
  const String &step
) {
  String html;
  html += "<div class='field'>";
  html += "<label for='" + publicHtmlEscape(name) + "'>" + publicHtmlEscape(label) + "</label>";
  html += "<input id='" + publicHtmlEscape(name) + "' name='" + publicHtmlEscape(name) + "' type='number' step='" + publicHtmlEscape(step) + "' value='" + publicHtmlEscape(value) + "'>";
  if (help.length()) html += "<div class='help'>" + publicHtmlEscape(help) + "</div>";
  html += "</div>";
  return html;
}

String wizardCheckbox(
  const String &name,
  const String &label,
  bool checked,
  const String &help
) {
  String html;
  html += "<div class='field'>";
  html += "<label>";
  html += "<input name='" + publicHtmlEscape(name) + "' type='checkbox' " + publicBoolChecked(checked) + ">";
  html += publicHtmlEscape(label);
  html += "</label>";
  if (help.length()) html += "<div class='help'>" + publicHtmlEscape(help) + "</div>";
  html += "</div>";
  return html;
}

String wizardSelectBool(
  const String &name,
  const String &label,
  bool value,
  const String &help
) {
  String html;
  html += "<div class='field'>";
  html += "<label for='" + publicHtmlEscape(name) + "'>" + publicHtmlEscape(label) + "</label>";
  html += "<select id='" + publicHtmlEscape(name) + "' name='" + publicHtmlEscape(name) + "'>";
  html += "<option value='1' " + publicBoolSelected(value, true) + ">Enabled</option>";
  html += "<option value='0' " + publicBoolSelected(value, false) + ">Disabled</option>";
  html += "</select>";
  if (help.length()) html += "<div class='help'>" + publicHtmlEscape(help) + "</div>";
  html += "</div>";
  return html;
}

String wizardSection(
  const String &title,
  const String &body
) {
  String html;
  html += "<section class='section'>";
  html += "<h2>" + publicHtmlEscape(title) + "</h2>";
  html += "<div class='body'>";
  html += body;
  html += "</div>";
  html += "</section>";
  return html;
}
