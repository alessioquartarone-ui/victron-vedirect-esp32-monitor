#pragma once

/*
  Victron VE.Direct ESP32 Monitor
  Public hardware target profiles

  Compile-time targets:

  - VIC_TARGET_CYD_ILI9341
  - VIC_TARGET_ESP32_HEADLESS
  - VIC_TARGET_ESP32S3_HEADLESS
  - VIC_TARGET_ESP32S3_ILI9341
  - VIC_TARGET_ESP32S3_ST7796

  Display/touch/SPI pins are compile-time settings.
  Runtime pins such as VE.Direct RX, ESP battery ADC, RS485 RX/TX/DE,
  shutdown and status LED are saved by the setup wizard.
*/

// ======================================================
// Target selection fallback
// ======================================================

#if !defined(VIC_TARGET_CYD_ILI9341) && \
    !defined(VIC_TARGET_ESP32_HEADLESS) && \
    !defined(VIC_TARGET_ESP32S3_HEADLESS) && \
    !defined(VIC_TARGET_ESP32S3_ILI9341) && \
    !defined(VIC_TARGET_ESP32S3_ST7796)

  #define VIC_TARGET_CYD_ILI9341

#endif

// ======================================================
// ESP32 CYD / Cheap Yellow Display / ILI9341
// ======================================================

#if defined(VIC_TARGET_CYD_ILI9341)

  #define VIC_TARGET_NAME              "ESP32-CYD-ILI9341"
  #define VIC_TARGET_LABEL             "ESP32 CYD ILI9341"
  #define VIC_TARGET_DESCRIPTION       "ESP32 Cheap Yellow Display 2.8 inch ILI9341 240x320 with XPT2046 touch"

  #define VIC_HAS_DISPLAY              1
  #define VIC_HAS_TOUCH                1
  #define VIC_HAS_SD                   1
  #define VIC_HAS_ESP_BATTERY_ADC      1
  #define VIC_HAS_RS485_DEFAULT        1

  // Confirmed stable private/public CYD wiring
  #define VIC_DEFAULT_VEDIRECT_RX_PIN  27
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  34
  #define VIC_DEFAULT_ESP_BAT_MULT     2.14f

  // Optional runtime pins
  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
  #define VIC_DEFAULT_STATUS_LED_PIN   -1

  // EPEVER / RS485 defaults for CYD
  // RX/TX are runtime configurable from wizard.
  // DE/RE can be -1 if using automatic-direction RS485 module.
  #define VIC_DEFAULT_EPEVER_RX_PIN    27
  #define VIC_DEFAULT_EPEVER_TX_PIN    26
  #define VIC_DEFAULT_EPEVER_DERE_PIN  22

  // TFT compile-time information, used for docs/status only
  #define VIC_DISPLAY_DRIVER           "ILI9341"
  #define VIC_DISPLAY_WIDTH            240
  #define VIC_DISPLAY_HEIGHT           320
  #define VIC_TOUCH_DRIVER             "XPT2046"

  #define VIC_TFT_MISO                 12
  #define VIC_TFT_MOSI                 13
  #define VIC_TFT_SCLK                 14
  #define VIC_TFT_CS                   15
  #define VIC_TFT_DC                   2
  #define VIC_TFT_RST                  -1
  #define VIC_TFT_BL                   21

// ======================================================
// Generic classic ESP32 headless
// ======================================================

