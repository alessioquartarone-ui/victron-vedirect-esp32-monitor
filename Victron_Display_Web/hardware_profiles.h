#pragma once

/*
  Victron VE.Direct ESP32 Monitor
  Public hardware target profiles

  This file defines the supported hardware targets.

  Important:
  - Display, touch and SPI settings are compile-time target settings.
  - The setup wizard can safely change runtime pins such as VE.Direct RX,
    ESP battery ADC, shutdown pin and status LED pin.
  - TFT/touch/driver/SPI should not be changed from the WebUI wizard.

  First official public target:
  - ESP32 CYD 2.8" ILI9341
  - VE.Direct RX default: GPIO27
  - ESP battery ADC default: GPIO34
*/

// ======================================================
// Default target
// If no build flag is selected, compile for ESP32 CYD ILI9341.
// ======================================================

#if !defined(VIC_TARGET_CYD_ILI9341) && \
    !defined(VIC_TARGET_ESP32_HEADLESS) && \
    !defined(VIC_TARGET_ESP32S3_HEADLESS) && \
    !defined(VIC_TARGET_ESP32S3_ILI9341) && \
    !defined(VIC_TARGET_ESP32S3_ST7796)

  #define VIC_TARGET_CYD_ILI9341

#endif


// ======================================================
// Target 1:
// ESP32 CYD 2.8" ILI9341
//
// This is the main public target based on the private stable firmware.
// Display/touch/SPI must remain the same as the known working CYD build.
// ======================================================

#if defined(VIC_TARGET_CYD_ILI9341)

  #define VIC_TARGET_NAME                    "ESP32-CYD-ILI9341"
  #define VIC_TARGET_LABEL                   "ESP32 CYD 2.8 ILI9341"
  #define VIC_TARGET_DESCRIPTION             "ESP32 Cheap Yellow Display with ILI9341 TFT and XPT2046 touch"

  #define VIC_CHIP_FAMILY                    "ESP32"
  #define VIC_BOARD_CLASS                    "CYD"

  #define VIC_HAS_DISPLAY                    1
  #define VIC_HAS_TOUCH                      1
  #define VIC_HAS_SD                         1
  #define VIC_HAS_ESP_BATTERY_ADC            1
  #define VIC_HAS_BACKLIGHT                  1

  // Runtime defaults configurable by wizard
  #define VIC_DEFAULT_VEDIRECT_RX_PIN        27
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN        34
  #define VIC_DEFAULT_ESP_BAT_MULT           2.14f

  #define VIC_DEFAULT_SHUTDOWN_PIN           -1
  #define VIC_DEFAULT_STATUS_LED_PIN         -1

  // Display/touch are compile-time only.
  // Keep these aligned with the working TFT_eSPI setup.
  #define VIC_DISPLAY_DRIVER                 "ILI9341"
  #define VIC_TOUCH_DRIVER                   "XPT2046"
  #define VIC_DISPLAY_WIDTH                  240
  #define VIC_DISPLAY_HEIGHT                 320


// ======================================================
// Target 2:
// ESP32 classic Headless
//
// Universal target for generic ESP32 boards without display.
// WebUI only.
// ======================================================

#elif defined(VIC_TARGET_ESP32_HEADLESS)

  #define VIC_TARGET_NAME                    "ESP32-HEADLESS"
  #define VIC_TARGET_LABEL                   "ESP32 WebUI Only"
  #define VIC_TARGET_DESCRIPTION             "Generic ESP32 board without display, WebUI only"

  #define VIC_CHIP_FAMILY                    "ESP32"
  #define VIC_BOARD_CLASS                    "GENERIC"

  #define VIC_HAS_DISPLAY                    0
  #define VIC_HAS_TOUCH                      0
  #define VIC_HAS_SD                         0
  #define VIC_HAS_ESP_BATTERY_ADC            1
  #define VIC_HAS_BACKLIGHT                  0

  // Runtime defaults configurable by wizard
  #define VIC_DEFAULT_VEDIRECT_RX_PIN        16
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN        34
  #define VIC_DEFAULT_ESP_BAT_MULT           2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN           -1
  #define VIC_DEFAULT_STATUS_LED_PIN         2

  #define VIC_DISPLAY_DRIVER                 "NONE"
  #define VIC_TOUCH_DRIVER                   "NONE"
  #define VIC_DISPLAY_WIDTH                  0
  #define VIC_DISPLAY_HEIGHT                 0


