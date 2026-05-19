#pragma once

/*
  Victron VE.Direct ESP32 Monitor
  Public protocol/device compatibility profiles

  Current supported protocols:
  - Victron VE.Direct text protocol over UART
  - Generic VE.Direct-like text protocol
  - EPEVER / EPsolar / Tracer RS485 Modbus RTU beta

  Planned future profiles are listed for roadmap / wizard visibility.
  They are NOT fully supported until the parser/driver is implemented.
*/

// ======================================================
// Protocol IDs
// ======================================================

#define VIC_PROTOCOL_VEDIRECT              "victron_vedirect"
#define VIC_PROTOCOL_GENERIC_VEDIRECT      "generic_vedirect_text"

#define VIC_PROTOCOL_EPEVER_MODBUS         "epever_rs485_modbus"
#define VIC_PROTOCOL_RENOGY_RS485          "renogy_rs485"
#define VIC_PROTOCOL_DALY_BMS              "daly_bms"
#define VIC_PROTOCOL_JBD_BMS               "jbd_bms"
#define VIC_PROTOCOL_JK_BMS                "jk_bms"
#define VIC_PROTOCOL_GENERIC_MODBUS_RTU    "generic_modbus_rtu"
#define VIC_PROTOCOL_GENERIC_UART_TEXT     "generic_uart_text"

// ======================================================
// Default public protocol
// ======================================================

#define VIC_DEFAULT_PROTOCOL_PROFILE        VIC_PROTOCOL_VEDIRECT
#define VIC_DEFAULT_PROTOCOL_LABEL          "Victron VE.Direct"
#define VIC_DEFAULT_PROTOCOL_STATUS         "supported"

// Compatibility aliases.
// Some files may use these longer names.
#define VIC_DEFAULT_DEVICE_PROTOCOL         VIC_DEFAULT_PROTOCOL_PROFILE
#define VIC_DEFAULT_DEVICE_PROTOCOL_LABEL   VIC_DEFAULT_PROTOCOL_LABEL
#define VIC_DEFAULT_DEVICE_PROTOCOL_STATUS  VIC_DEFAULT_PROTOCOL_STATUS

// ======================================================
// Current support flags
// ======================================================

#define VIC_SUPPORTS_VEDIRECT               1
#define VIC_SUPPORTS_GENERIC_VEDIRECT       1

// EPEVER parser/driver exists, but real device testing is still required.
#define VIC_SUPPORTS_EPEVER_MODBUS          1

#define VIC_SUPPORTS_RENOGY_RS485           0
#define VIC_SUPPORTS_DALY_BMS               0
#define VIC_SUPPORTS_JBD_BMS                0
#define VIC_SUPPORTS_JK_BMS                 0
#define VIC_SUPPORTS_GENERIC_MODBUS_RTU     0
#define VIC_SUPPORTS_GENERIC_UART_TEXT      0

// ======================================================
// Compatibility notes
// ======================================================

#define VIC_COMPAT_NOTE_VEDIRECT \
  "Supported now. Connect device TX to ESP32 RX GPIO and device GND to ESP32 GND."

#define VIC_COMPAT_NOTE_EPEVER \
  "Supported in beta. Requires TTL-to-RS485 transceiver such as SP3485/MAX485 and correct Modbus settings."

#define VIC_COMPAT_NOTE_MODBUS \
  "Planned. Requires RS485 transceiver such as MAX485/SP3485 and a Modbus parser."

#define VIC_COMPAT_NOTE_BMS \
  "Planned. Requires matching UART/RS485/BLE protocol support and safe level shifting."

#define VIC_COMPAT_NOTE_GENERIC \
  "Experimental/planned. Requires custom parser mapping."
