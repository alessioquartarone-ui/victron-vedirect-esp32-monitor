#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

/*
  EPEVER / EPsolar / Tracer RS485 Modbus RTU support

  Hardware required:
  - TTL-to-RS485 transceiver module, preferably SP3485 3.3V or compatible MAX485 module
  - ESP32 UART RX/TX
  - Optional DE/RE direction GPIO
  - If the RS485 module has automatic direction control, set DE/RE GPIO to -1 in the wizard

  Default ESP32-S3:
  RX GPIO 18
  TX GPIO 17
  DE/RE GPIO 16
  Slave ID 1
  Baudrate 115200

  Default ESP32 classic:
  RX GPIO 16
  TX GPIO 17
  DE/RE GPIO 4

  Default CYD:
  RX GPIO 27
  TX GPIO 26
  DE/RE GPIO 22
*/

struct EpeverTelemetry {
  bool enabled;
  bool online;

  uint32_t lastPollMs;
  uint32_t lastOkMs;
  uint32_t errorCount;

  uint8_t slaveId;
  uint32_t baudrate;

  int rxPin;
  int txPin;
  int deRePin;

  float pvVoltage;
  float pvCurrent;
  float pvPower;

  float batteryVoltage;
  float batteryCurrent;
  float batteryPower;

  float loadVoltage;
  float loadCurrent;
  float loadPower;

  float batteryTemperature;
  float deviceTemperature;

  uint16_t rawBatteryStatus;
  uint16_t rawChargingStatus;
  uint16_t rawDischargingStatus;

  String lastError;
};

extern EpeverTelemetry epever;

void epeverApplyDefaults();

void epeverBegin(
  uint8_t slaveId,
  uint32_t baudrate,
  int rxPin,
  int txPin,
  int deRePin
);

void epeverStop();

void epeverLoop(uint32_t intervalMs = 2000);
bool epeverPollNow();

bool epeverIsOnline();

String epeverJson(bool pretty = true);
String epeverStatusText();
String epeverBriefStatus();
