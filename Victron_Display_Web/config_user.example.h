#pragma once

/*
  Victron VE.Direct ESP32 Monitor - Public user configuration example

  OPTIONAL:
  Copy this file to:

      config_user.h

  if you want to change compile-time defaults before building.

  IMPORTANT:
  - Do not commit config_user.h.
  - Do not put WiFi passwords, GitHub tokens or private data in the public repo.
  - Runtime settings saved by the setup wizard override these defaults.
*/

#include "hardware_profiles.h"

// ======================================================
// Firmware identity
// ======================================================

#define VIC_PUBLIC_BUILD                      1
#define VIC_FW_NAME                           "Victron VE.Direct ESP32 Monitor"
#define VIC_FW_VERSION                        "V10.5.0-CYD-PUBLIC-WIZARD"

// ======================================================
// Hardware defaults
// ======================================================

#define VIC_DEFAULT_HARDWARE_PROFILE          VIC_TARGET_NAME
#define VIC_DEFAULT_HARDWARE_LABEL            VIC_TARGET_LABEL
#define VIC_DEFAULT_HARDWARE_DESCRIPTION      VIC_TARGET_DESCRIPTION

#define VIC_USER_DEFAULT_VEDIRECT_RX_PIN      VIC_DEFAULT_VEDIRECT_RX_PIN
#define VIC_USER_DEFAULT_ESP_BAT_ADC_PIN      VIC_DEFAULT_ESP_BAT_ADC_PIN
#define VIC_USER_DEFAULT_ESP_BAT_MULTIPLIER   VIC_DEFAULT_ESP_BAT_MULT

#define VIC_USER_DEFAULT_SHUTDOWN_PIN         VIC_DEFAULT_SHUTDOWN_PIN
#define VIC_USER_DEFAULT_STATUS_LED_PIN       VIC_DEFAULT_STATUS_LED_PIN

// ======================================================
// WiFiManager defaults
// ======================================================

#define VIC_DEFAULT_HOSTNAME                  "victron-esp32-monitor"
#define VIC_DEFAULT_SETUP_AP_SSID             "Victron-ESP32-Setup"
#define VIC_DEFAULT_SETUP_AP_PASSWORD         "12345678"

// ======================================================
// OTA defaults - public OTA repository
// ======================================================

#define VIC_DEFAULT_OTA_ENABLED               true
#define VIC_DEFAULT_OTA_CHANNEL               "stable"

#define VIC_DEFAULT_OTA_VERSION_URL           "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt"
#define VIC_DEFAULT_OTA_BIN_URL               "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin"
#define VIC_DEFAULT_OTA_SHA256_URL            "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256"

// ======================================================
// Plant defaults
// ======================================================

#define VIC_DEFAULT_PLANT_NAME                "My Victron System"
#define VIC_DEFAULT_BATTERY_NAME              "Service Battery"
#define VIC_DEFAULT_BATTERY_TYPE              "AGM"

#define VIC_DEFAULT_SYSTEM_VOLTAGE            12.0f
#define VIC_DEFAULT_BATTERY_CAPACITY_AH       75.0f
#define VIC_DEFAULT_PANEL_WATTS               100.0f

// ======================================================
// Battery thresholds
// ======================================================

#define VIC_DEFAULT_BAT_LOW_V                 12.0f
#define VIC_DEFAULT_BAT_MEDIUM_V              12.3f
#define VIC_DEFAULT_BAT_FULL_V                12.7f

// ======================================================
// Logging / backup defaults
// ======================================================

#define VIC_DEFAULT_SD_LOGGING_ENABLED        true
#define VIC_DEFAULT_LFS_LOGGING_ENABLED       true

#define VIC_DEFAULT_BACKUP_CONFIG_MAX         5
#define VIC_DEFAULT_BACKUP_HISTORY_MAX        5

#define VIC_DEFAULT_JSON_PRETTY               true

// ======================================================
// WebUI defaults
// ======================================================

#define VIC_DEFAULT_LANGUAGE                  "en"
#define VIC_DEFAULT_THEME                     "dark"

#define VIC_DEFAULT_DASH_REFRESH_MS           3000
#define VIC_DEFAULT_HISTORY_REFRESH_MS        10000

#define VIC_DEFAULT_POPUPS_ENABLED            true
#define VIC_DEFAULT_SHOW_ESP_BATTERY          true
#define VIC_DEFAULT_SHOW_VEDIRECT_DEBUG       false

// ======================================================
// Shutdown defaults
// ======================================================

#define VIC_DEFAULT_SHUTDOWN_ENABLED          true
#define VIC_DEFAULT_SHUTDOWN_TIMER_SEC        180
#define VIC_DEFAULT_SHUTDOWN_CONFIRM          true

// ======================================================
// Advanced safety defaults
// ======================================================

#define VIC_DEFAULT_ALLOW_FACTORY_RESET        true
#define VIC_DEFAULT_REQUIRE_SETUP_ON_FIRSTBOOT true
