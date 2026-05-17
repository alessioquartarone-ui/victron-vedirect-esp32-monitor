# Victron VE.Direct ESP32 Monitor

Public ESP32-based Victron VE.Direct monitor with WebUI, OTA updates, setup wizard, history, backup, shutdown controls and JSON API.

This project is based on a stable private ESP32 CYD firmware branch, cleaned and converted into a public-ready edition with no private Wi-Fi credentials, no private OTA secrets and a first-boot setup wizard.

---

## First public firmware target

The first official public target is:

ESP32-CYD-ILI9341

Default hardware configuration:

Board: ESP32 CYD / Cheap Yellow Display  
Display: 2.8" ILI9341 240x320  
Touch: XPT2046  
VE.Direct RX default: GPIO27  
ESP battery ADC default: GPIO34  

Default wiring:

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

Default OTA files for the CYD target:

firmware/esp32-cyd-ili9341/version.txt  
firmware/esp32-cyd-ili9341/latest.bin  
firmware/esp32-cyd-ili9341/latest.sha256  

Default raw URLs:

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin

https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256

Users can change these URLs from the setup wizard.

---

## Planned hardware targets

- ESP32-CYD-ILI9341
- ESP32-HEADLESS
- ESP32S3-HEADLESS
- ESP32S3-ILI9341
- ESP32S3-ST7796

The headless targets are planned for users who want to use the monitor only from the WebUI without a TFT display.

---

## Configuration files

Public default configuration:

Victron_Display_Web/config_user.example.h

Optional private local configuration:

Victron_Display_Web/config_user.h

config_user.h is ignored by Git and must not be committed.

---

## Safety notes

- Do not connect VE.Direct 5V to the ESP32 unless your board/power design is safe.
- Connect only VE.Direct TX and GND for normal monitoring.
- Use a proper buck converter to power the ESP32 from a 12V/24V system.
- Check GPIO compatibility before changing runtime pins.
- Display/touch/SPI pins require a matching firmware target.

---

## License

License to be defined.
