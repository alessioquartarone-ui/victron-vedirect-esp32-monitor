#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "public_config.h"

/*
  Public setup wizard routes.

  This module adds:
  - /setup
  - /setup-save
  - /setup-reset
  - /setup-json

  It does not touch TFT/touch/SPI/display code.
*/

// Register all setup wizard routes on the existing WebServer instance.
void registerPublicWizardRoutes(WebServer &server);

// Optional helper for redirecting first boot to setup.
bool publicWizardShouldRedirectToSetup();

// HTML page builder.
String buildPublicWizardHtml();

// HTTP handlers.
void handlePublicSetup(WebServer &server);
void handlePublicSetupSave(WebServer &server);
void handlePublicSetupReset(WebServer &server);
void handlePublicSetupJson(WebServer &server);

// Small HTML helpers.
String wizardInputText(
  const String &name,
  const String &label,
  const String &value,
  const String &help = ""
);

String wizardInputPassword(
  const String &name,
  const String &label,
  const String &value,
  const String &help = ""
);

String wizardInputNumber(
  const String &name,
  const String &label,
  const String &value,
  const String &help = "",
  const String &step = "1"
);

String wizardCheckbox(
  const String &name,
  const String &label,
  bool checked,
  const String &help = ""
);

String wizardProtocolSelect();

String wizardSelectBool(
  const String &name,
  const String &label,
  bool value,
  const String &help = ""
);

String wizardSection(
  const String &title,
  const String &body
);
