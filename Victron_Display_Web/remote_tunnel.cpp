#include "remote_tunnel.h"

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#if __has_include("protocol_epever.h")
  #include "protocol_epever.h"
#endif

RemoteTunnelState tunnelState;

// ======================================================
// Internal helpers
// ======================================================

static bool tunnelUrlIsHttps(const String &url) {
  return url.startsWith("https://");
}

static bool tunnelUrlIsHttp(const String &url) {
  return url.startsWith("http://");
}

static String tunnelCleanBaseUrl(String url) {
  url.trim();

  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }

  return url;
}

static bool tunnelHttpGet(const String &url, String &response, int &httpCode) {
  response = "";
  httpCode = -1;

  if (WiFi.status() != WL_CONNECTED) {
    response = "WiFi not connected";
    return false;
  }

  HTTPClient http;

  if (tunnelUrlIsHttps(url)) {
    WiFiClientSecure client;
    client.setInsecure();

    if (!http.begin(client, url)) {
      response = "HTTP begin HTTPS failed";
      return false;
    }

    http.setTimeout(8000);
    http.addHeader("X-Device-Id", remoteTunnelDeviceId());
    http.addHeader("X-Device-Token", remoteTunnelToken());
    http.addHeader("User-Agent", "VictronESP32Tunnel/1.0");

    httpCode = http.GET();
    response = http.getString();
    http.end();

    return httpCode > 0 && httpCode < 500;
  }

  if (tunnelUrlIsHttp(url)) {
    WiFiClient client;

    if (!http.begin(client, url)) {
      response = "HTTP begin HTTP failed";
      return false;
    }

    http.setTimeout(8000);
    http.addHeader("X-Device-Id", remoteTunnelDeviceId());
    http.addHeader("X-Device-Token", remoteTunnelToken());
    http.addHeader("User-Agent", "VictronESP32Tunnel/1.0");

    httpCode = http.GET();
    response = http.getString();
    http.end();

    return httpCode > 0 && httpCode < 500;
  }

  response = "Invalid tunnel URL";
  return false;
}

static bool tunnelHttpPostJson(const String &url, const String &jsonBody, String &response, int &httpCode) {
  response = "";
  httpCode = -1;

  if (WiFi.status() != WL_CONNECTED) {
    response = "WiFi not connected";
    return false;
  }

  HTTPClient http;

  if (tunnelUrlIsHttps(url)) {
    WiFiClientSecure client;
    client.setInsecure();

    if (!http.begin(client, url)) {
      response = "HTTP begin HTTPS failed";
      return false;
    }

    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Id", remoteTunnelDeviceId());
    http.addHeader("X-Device-Token", remoteTunnelToken());
    http.addHeader("User-Agent", "VictronESP32Tunnel/1.0");

    httpCode = http.POST(jsonBody);
    response = http.getString();
    http.end();

    return httpCode > 0 && httpCode < 500;
  }

  if (tunnelUrlIsHttp(url)) {
    WiFiClient client;

    if (!http.begin(client, url)) {
      response = "HTTP begin HTTP failed";
      return false;
    }

    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Id", remoteTunnelDeviceId());
    http.addHeader("X-Device-Token", remoteTunnelToken());
    http.addHeader("User-Agent", "VictronESP32Tunnel/1.0");

    httpCode = http.POST(jsonBody);
    response = http.getString();
    http.end();

    return httpCode > 0 && httpCode < 500;
  }

  response = "Invalid tunnel URL";
  return false;
}