#elif defined(VIC_TARGET_ESP32_HEADLESS)

  #define VIC_TARGET_NAME              "ESP32-HEADLESS"
  #define VIC_TARGET_LABEL             "ESP32 Headless WebUI"
  #define VIC_TARGET_DESCRIPTION       "Generic classic ESP32 board, WebUI only, no display required"

  #define VIC_HAS_DISPLAY              0
  #define VIC_HAS_TOUCH                0
  #define VIC_HAS_SD                   0
  #define VIC_HAS_ESP_BATTERY_ADC      1
  #define VIC_HAS_RS485_DEFAULT        1

  #define VIC_DEFAULT_VEDIRECT_RX_PIN  16
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  34
  #define VIC_DEFAULT_ESP_BAT_MULT     2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
  #define VIC_DEFAULT_STATUS_LED_PIN   2

  // EPEVER / RS485 defaults for classic ESP32
  #define VIC_DEFAULT_EPEVER_RX_PIN    16
  #define VIC_DEFAULT_EPEVER_TX_PIN    17
  #define VIC_DEFAULT_EPEVER_DERE_PIN  4

  #define VIC_DISPLAY_DRIVER           "none"
  #define VIC_DISPLAY_WIDTH            0
  #define VIC_DISPLAY_HEIGHT           0
  #define VIC_TOUCH_DRIVER             "none"

// ======================================================
// ESP32-S3 headless recommended target
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_HEADLESS)

  #define VIC_TARGET_NAME              "ESP32S3-HEADLESS"
  #define VIC_TARGET_LABEL             "ESP32-S3 Headless WebUI"
  #define VIC_TARGET_DESCRIPTION       "Recommended generic ESP32-S3 N16R8 board, WebUI only, no display required"

  #define VIC_HAS_DISPLAY              0
  #define VIC_HAS_TOUCH                0
  #define VIC_HAS_SD                   0
  #define VIC_HAS_ESP_BATTERY_ADC      1
  #define VIC_HAS_RS485_DEFAULT        1

  #define VIC_DEFAULT_VEDIRECT_RX_PIN  18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  4
  #define VIC_DEFAULT_ESP_BAT_MULT     2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
  #define VIC_DEFAULT_STATUS_LED_PIN   2

  // EPEVER / RS485 defaults for ESP32-S3
  #define VIC_DEFAULT_EPEVER_RX_PIN    18
  #define VIC_DEFAULT_EPEVER_TX_PIN    17
  #define VIC_DEFAULT_EPEVER_DERE_PIN  16

  #define VIC_DISPLAY_DRIVER           "none"
  #define VIC_DISPLAY_WIDTH            0
  #define VIC_DISPLAY_HEIGHT           0
  #define VIC_TOUCH_DRIVER             "none"

// ======================================================
// Planned ESP32-S3 ILI9341 display target
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_ILI9341)

  #define VIC_TARGET_NAME              "ESP32S3-ILI9341"
  #define VIC_TARGET_LABEL             "ESP32-S3 ILI9341 Display"
  #define VIC_TARGET_DESCRIPTION       "Planned ESP32-S3 target with external ILI9341 display"

  #define VIC_HAS_DISPLAY              1
  #define VIC_HAS_TOUCH                0
  #define VIC_HAS_SD                   0
  #define VIC_HAS_ESP_BATTERY_ADC      1
  #define VIC_HAS_RS485_DEFAULT        1

  #define VIC_DEFAULT_VEDIRECT_RX_PIN  18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  4
  #define VIC_DEFAULT_ESP_BAT_MULT     2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
  #define VIC_DEFAULT_STATUS_LED_PIN   2

  #define VIC_DEFAULT_EPEVER_RX_PIN    18
  #define VIC_DEFAULT_EPEVER_TX_PIN    17
  #define VIC_DEFAULT_EPEVER_DERE_PIN  16

  #define VIC_DISPLAY_DRIVER           "ILI9341"
  #define VIC_DISPLAY_WIDTH            240
  #define VIC_DISPLAY_HEIGHT           320
  #define VIC_TOUCH_DRIVER             "optional"

// ======================================================
// Planned ESP32-S3 ST7796 display target
// ======================================================

