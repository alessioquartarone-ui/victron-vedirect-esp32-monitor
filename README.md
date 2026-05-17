# Victron VE.Direct ESP32 Monitor

Public ESP32-based Victron VE.Direct monitor with WebUI, OTA updates, setup wizard, history, backup, shutdown controls and JSON API.

This project is based on a stable private ESP32 CYD firmware branch, cleaned and converted into a public-ready edition with no private Wi-Fi credentials, no private OTA secrets and a first-boot setup wizard.

---

## Recommended board

Recommended main board:

ESP32-S3 N16R8 Headless

Suggested specs:

- ESP32-S3
- 16 MB Flash
- 8 MB PSRAM
- USB-C
- WebUI only, no display required

Why this is recommended:

- Best price/performance
- More memory than classic ESP32
- Better for WebUI, OTA, JSON, history and future features
- No display/touch clone compatibility problems
- Any phone, tablet or PC can be used as the display through the WebUI

Recommended wiring for ESP32-S3 Headless:

Victron VE.Direct TX  -> ESP32-S3 GPIO18  
Victron VE.Direct GND -> ESP32-S3 GND  
Victron VE.Direct RX  -> not connected  
Victron VE.Direct 5V  -> not connected  

Default ESP32-S3 Headless runtime pins:

VE.Direct RX GPIO: 18  
ESP battery ADC GPIO: 4  
ESP battery ADC can be disabled from the setup wizard by setting it to -1.

---

## Supported public firmware targets

Current public targets:

- ESP32S3-HEADLESS
- ESP32-HEADLESS
- ESP32-CYD-ILI9341

Target descriptions:

ESP32S3-HEADLESS  
Recommended target. Generic ESP32-S3 board, WebUI only, no display required.

ESP32-HEADLESS  
Generic classic ESP32 board, WebUI only, no display required.

ESP32-CYD-ILI9341  
ESP32 Cheap Yellow Display / CYD with 2.8" ILI9341 display and XPT2046 touch.

---

## Display board option

Best all-in-one display board:

ESP32-CYD-ILI9341

Default hardware configuration:

Board: ESP32 CYD / Cheap Yellow Display  
Display: 2.8" ILI9341 240x320  
Touch: XPT2046  
VE.Direct RX default: GPIO27  
ESP battery ADC default: GPIO34  

Default CYD wiring:

Victron VE.Direct TX  -> ESP32 GPIO27  
Victron VE.Direct GND -> ESP32 GND  
Victron VE.Direct RX  -> not connected  
Victron VE.Direct 5V  -> not connected  

Do not power the ESP32/CYD directly from the VE.Direct 5V pin unless your hardware design explicitly supports it.

---

## Features

- VE.Direct data parser
- ESP32 WebUI dashboard
- First-boot setup wizard
- WiFiManager captive portal
- Configurable OTA URLs
- History pages
- Backup / restore support
- Shutdown controls
- Network information page
- JSON API
- Pretty JSON output
- Runtime configuration stored in Preferences/NVS
- Multiple public firmware targets
- OTA firmware repository separated from source repository

---

## Public setup wizard

On first boot, the firmware opens a setup wizard.

The wizard can configure:

- VE.Direct RX GPIO
- ESP battery ADC GPIO
- ESP battery voltage multiplier
- Hostname
- WiFiManager setup AP name/password
- OTA version/bin/SHA256 URLs
- Plant name
- Battery name/type/capacity
- System voltage
- Solar panel watts
- Battery thresholds
- Logging options
- Backup retention
- Dashboard refresh interval
- History refresh interval
- Language/theme
- Shutdown settings
- Factory reset option

Display, touch and SPI pins are compile-time hardware target settings and are not changed from the wizard.

To use another display or touch controller, flash the matching firmware target or build a custom target.

---

## Default setup AP

If no Wi-Fi is configured, WiFiManager starts a fallback access point:

SSID: Victron-ESP32-Setup  
Password: 12345678  

Connect to that network and open the captive portal or the device IP shown by your router.

---

## OTA repository

OTA firmware binaries are stored in a separate public repository:

victron-vedirect-esp32-monitor-ota

Current OTA targets:

firmware/esp32s3-headless/  
firmware/esp32-headless/  
firmware/esp32-cyd-ili9341/  

Each target contains:

version.txt  
latest.bin  
latest.sha256  

Default raw URLs for ESP32-S3 Headless:

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/version.txt

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/latest.bin

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32s3-headless/latest.sha256

Default raw URLs for ESP32 Headless:

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/version.txt

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/latest.bin

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-headless/latest.sha256

Default raw URLs for ESP32 CYD ILI9341:

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256

Users can change OTA URLs from the setup wizard.

---

## Firmware versions

Current public versions:

ESP32S3-HEADLESS  
V10.5.1-ESP32S3-HEADLESS-PUBLIC-WIZARD

ESP32-HEADLESS  
V10.5.1-ESP32-HEADLESS-PUBLIC-WIZARD

ESP32-CYD-ILI9341  
V10.5.1-CYD-PUBLIC-WIZARD

---

## Configuration files

Public default configuration:

Victron_Display_Web/config_user.example.h

Optional private local configuration:

Victron_Display_Web/config_user.h

config_user.h is ignored by Git and must not be committed.

Runtime configuration is saved on the ESP32 using Preferences/NVS and can be changed from the setup wizard.

---

## Planned future targets

Planned display targets:

- ESP32S3-ILI9341
- ESP32S3-ST7796

Possible future experimental targets:

- ESP32-C6-HEADLESS
- LilyGO T-Display S3

---

## Safety notes

- Do not connect VE.Direct 5V to the ESP32 unless your board/power design is safe.
- Connect only VE.Direct TX and GND for normal monitoring.
- Use a proper buck converter to power the ESP32 from a 12V/24V system.
- Check GPIO compatibility before changing runtime pins.
- Display/touch/SPI pins require a matching firmware target.
- VE.Direct TX goes to ESP32 RX GPIO.
- VE.Direct GND must be connected to ESP32 GND.
- VE.Direct RX is normally not needed.
- VE.Direct 5V should normally remain disconnected.

---

## License

MIT License
