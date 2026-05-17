#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "public_config.h"
#include "public_wizard.h"

WebServer server(80);

void handleRoot() {
  if (publicWizardShouldRedirectToSetup()) {
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "Redirecting to setup wizard...");
    return;
  }

  String html;
  html.reserve(6000);

  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Victron ESP32 Monitor</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#020617;color:#e5e7eb;padding:24px}";
  html += ".box{background:#0f172a;border:1px solid #334155;border-radius:16px;padding:18px;max-width:760px}";
  html += "a{color:#38bdf8}";
  html += "code{background:#111827;padding:2px 6px;border-radius:6px}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='box'>";
  html += "<h1>Victron VE.Direct ESP32 Monitor</h1>";
  html += "<p><b>Firmware:</b> " + publicHtmlEscape(pubCfg.firmwareVersion) + "</p>";
  html += "<p><b>Target:</b> " + publicHtmlEscape(pubCfg.hardwareProfile) + "</p>";
  html += "<p><b>VE.Direct RX GPIO:</b> " + String(pubCfg.veDirectRxPin) + "</p>";
  html += "<p><b>ESP Battery ADC GPIO:</b> " + String(pubCfg.espBatteryAdcPin) + "</p>";
  html += "<p>This is the temporary public skeleton used to validate the build system and setup wizard.</p>";
  html += "<p><a href='/setup'>Open setup wizard</a></p>";
  html += "<p><a href='/setup-json'>View setup JSON</a></p>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleHealth() {
  server.send(200, "application/json", "{\"ok\":true,\"firmware\":\"" + pubCfg.firmwareVersion + "\"}");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  loadPublicConfig();

  WiFi.mode(WIFI_STA);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);

  registerPublicWizardRoutes(server);

  server.begin();

  Serial.println();
  Serial.println("Victron VE.Direct ESP32 Monitor public skeleton started");
  Serial.println(publicConfigSummaryText());
}

void loop() {
  server.handleClient();
  delay(2);
}