#elif defined(VIC_TARGET_ESP32S3_ST7796)

  #define VIC_TARGET_NAME              "ESP32S3-ST7796"
  #define VIC_TARGET_LABEL             "ESP32-S3 ST7796 Display"
  #define VIC_TARGET_DESCRIPTION       "Planned ESP32-S3 target with external ST7796 display"

  #define VIC_HAS_DISPLAY              1
  #define VIC_HAS_TOUCH                0
  #define VIC_HAS_SD                   0
  #define VIC_HAS_ESP_BATTERY_ADC      1
  #define VIC_HAS_RS485_DEFAULT        1

  #define VIC_DEFAULT_VEDIRECT_RX_PIN  18
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  4
  #define VIC_DEFAULT_ESP_BAT_MULT     2.00f

  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
  #define VIC_DEFAULT_STATUS_LED_PIN   2

  #define VIC_DEFAULT_EPEVER_RX_PIN    18
  #define VIC_DEFAULT_EPEVER_TX_PIN    17
  #define VIC_DEFAULT_EPEVER_DERE_PIN  16

  #define VIC_DISPLAY_DRIVER           "ST7796"
  #define VIC_DISPLAY_WIDTH            320
  #define VIC_DISPLAY_HEIGHT           480
  #define VIC_TOUCH_DRIVER             "optional"

#endif

// ======================================================
// Safety fallback definitions
// ======================================================

#ifndef VIC_TARGET_NAME
  #define VIC_TARGET_NAME              "UNKNOWN"
#endif

#ifndef VIC_TARGET_LABEL
  #define VIC_TARGET_LABEL             "Unknown target"
#endif

#ifndef VIC_TARGET_DESCRIPTION
  #define VIC_TARGET_DESCRIPTION       "Unknown hardware target"
#endif

#ifndef VIC_HAS_DISPLAY
  #define VIC_HAS_DISPLAY              0
#endif

#ifndef VIC_HAS_TOUCH
  #define VIC_HAS_TOUCH                0
#endif

#ifndef VIC_HAS_SD
  #define VIC_HAS_SD                   0
#endif

#ifndef VIC_HAS_ESP_BATTERY_ADC
  #define VIC_HAS_ESP_BATTERY_ADC      0
#endif

#ifndef VIC_HAS_RS485_DEFAULT
  #define VIC_HAS_RS485_DEFAULT        0
#endif

#ifndef VIC_DEFAULT_VEDIRECT_RX_PIN
  #define VIC_DEFAULT_VEDIRECT_RX_PIN  27
#endif

#ifndef VIC_DEFAULT_ESP_BAT_ADC_PIN
  #define VIC_DEFAULT_ESP_BAT_ADC_PIN  34
#endif

#ifndef VIC_DEFAULT_ESP_BAT_MULT
  #define VIC_DEFAULT_ESP_BAT_MULT     2.14f
#endif

#ifndef VIC_DEFAULT_SHUTDOWN_PIN
  #define VIC_DEFAULT_SHUTDOWN_PIN     -1
#endif

#ifndef VIC_DEFAULT_STATUS_LED_PIN
  #define VIC_DEFAULT_STATUS_LED_PIN   -1
#endif

#ifndef VIC_DEFAULT_EPEVER_RX_PIN
  #define VIC_DEFAULT_EPEVER_RX_PIN    18
#endif

#ifndef VIC_DEFAULT_EPEVER_TX_PIN
  #define VIC_DEFAULT_EPEVER_TX_PIN    17
#endif

#ifndef VIC_DEFAULT_EPEVER_DERE_PIN
  #define VIC_DEFAULT_EPEVER_DERE_PIN  16
#endif

#ifndef VIC_DISPLAY_DRIVER
  #define VIC_DISPLAY_DRIVER           "none"
#endif

#ifndef VIC_DISPLAY_WIDTH
  #define VIC_DISPLAY_WIDTH            0
#endif

#ifndef VIC_DISPLAY_HEIGHT
  #define VIC_DISPLAY_HEIGHT           0
#endif

#ifndef VIC_TOUCH_DRIVER
  #define VIC_TOUCH_DRIVER             "none"
#endif