static String tunnelBasicRemoteDashboardPage() {
  String html;
  html.reserve(6000);

  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='10'>";
  html += "<title>Victron ESP32 Remote WebUI</title>";
  html += "<style>";
  html += "body{margin:0;background:#020617;color:#e5e7eb;font-family:Arial,Helvetica,sans-serif}";
  html += "header{padding:18px;background:#0f172a;border-bottom:1px solid #334155}";
  html += "main{max-width:900px;margin:0 auto;padding:18px}";
  html += ".card{background:#111827;border:1px solid #334155;border-radius:16px;padding:16px;margin:14px 0}";
  html += "a{color:#38bdf8;text-decoration:none;font-weight:700}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}";
  html += ".kv{background:#020617;border:1px solid #334155;border-radius:12px;padding:12px}";
  html += ".k{color:#94a3b8;font-size:12px}.v{font-size:18px;margin-top:4px}";
  html += "@media(max-width:700px){.grid{grid-template-columns:1fr}}";
  html += "</style>";
  html += "</head><body>";

  html += "<header>";
  html += "<h2 style='margin:0'>Victron ESP32 Remote WebUI Tunnel</h2>";
  html += "<div style='color:#94a3b8;margin-top:4px'>";
  html += publicHtmlEscape(pubCfg.firmwareVersion);
  html += " · ";
  html += publicHtmlEscape(pubCfg.hardwareProfile);
  html += "</div>";
  html += "</header>";

  html += "<main>";

  html += "<div class='card'>";
  html += "<h3>Remote tunnel</h3>";
  html += "<div class='grid'>";
  html += "<div class='kv'><div class='k'>Enabled</div><div class='v'>";
  html += pubCfg.tunnelEnabled ? "Yes" : "No";
  html += "</div></div>";
  html += "<div class='kv'><div class='k'>Online</div><div class='v'>";
  html += remoteTunnelIsOnline() ? "Yes" : "No";
  html += "</div></div>";
  html += "<div class='kv'><div class='k'>Device ID</div><div class='v'>";
  html += publicHtmlEscape(pubCfg.tunnelDeviceId);
  html += "</div></div>";
  html += "<div class='kv'><div class='k'>Mode</div><div class='v'>";
  html += pubCfg.tunnelReadOnly ? "Read-only" : "Extended";
  html += "</div></div>";
  html += "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Useful remote paths</h3>";
  html += "<p><a href='/setup-json'>/setup-json</a></p>";
  html += "<p><a href='/tunnel-status'>/tunnel-status</a></p>";
  html += "<p><a href='/tunnel-status.txt'>/tunnel-status.txt</a></p>";
  html += "<p><a href='/epever-json'>/epever-json</a></p>";
  html += "<p><a href='/epever-status'>/epever-status</a></p>";
  html += "<p><a href='/json'>/json</a></p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Configuration summary</h3>";
  html += "<pre style='white-space:pre-wrap;color:#cbd5e1'>";
  html += publicHtmlEscape(publicConfigSummaryText());
  html += "</pre>";
  html += "</div>";

  html += "</main></body></html>";

  return html;
}

// ======================================================
// Init / loop
// ======================================================

void remoteTunnelBegin() {
  tunnelState.enabled = pubCfg.tunnelEnabled;
  tunnelState.connected = false;
  tunnelState.lastPollOk = false;

  tunnelState.lastPollMs = 0;
  tunnelState.lastOkMs = 0;
  tunnelState.pollCount = 0;
  tunnelState.requestCount = 0;
  tunnelState.blockedCount = 0;
  tunnelState.errorCount = 0;

  tunnelState.lastError = "";
  tunnelState.lastRequestId = "";
  tunnelState.lastPath = "";
  tunnelState.lastMethod = "";
  tunnelState.lastHttpCode = 0;
}

void remoteTunnelLoop() {
  if (!remoteTunnelIsEnabled()) return;
  if (WiFi.status() != WL_CONNECTED) {
    tunnelState.connected = false;
    tunnelState.lastPollOk = false;
    tunnelState.lastError = "WiFi not connected";
    return;
  }

  uint32_t now = millis();
  uint32_t intervalMs = pubCfg.tunnelPollMs;
  if (intervalMs < 1000) intervalMs = 1000;

  if (now - tunnelState.lastPollMs < intervalMs) return;

  remoteTunnelPollServer();
}

// ======================================================
// Status
// ======================================================

