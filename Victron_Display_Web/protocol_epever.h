#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

/*
  EPEVER / EPsolar / Tracer RS485 Modbus RTU support

  Hardware required:
  - MAX485 / SP3485 / TTL-to-RS485 module
  - ESP32 UART RX/TX
  - DE/RE direction control GPIO

  Default ESP32-S3:
  RX GPIO 18
  TX GPIO 17
  DE/RE GPIO 16
  Slave ID 1
  Baudrate 115200
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

void epeverLoop(uint32_t intervalMs = 2000);
bool epeverPollNow();

bool epeverIsOnline();

String epeverJson(bool pretty = true);
String epeverStatusText();
