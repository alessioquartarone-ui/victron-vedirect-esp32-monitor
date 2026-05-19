#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>

#include "public_config.h"

/*
  Remote WebUI Tunnel client - public POC

  Goal:
  - ESP32 opens outgoing HTTP polling connection to a tunnel server.
  - Server can queue safe remote WebUI requests.
  - ESP32 executes allowed local WebUI routes and returns content.
  - No Tailscale, no port forwarding, no special router.

  First safe implementation:
  - Poll-based tunnel, not permanent WebSocket yet.
  - Supports safe GET endpoints.
  - Blocks dangerous endpoints unless explicitly allowed.
  - Keeps original local ESP32 WebUI unchanged.
*/

struct RemoteTunnelState {
  bool enabled;
  bool connected;
  bool lastPollOk;

  uint32_t lastPollMs;
  uint32_t lastOkMs;
  uint32_t pollCount;
  uint32_t requestCount;
  uint32_t blockedCount;
  uint32_t errorCount;

  String lastError;
  String lastRequestId;
  String lastPath;
  String lastMethod;
  int lastHttpCode;
};

extern RemoteTunnelState tunnelState;

// Init / loop
void remoteTunnelBegin();
void remoteTunnelLoop();

// Status
bool remoteTunnelIsEnabled();
bool remoteTunnelIsOnline();
String remoteTunnelStatusJson(bool pretty = true);
String remoteTunnelStatusText();

// Local HTTP routes
void registerRemoteTunnelRoutes(WebServer &server);
void handleTunnelStatusJson(WebServer &server);
void handleTunnelStatusText(WebServer &server);

// Request safety
bool remoteTunnelPathAllowed(const String &method, const String &path);
bool remoteTunnelIsDangerousPath(const String &path);
bool remoteTunnelIsSetupPath(const String &path);

// Local WebUI capture / proxy helpers
String remoteTunnelHandleLocalRequest(const String &method, const String &path, const String &body, int &statusCode, String &contentType);

// Low-level server exchange
bool remoteTunnelPollServer();
bool remoteTunnelSendResponse(
  const String &requestId,
  int statusCode,
  const String &contentType,
  const String &body
);

// Small helpers
String remoteTunnelBaseUrl();
String remoteTunnelDeviceId();
String remoteTunnelToken();
String remoteTunnelUrlEncode(const String &value);
String remoteTunnelJsonEscape(const String &value);