// ======================================================
// Target 3:
// ESP32-S3 Headless
//
// Recommended modern WebUI-only target.
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_HEADLESS)

  #define VIC_TARGET_NAME                    "ESP32S3-HEADLESS"
  #define VIC_TARGET_LABEL                   "ESP32-S3 WebUI Only"
  #define VIC_TARGET_DESCRIPTION             "Generic ESP32-S3 board without display, WebUI only"

  #define VIC_CHIP_FAMILY                    "ESP32-S3"
  #define VIC_BOARD_CLASS                    "GENERIC"

  #define VIC_HAS_DISPLAY                    0
  #define VIC_HAS_TOUCH                      0
  #define VIC_HAS_SD                         0
  #define VIC_HAS_ESP_BATTERY_ADC            1
  #define VIC_HAS_BACKLIGHT                  0

  // Runtime defaults configurable by wizard
  #define VIC_DEFAULT_VEDIRECT_RX_PIN        18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN        4
  #define VIC_DEFAULT_ESP_BAT_MULT           2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN           -1
  #define VIC_DEFAULT_STATUS_LED_PIN         -1

  #define VIC_DISPLAY_DRIVER                 "NONE"
  #define VIC_TOUCH_DRIVER                   "NONE"
  #define VIC_DISPLAY_WIDTH                  0
  #define VIC_DISPLAY_HEIGHT                 0


// ======================================================
// Target 4:
// ESP32-S3 with external ILI9341
//
// Future display target.
// Requires matching TFT_eSPI/User_Setup or build flags.
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_ILI9341)

  #define VIC_TARGET_NAME                    "ESP32S3-ILI9341"
  #define VIC_TARGET_LABEL                   "ESP32-S3 ILI9341"
  #define VIC_TARGET_DESCRIPTION             "ESP32-S3 board with external ILI9341 TFT"

  #define VIC_CHIP_FAMILY                    "ESP32-S3"
  #define VIC_BOARD_CLASS                    "GENERIC_DISPLAY"

  #define VIC_HAS_DISPLAY                    1
  #define VIC_HAS_TOUCH                      0
  #define VIC_HAS_SD                         0
  #define VIC_HAS_ESP_BATTERY_ADC            1
  #define VIC_HAS_BACKLIGHT                  1

  // Runtime defaults configurable by wizard
  #define VIC_DEFAULT_VEDIRECT_RX_PIN        18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN        4
  #define VIC_DEFAULT_ESP_BAT_MULT           2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN           -1
  #define VIC_DEFAULT_STATUS_LED_PIN         -1

  // Display is compile-time only.
  #define VIC_DISPLAY_DRIVER                 "ILI9341"
  #define VIC_TOUCH_DRIVER                   "NONE"
  #define VIC_DISPLAY_WIDTH                  240
  #define VIC_DISPLAY_HEIGHT                 320


// ======================================================
// Target 5:
// ESP32-S3 with external ST7796
//
// Future larger display target.
// Requires matching TFT_eSPI/User_Setup or build flags.
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_ST7796)

  #define VIC_TARGET_NAME                    "ESP32S3-ST7796"
  #define VIC_TARGET_LABEL                   "ESP32-S3 ST7796"
  #define VIC_TARGET_DESCRIPTION             "ESP32-S3 board with external ST7796 TFT"

  #define VIC_CHIP_FAMILY                    "ESP32-S3"
  #define VIC_BOARD_CLASS                    "GENERIC_DISPLAY"

  #define VIC_HAS_DISPLAY                    1
  #define VIC_HAS_TOUCH                      0
  #define VIC_HAS_SD                         0
  #define VIC_HAS_ESP_BATTERY_ADC            1
  #define VIC_HAS_BACKLIGHT                  1

  // Runtime defaults configurable by wizard
  #define VIC_DEFAULT_VEDIRECT_RX_PIN        18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN        4
  #define VIC_DEFAULT_ESP_BAT_MULT           2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN           -1
  #define VIC_DEFAULT_STATUS_LED_PIN         -1

  // Display is compile-time only.
  #define VIC_DISPLAY_DRIVER                 "ST7796"
  #define VIC_TOUCH_DRIVER                   "NONE"
  #define VIC_DISPLAY_WIDTH                  320
  #define VIC_DISPLAY_HEIGHT                 480


#else

  #error "No valid VIC_TARGET selected in hardware_profiles.h"

#endif


// ======================================================
// Common validation
// ======================================================

#ifndef VIC_TARGET_NAME
  #error "VIC_TARGET_NAME not defined"
#endif

#ifndef VIC_DEFAULT_VEDIRECT_RX_PIN
  #error "VIC_DEFAULT_VEDIRECT_RX_PIN not defined"
#endif

#ifndef VIC_DEFAULT_ESP_BAT_ADC_PIN
  #error "VIC_DEFAULT_ESP_BAT_ADC_PIN not defined"
#endif

#ifndef VIC_DEFAULT_ESP_BAT_MULT
  #error "VIC_DEFAULT_ESP_BAT_MULT not defined"
#endif
