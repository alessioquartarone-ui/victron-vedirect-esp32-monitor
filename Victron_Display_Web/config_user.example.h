#pragma once

/*
  Victron VE.Direct ESP32 Monitor - Public user configuration example

  Optional:
  Copy this file to config_user.h if you want compile-time defaults.

  Do not commit config_user.h.
  Runtime settings saved by the setup wizard override these defaults.
*/

#include "hardware_profiles.h"
#include "protocol_profiles.h"

#define VIC_PUBLIC_BUILD 1
#define VIC_FW_NAME "Victron VE.Direct ESP32 Monitor"

#if defined(VIC_TARGET_CYD_ILI9341)
  #define VIC_FW_VERSION "V10.5.4-CYD-PUBLIC-REMOTE-WEBUI-TUNNEL"
#elif defined(VIC_TARGET_ESP32_HEADLESS)
  #define VIC_FW_VERSION "V10.5.4-ESP32-HEADLESS-REMOTE-WEBUI-TUNNEL"
#elif defined(VIC_TARGET_ESP32S3_HEADLESS)
  #define VIC_FW_VERSION "V10.5.4-ESP32S3-HEADLESS-REMOTE-WEBUI-TUNNEL"
#elif defined(VIC_TARGET_ESP32S3_ILI9341)
  #define VIC_FW_VERSION "V10.6.0-ESP32S3-ILI9341-PUBLIC-WIZARD"
#elif defined(VIC_TARGET_ESP32S3_ST7796)
  #define VIC_FW_VERSION "V10.6.0-ESP32S3-ST7796-PUBLIC-WIZARD"
#else
  #define VIC_FW_VERSION "V10.5.4-PUBLIC-REMOTE-WEBUI-TUNNEL"
#endif

#define VIC_DEFAULT_HARDWARE_PROFILE          VIC_TARGET_NAME
#define VIC_DEFAULT_HARDWARE_LABEL            VIC_TARGET_LABEL
#define VIC_DEFAULT_HARDWARE_DESCRIPTION      VIC_TARGET_DESCRIPTION

#define VIC_USER_DEFAULT_VEDIRECT_RX_PIN      VIC_DEFAULT_VEDIRECT_RX_PIN
#define VIC_USER_DEFAULT_ESP_BAT_ADC_PIN      VIC_DEFAULT_ESP_BAT_ADC_PIN
#define VIC_USER_DEFAULT_ESP_BAT_MULTIPLIER   VIC_DEFAULT_ESP_BAT_MULT
#define VIC_USER_DEFAULT_SHUTDOWN_PIN         VIC_DEFAULT_SHUTDOWN_PIN
#define VIC_USER_DEFAULT_STATUS_LED_PIN       VIC_DEFAULT_STATUS_LED_PIN

#define VIC_DEFAULT_HOSTNAME                  "victron-esp32-monitor"
#define VIC_DEFAULT_SETUP_AP_SSID             "Victron-ESP32-Setup"
#define VIC_DEFAULT_SETUP_AP_PASSWORD         "12345678"

#define VIC_DEFAULT_OTA_ENABLED               true
#define VIC_DEFAULT_OTA_CHANNEL               "stable"

#if defined(VIC_TARGET_CYD_ILI9341)
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256"
#elif defined(VIC_TARGET_ESP32_HEADLESS)
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/latest.sha256"
#elif defined(VIC_TARGET_ESP32S3_HEADLESS)
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/latest.sha256"
#elif defined(VIC_TARGET_ESP32S3_ILI9341)
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-ili9341/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-ili9341/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-ili9341/latest.sha256"
#elif defined(VIC_TARGET_ESP32S3_ST7796)
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-st7796/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-st7796/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-st7796/latest.sha256"
#else
  #define VIC_DEFAULT_OTA_VERSION_URL "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt"
  #define VIC_DEFAULT_OTA_BIN_URL     "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin"
  #define VIC_DEFAULT_OTA_SHA256_URL  "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256"
#endif

#define VIC_DEFAULT_PLANT_NAME                "My Victron System"
#define VIC_DEFAULT_BATTERY_NAME              "Service Battery"
#define VIC_DEFAULT_BATTERY_TYPE              "AGM"
#define VIC_DEFAULT_SYSTEM_VOLTAGE            12.0f
#define VIC_DEFAULT_BATTERY_CAPACITY_AH       75.0f
#define VIC_DEFAULT_PANEL_WATTS               100.0f

#define VIC_DEFAULT_BAT_LOW_V                 12.0f
#define VIC_DEFAULT_BAT_MEDIUM_V              12.3f
#define VIC_DEFAULT_BAT_FULL_V                12.7f

#define VIC_DEFAULT_SD_LOGGING_ENABLED        true
#define VIC_DEFAULT_LFS_LOGGING_ENABLED       true
#define VIC_DEFAULT_BACKUP_CONFIG_MAX         5
#define VIC_DEFAULT_BACKUP_HISTORY_MAX        5
#define VIC_DEFAULT_JSON_PRETTY               true

#define VIC_DEFAULT_LANGUAGE                  "en"
#define VIC_DEFAULT_THEME                     "dark"
#define VIC_DEFAULT_DASH_REFRESH_MS           3000
#define VIC_DEFAULT_HISTORY_REFRESH_MS        10000
#define VIC_DEFAULT_POPUPS_ENABLED            true
#define VIC_DEFAULT_SHOW_ESP_BATTERY          true
#define VIC_DEFAULT_SHOW_VEDIRECT_DEBUG       false

#define VIC_DEFAULT_SHUTDOWN_ENABLED          true
#define VIC_DEFAULT_SHUTDOWN_TIMER_SEC        180
#define VIC_DEFAULT_SHUTDOWN_CONFIRM          true
#define VIC_DEFAULT_ALLOW_FACTORY_RESET        true
#define VIC_DEFAULT_REQUIRE_SETUP_ON_FIRSTBOOT true

// EPEVER / RS485 Modbus defaults
#define VIC_DEFAULT_EPEVER_ENABLED             false
#define VIC_DEFAULT_EPEVER_SLAVE_ID            1
#define VIC_DEFAULT_EPEVER_BAUDRATE            115200
#define VIC_USER_DEFAULT_EPEVER_RX_PIN         VIC_DEFAULT_EPEVER_RX_PIN
#define VIC_USER_DEFAULT_EPEVER_TX_PIN         VIC_DEFAULT_EPEVER_TX_PIN
#define VIC_USER_DEFAULT_EPEVER_DERE_PIN       VIC_DEFAULT_EPEVER_DERE_PIN
#define VIC_DEFAULT_EPEVER_POLL_MS             2000