bool remoteTunnelIsEnabled() {
  if (!pubCfg.tunnelEnabled) return false;
  if (pubCfg.tunnelServerUrl.length() == 0) return false;
  if (pubCfg.tunnelDeviceId.length() == 0) return false;
  if (pubCfg.tunnelDeviceToken.length() == 0) return false;
  return true;
}

bool remoteTunnelIsOnline() {
  if (!remoteTunnelIsEnabled()) return false;
  if (!tunnelState.connected) return false;
  if (!tunnelState.lastPollOk) return false;
  if (millis() - tunnelState.lastOkMs > 30000) return false;
  return true;
}

String remoteTunnelStatusJson(bool pretty) {
  String nl = pretty ? "\n" : "";
  String sp = pretty ? "  " : "";

  String json;
  json.reserve(2500);

  json += "{" + nl;
  json += sp + "\"enabled\":" + String(pubCfg.tunnelEnabled ? "true" : "false") + "," + nl;
  json += sp + "\"configured\":" + String(remoteTunnelIsEnabled() ? "true" : "false") + "," + nl;
  json += sp + "\"online\":" + String(remoteTunnelIsOnline() ? "true" : "false") + "," + nl;
  json += sp + "\"serverUrl\":\"" + remoteTunnelJsonEscape(pubCfg.tunnelServerUrl) + "\"," + nl;
  json += sp + "\"deviceId\":\"" + remoteTunnelJsonEscape(pubCfg.tunnelDeviceId) + "\"," + nl;
  json += sp + "\"tokenConfigured\":" + String(pubCfg.tunnelDeviceToken.length() > 0 ? "true" : "false") + "," + nl;
  json += sp + "\"readOnly\":" + String(pubCfg.tunnelReadOnly ? "true" : "false") + "," + nl;
  json += sp + "\"allowRemoteSetup\":" + String(pubCfg.tunnelAllowRemoteSetup ? "true" : "false") + "," + nl;
  json += sp + "\"allowDangerous\":" + String(pubCfg.tunnelAllowDangerous ? "true" : "false") + "," + nl;
  json += sp + "\"pollMs\":" + String(pubCfg.tunnelPollMs) + "," + nl;
  json += sp + "\"maxResponseBytes\":" + String(pubCfg.tunnelMaxResponseBytes) + "," + nl;
  json += sp + "\"pollCount\":" + String(tunnelState.pollCount) + "," + nl;
  json += sp + "\"requestCount\":" + String(tunnelState.requestCount) + "," + nl;
  json += sp + "\"blockedCount\":" + String(tunnelState.blockedCount) + "," + nl;
  json += sp + "\"errorCount\":" + String(tunnelState.errorCount) + "," + nl;
  json += sp + "\"lastHttpCode\":" + String(tunnelState.lastHttpCode) + "," + nl;
  json += sp + "\"lastRequestId\":\"" + remoteTunnelJsonEscape(tunnelState.lastRequestId) + "\"," + nl;
  json += sp + "\"lastMethod\":\"" + remoteTunnelJsonEscape(tunnelState.lastMethod) + "\"," + nl;
  json += sp + "\"lastPath\":\"" + remoteTunnelJsonEscape(tunnelState.lastPath) + "\"," + nl;
  json += sp + "\"lastOkMs\":" + String(tunnelState.lastOkMs) + "," + nl;
  json += sp + "\"lastError\":\"" + remoteTunnelJsonEscape(tunnelState.lastError) + "\"" + nl;
  json += "}" + nl;

  return json;
}

String remoteTunnelStatusText() {
  String out;

  out += "Remote WebUI Tunnel\n";
  out += "Enabled: " + String(pubCfg.tunnelEnabled ? "yes" : "no") + "\n";
  out += "Configured: " + String(remoteTunnelIsEnabled() ? "yes" : "no") + "\n";
  out += "Online: " + String(remoteTunnelIsOnline() ? "yes" : "no") + "\n";
  out += "Server URL: " + pubCfg.tunnelServerUrl + "\n";
  out += "Device ID: " + pubCfg.tunnelDeviceId + "\n";
  out += "Token configured: " + String(pubCfg.tunnelDeviceToken.length() > 0 ? "yes" : "no") + "\n";
  out += "Read-only: " + String(pubCfg.tunnelReadOnly ? "yes" : "no") + "\n";
  out += "Allow remote setup: " + String(pubCfg.tunnelAllowRemoteSetup ? "yes" : "no") + "\n";
  out += "Allow dangerous: " + String(pubCfg.tunnelAllowDangerous ? "yes" : "no") + "\n";
  out += "Poll ms: " + String(pubCfg.tunnelPollMs) + "\n";
  out += "Max response bytes: " + String(pubCfg.tunnelMaxResponseBytes) + "\n";
  out += "Poll count: " + String(tunnelState.pollCount) + "\n";
  out += "Request count: " + String(tunnelState.requestCount) + "\n";
  out += "Blocked count: " + String(tunnelState.blockedCount) + "\n";
  out += "Error count: " + String(tunnelState.errorCount) + "\n";
  out += "Last HTTP code: " + String(tunnelState.lastHttpCode) + "\n";
  out += "Last request ID: " + tunnelState.lastRequestId + "\n";
  out += "Last method: " + tunnelState.lastMethod + "\n";
  out += "Last path: " + tunnelState.lastPath + "\n";
  out += "Last error: " + tunnelState.lastError + "\n";

  return out;
}

// ======================================================
// Local HTTP routes
// ======================================================

void registerRemoteTunnelRoutes(WebServer &server) {
  server.on("/tunnel-status", HTTP_GET, [&server]() {
    handleTunnelStatusJson(server);
  });

  server.on("/tunnel-status.txt", HTTP_GET, [&server]() {
    handleTunnelStatusText(server);
  });
}

void handleTunnelStatusJson(WebServer &server) {
  server.send(200, "application/json", remoteTunnelStatusJson(true));
}

void handleTunnelStatusText(WebServer &server) {
  server.send(200, "text/plain", remoteTunnelStatusText());
}

// ======================================================
// Request safety
// ======================================================

bool remoteTunnelIsDangerousPath(const String &path) {
  String p = path;
  p.toLowerCase();

  if (p.indexOf("ota") >= 0) return true;
  if (p.indexOf("update") >= 0) return true;
  if (p.indexOf("upload") >= 0) return true;
  if (p.indexOf("reboot") >= 0) return true;
  if (p.indexOf("restart") >= 0) return true;
  if (p.indexOf("shutdown") >= 0) return true;
  if (p.indexOf("reset") >= 0) return true;
  if (p.indexOf("factory") >= 0) return true;
  if (p.indexOf("format") >= 0) return true;
  if (p.indexOf("delete") >= 0) return true;
  if (p.indexOf("erase") >= 0) return true;

  return false;
}

bool remoteTunnelIsSetupPath(const String &path) {
  String p = path;
  p.toLowerCase();

  if (p == "/setup") return true;
  if (p == "/setup-save") return true;
  if (p == "/setup-json") return true;
  if (p == "/setup-reset") return true;

  return false;
}

bool remoteTunnelPathAllowed(const String &method, const String &path) {
  String m = method;
  m.toUpperCase();

  if (path.length() == 0) return false;
  if (!path.startsWith("/")) return false;

  if (remoteTunnelIsDangerousPath(path) && !pubCfg.tunnelAllowDangerous) {
    return false;
  }

  if (remoteTunnelIsSetupPath(path)) {
    if (m == "GET" && path == "/setup-json") return true;
    if (m == "GET" && path == "/setup") return !pubCfg.tunnelReadOnly || pubCfg.tunnelAllowRemoteSetup;
    if (m == "POST") return pubCfg.tunnelAllowRemoteSetup && !pubCfg.tunnelReadOnly;
    if (path == "/setup-reset") return pubCfg.tunnelAllowDangerous;
  }

  if (m == "GET") {
    return true;
  }

  if (m == "POST") {
    if (pubCfg.tunnelReadOnly) return false;
    if (remoteTunnelIsDangerousPath(path) && !pubCfg.tunnelAllowDangerous) return false;
    return pubCfg.tunnelAllowRemoteSetup;
  }

  return false;
}

// ======================================================
// Local WebUI capture / proxy helpers
// ======================================================

String remoteTunnelHandleLocalRequest(const String &method, const String &path, const String &body, int &statusCode, String &contentType) {
  (void)body;

  String m = method;
  m.toUpperCase();

  statusCode = 200;
  contentType = "text/html";

  if (!remoteTunnelPathAllowed(m, path)) {
    statusCode = 403;
    contentType = "text/plain";
    tunnelState.blockedCount++;
    return "Remote tunnel blocked this request for safety: " + method + " " + path;
  }

  if (m != "GET") {
    statusCode = 405;
    contentType = "text/plain";
    return "Remote tunnel POC currently supports safe GET capture only.";
  }

  if (path == "/" || path == "/index" || path == "/dashboard") {
    contentType = "text/html";
    return tunnelBasicRemoteDashboardPage();
  }

  if (path == "/setup-json") {
    contentType = "application/json";
    return publicConfigToJson(true);
  }

  if (path == "/tunnel-status") {
    contentType = "application/json";
    return remoteTunnelStatusJson(true);
  }

  if (path == "/tunnel-status.txt") {
    contentType = "text/plain";
    return remoteTunnelStatusText();
  }

#if __has_include("protocol_epever.h")
  if (path == "/epever-json") {
    contentType = "application/json";
    return epeverJson(true);
  }

  if (path == "/epever-status") {
    contentType = "text/plain";
    return epeverStatusText();
  }
#endif

  if (path == "/setup") {
    if (pubCfg.tunnelReadOnly && !pubCfg.tunnelAllowRemoteSetup) {
      statusCode = 403;
      contentType = "text/plain";
      return "Remote setup page is blocked in read-only mode. Enable Allow remote setup in local wizard.";
    }

    contentType = "text/html";
    return buildPublicWizardHtml();
  }

  if (path == "/json") {
    contentType = "application/json";

    String json;
    json.reserve(1800);

    json += "{\n";
    json += "  \"remoteTunnel\":\"limited-poc\",\n";
    json += "  \"note\":\"Full local /json passthrough requires route adapter in main INO. Setup JSON and tunnel status are available now.\",\n";
    json += "  \"config\": ";
    json += publicConfigToJson(true);
    json += "\n}\n";

    return json;
  }

  statusCode = 404;
  contentType = "text/plain";
  return "Remote tunnel local route not available in POC: " + path;
}

// ======================================================
// Low-level server exchange
// ======================================================

bool remoteTunnelPollServer() {
  tunnelState.enabled = pubCfg.tunnelEnabled;
  tunnelState.lastPollMs = millis();
  tunnelState.pollCount++;

  if (!remoteTunnelIsEnabled()) {
    tunnelState.connected = false;
    tunnelState.lastPollOk = false;
    tunnelState.lastError = "Tunnel not fully configured";
    return false;
  }

  String url = remoteTunnelBaseUrl();
  url += "/api/device/poll";
  url += "?deviceId=" + remoteTunnelUrlEncode(remoteTunnelDeviceId());
  url += "&fw=" + remoteTunnelUrlEncode(pubCfg.firmwareVersion);

  String response;
  int httpCode = -1;

  bool ok = tunnelHttpGet(url, response, httpCode);
  tunnelState.lastHttpCode = httpCode;

  if (!ok) {
    tunnelState.connected = false;
    tunnelState.lastPollOk = false;
    tunnelState.errorCount++;
    tunnelState.lastError = response.length() ? response : "poll failed";
    return false;
  }

  if (httpCode == 204) {
    tunnelState.connected = true;
    tunnelState.lastPollOk = true;
    tunnelState.lastOkMs = millis();
    tunnelState.lastError = "ok/no-request";
    return true;
  }

  if (httpCode != 200) {
    tunnelState.connected = false;
    tunnelState.lastPollOk = false;
    tunnelState.errorCount++;
    tunnelState.lastError = "poll HTTP " + String(httpCode) + ": " + response;
    return false;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, response);

  if (err) {
    tunnelState.connected = false;
    tunnelState.lastPollOk = false;
    tunnelState.errorCount++;
    tunnelState.lastError = "JSON parse failed: " + String(err.c_str());
    return false;
  }

  bool hasRequest = doc["hasRequest"] | false;

  tunnelState.connected = true;
  tunnelState.lastPollOk = true;
  tunnelState.lastOkMs = millis();

  if (!hasRequest) {
    tunnelState.lastError = "ok/no-request";
    return true;
  }

  String requestId = doc["requestId"] | "";
  String method = doc["method"] | "GET";
  String path = doc["path"] | "/";
  String body = doc["body"] | "";

  tunnelState.requestCount++;
  tunnelState.lastRequestId = requestId;
  tunnelState.lastMethod = method;
  tunnelState.lastPath = path;

  int statusCode = 200;
  String contentType = "text/plain";
  String localResponse = remoteTunnelHandleLocalRequest(method, path, body, statusCode, contentType);

  if (pubCfg.tunnelMaxResponseBytes > 0 && localResponse.length() > pubCfg.tunnelMaxResponseBytes) {
    localResponse = localResponse.substring(0, pubCfg.tunnelMaxResponseBytes);
    localResponse += "\n<!-- Remote tunnel response truncated by safety limit -->\n";
  }

  return remoteTunnelSendResponse(requestId, statusCode, contentType, localResponse);
}

bool remoteTunnelSendResponse(
  const String &requestId,
  int statusCode,
  const String &contentType,
  const String &body
) {
  if (requestId.length() == 0) {
    tunnelState.errorCount++;
    tunnelState.lastError = "empty requestId";
    return false;
  }

  String url = remoteTunnelBaseUrl();
  url += "/api/device/response";

  String payload;
  payload.reserve(body.length() + 700);

  payload += "{";
  payload += "\"deviceId\":\"" + remoteTunnelJsonEscape(remoteTunnelDeviceId()) + "\",";
  payload += "\"requestId\":\"" + remoteTunnelJsonEscape(requestId) + "\",";
  payload += "\"statusCode\":" + String(statusCode) + ",";
  payload += "\"contentType\":\"" + remoteTunnelJsonEscape(contentType) + "\",";
  payload += "\"body\":\"" + remoteTunnelJsonEscape(body) + "\"";
  payload += "}";

  String response;
  int httpCode = -1;

  bool ok = tunnelHttpPostJson(url, payload, response, httpCode);
  tunnelState.lastHttpCode = httpCode;

  if (!ok || httpCode < 200 || httpCode >= 300) {
    tunnelState.errorCount++;
    tunnelState.lastError = "response POST failed HTTP " + String(httpCode) + ": " + response;
    return false;
  }

  tunnelState.lastError = "ok/response-sent";
  tunnelState.lastOkMs = millis();
  return true;
}

// ======================================================
// Small helpers
// ======================================================

String remoteTunnelBaseUrl() {
  return tunnelCleanBaseUrl(pubCfg.tunnelServerUrl);
}

String remoteTunnelDeviceId() {
  return pubCfg.tunnelDeviceId;
}

String remoteTunnelToken() {
  return pubCfg.tunnelDeviceToken;
}

String remoteTunnelUrlEncode(const String &value) {
  String out;
  char buf[4];

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);

    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }

  return out;
}

String remoteTunnelJsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 16);

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);

    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }

  return out;
}
