#include "protocol_epever.h"

#if defined(ESP32)
HardwareSerial EpeverSerial(2);
#else
#error "EPEVER protocol currently requires ESP32 HardwareSerial"
#endif

EpeverTelemetry epever;

static ModbusMaster epeverNode;
static int epeverDeRePin = -1;
static bool epeverSerialStarted = false;

// ======================================================
// Small conversion helpers
// ======================================================

static float regToFloat100(uint16_t value) {
  return ((float)value) / 100.0f;
}

static float signedRegToFloat100(uint16_t value) {
  int16_t signedValue = (int16_t)value;
  return ((float)signedValue) / 100.0f;
}

static float regsToFloat100(uint16_t lowWord, uint16_t highWord) {
  uint32_t raw = ((uint32_t)highWord << 16) | lowWord;
  return ((float)raw) / 100.0f;
}

// ======================================================
// RS485 direction control
// ======================================================

static void epeverPreTransmission() {
  if (epeverDeRePin >= 0) {
    digitalWrite(epeverDeRePin, HIGH);
    delayMicroseconds(400);
  }
}

static void epeverPostTransmission() {
  if (epeverDeRePin >= 0) {
    delayMicroseconds(400);
    digitalWrite(epeverDeRePin, LOW);
  }
}

// ======================================================
// Defaults
// ======================================================

void epeverApplyDefaults() {
  epever.enabled = false;
  epever.online = false;

  epever.lastPollMs = 0;
  epever.lastOkMs = 0;
  epever.errorCount = 0;

  epever.slaveId = 1;
  epever.baudrate = 115200;

#if defined(VIC_TARGET_CYD_ILI9341)
  epever.rxPin = 27;
  epever.txPin = 26;
  epever.deRePin = 22;
#elif defined(VIC_TARGET_ESP32_HEADLESS)
  epever.rxPin = 16;
  epever.txPin = 17;
  epever.deRePin = 4;
#else
  epever.rxPin = 18;
  epever.txPin = 17;
  epever.deRePin = 16;
#endif

  epever.pvVoltage = NAN;
  epever.pvCurrent = NAN;
  epever.pvPower = NAN;

  epever.batteryVoltage = NAN;
  epever.batteryCurrent = NAN;
  epever.batteryPower = NAN;

  epever.loadVoltage = NAN;
  epever.loadCurrent = NAN;
  epever.loadPower = NAN;

  epever.batteryTemperature = NAN;
  epever.deviceTemperature = NAN;

  epever.rawBatteryStatus = 0;
  epever.rawChargingStatus = 0;
  epever.rawDischargingStatus = 0;

  epever.lastError = "";
}

// ======================================================
// Begin / stop
// ======================================================

void epeverBegin(
  uint8_t slaveId,
  uint32_t baudrate,
  int rxPin,
  int txPin,
  int deRePin
) {
  epeverApplyDefaults();

  if (slaveId < 1) slaveId = 1;
  if (slaveId > 247) slaveId = 247;

  if (baudrate < 1200) baudrate = 115200;

  epever.enabled = true;
  epever.slaveId = slaveId;
  epever.baudrate = baudrate;
  epever.rxPin = rxPin;
  epever.txPin = txPin;
  epever.deRePin = deRePin;

  epeverDeRePin = deRePin;

  if (epeverDeRePin >= 0) {
    pinMode(epeverDeRePin, OUTPUT);
    digitalWrite(epeverDeRePin, LOW);
  }

  EpeverSerial.begin(baudrate, SERIAL_8N1, rxPin, txPin);
  epeverSerialStarted = true;

  epeverNode.begin(slaveId, EpeverSerial);
  epeverNode.preTransmission(epeverPreTransmission);
  epeverNode.postTransmission(epeverPostTransmission);

  epever.lastError = "started";
}

void epeverStop() {
  epever.enabled = false;
  epever.online = false;

  if (epeverSerialStarted) {
    EpeverSerial.end();
    epeverSerialStarted = false;
  }

  if (epeverDeRePin >= 0) {
    digitalWrite(epeverDeRePin, LOW);
  }

  epever.lastError = "stopped";
}

// ======================================================
// Polling
// ======================================================

bool epeverPollNow() {
  if (!epever.enabled) {
    epever.online = false;
    epever.lastError = "not enabled";
    return false;
  }

  if (!epeverSerialStarted) {
    epever.online = false;
    epever.errorCount++;
    epever.lastError = "serial not started";
    return false;
  }

  epever.lastPollMs = millis();

  /*
    EPEVER common input registers, function 0x04.

    0x3100 PV array voltage            /100 V
    0x3101 PV array current            /100 A
    0x3102 PV array power low word     /100 W
    0x3103 PV array power high word

    0x3104 Battery voltage             /100 V
    0x3105 Battery charging current    /100 A
    0x3106 Battery charging power low  /100 W
    0x3107 Battery charging power high

    0x310C Load voltage                /100 V
    0x310D Load current                /100 A
    0x310E Load power low              /100 W
    0x310F Load power high

    0x3110 Battery temperature         /100 °C, signed on some models
    0x3111 Device temperature          /100 °C, signed on some models
  */

  uint8_t result = epeverNode.readInputRegisters(0x3100, 18);

  if (result != epeverNode.ku8MBSuccess) {
    epever.online = false;
    epever.errorCount++;
    epever.lastError = "read 0x3100 failed code " + String(result);
    return false;
  }

  uint16_t r[18];
  for (int i = 0; i < 18; i++) {
    r[i] = epeverNode.getResponseBuffer(i);
  }

  epever.pvVoltage = regToFloat100(r[0]);
  epever.pvCurrent = regToFloat100(r[1]);
  epever.pvPower = regsToFloat100(r[2], r[3]);

  epever.batteryVoltage = regToFloat100(r[4]);
  epever.batteryCurrent = regToFloat100(r[5]);
  epever.batteryPower = regsToFloat100(r[6], r[7]);

  epever.loadVoltage = regToFloat100(r[12]);
  epever.loadCurrent = regToFloat100(r[13]);
  epever.loadPower = regsToFloat100(r[14], r[15]);

  epever.batteryTemperature = signedRegToFloat100(r[16]);
  epever.deviceTemperature = signedRegToFloat100(r[17]);

  /*
    Optional status registers.
    Not all models respond exactly the same way, so this second read is allowed to fail.
  */
  uint8_t statusResult = epeverNode.readInputRegisters(0x3200, 3);
  if (statusResult == epeverNode.ku8MBSuccess) {
    epever.rawBatteryStatus = epeverNode.getResponseBuffer(0);
    epever.rawChargingStatus = epeverNode.getResponseBuffer(1);
    epever.rawDischargingStatus = epeverNode.getResponseBuffer(2);
  }

  epever.online = true;
  epever.lastOkMs = millis();
  epever.lastError = "ok";

  return true;
}

void epeverLoop(uint32_t intervalMs) {
  if (!epever.enabled) return;

  if (intervalMs < 500) intervalMs = 500;

  uint32_t now = millis();
  if (now - epever.lastPollMs >= intervalMs) {
    epeverPollNow();
  }
}

bool epeverIsOnline() {
  if (!epever.enabled) return false;
  if (!epever.online) return false;
  if (millis() - epever.lastOkMs > 10000) return false;
  return true;
}

// ======================================================
// Status / JSON
// ======================================================

String epeverJson(bool pretty) {
  String nl = pretty ? "\n" : "";
  String sp = pretty ? "  " : "";

  String j;
  j.reserve(2600);

  j += "{" + nl;
  j += sp + "\"protocol\":\"epever_rs485_modbus\"," + nl;
  j += sp + "\"enabled\":" + String(epever.enabled ? "true" : "false") + "," + nl;
  j += sp + "\"online\":" + String(epeverIsOnline() ? "true" : "false") + "," + nl;
  j += sp + "\"slaveId\":" + String(epever.slaveId) + "," + nl;
  j += sp + "\"baudrate\":" + String(epever.baudrate) + "," + nl;
  j += sp + "\"rxPin\":" + String(epever.rxPin) + "," + nl;
  j += sp + "\"txPin\":" + String(epever.txPin) + "," + nl;
  j += sp + "\"deRePin\":" + String(epever.deRePin) + "," + nl;

  j += sp + "\"pv\":{" + nl;
  j += sp + sp + "\"voltage\":" + String(epever.pvVoltage, 2) + "," + nl;
  j += sp + sp + "\"current\":" + String(epever.pvCurrent, 2) + "," + nl;
  j += sp + sp + "\"power\":" + String(epever.pvPower, 2) + nl;
  j += sp + "}," + nl;

  j += sp + "\"battery\":{" + nl;
  j += sp + sp + "\"voltage\":" + String(epever.batteryVoltage, 2) + "," + nl;
  j += sp + sp + "\"current\":" + String(epever.batteryCurrent, 2) + "," + nl;
  j += sp + sp + "\"power\":" + String(epever.batteryPower, 2) + "," + nl;
  j += sp + sp + "\"temperature\":" + String(epever.batteryTemperature, 2) + nl;
  j += sp + "}," + nl;

  j += sp + "\"load\":{" + nl;
  j += sp + sp + "\"voltage\":" + String(epever.loadVoltage, 2) + "," + nl;
  j += sp + sp + "\"current\":" + String(epever.loadCurrent, 2) + "," + nl;
  j += sp + sp + "\"power\":" + String(epever.loadPower, 2) + nl;
  j += sp + "}," + nl;

  j += sp + "\"deviceTemperature\":" + String(epever.deviceTemperature, 2) + "," + nl;
  j += sp + "\"rawBatteryStatus\":" + String(epever.rawBatteryStatus) + "," + nl;
  j += sp + "\"rawChargingStatus\":" + String(epever.rawChargingStatus) + "," + nl;
  j += sp + "\"rawDischargingStatus\":" + String(epever.rawDischargingStatus) + "," + nl;
  j += sp + "\"lastPollMs\":" + String(epever.lastPollMs) + "," + nl;
  j += sp + "\"lastOkMs\":" + String(epever.lastOkMs) + "," + nl;
  j += sp + "\"errorCount\":" + String(epever.errorCount) + "," + nl;
  j += sp + "\"lastError\":\"" + epever.lastError + "\"" + nl;
  j += "}" + nl;

  return j;
}

String epeverStatusText() {
  String out;

  out += "EPEVER / EPsolar / Tracer RS485 Modbus\n";
  out += "Enabled: " + String(epever.enabled ? "yes" : "no") + "\n";
  out += "Online: " + String(epeverIsOnline() ? "yes" : "no") + "\n";
  out += "Slave ID: " + String(epever.slaveId) + "\n";
  out += "Baudrate: " + String(epever.baudrate) + "\n";
  out += "RX GPIO: " + String(epever.rxPin) + "\n";
  out += "TX GPIO: " + String(epever.txPin) + "\n";
  out += "DE/RE GPIO: " + String(epever.deRePin) + "\n";
  out += "PV: " + String(epever.pvVoltage, 2) + " V, " + String(epever.pvCurrent, 2) + " A, " + String(epever.pvPower, 2) + " W\n";
  out += "Battery: " + String(epever.batteryVoltage, 2) + " V, " + String(epever.batteryCurrent, 2) + " A, " + String(epever.batteryPower, 2) + " W\n";
  out += "Load: " + String(epever.loadVoltage, 2) + " V, " + String(epever.loadCurrent, 2) + " A, " + String(epever.loadPower, 2) + " W\n";
  out += "Battery temp: " + String(epever.batteryTemperature, 2) + " C\n";
  out += "Device temp: " + String(epever.deviceTemperature, 2) + " C\n";
  out += "Raw battery status: " + String(epever.rawBatteryStatus) + "\n";
  out += "Raw charging status: " + String(epever.rawChargingStatus) + "\n";
  out += "Raw discharging status: " + String(epever.rawDischargingStatus) + "\n";
  out += "Last poll ms: " + String(epever.lastPollMs) + "\n";
  out += "Last ok ms: " + String(epever.lastOkMs) + "\n";
  out += "Error count: " + String(epever.errorCount) + "\n";
  out += "Last error: " + epever.lastError + "\n";

  return out;
}

String epeverBriefStatus() {
  String out;

  out += epeverIsOnline() ? "online" : "offline";
  out += " · PV ";
  out += String(epever.pvPower, 1);
  out += " W · BAT ";
  out += String(epever.batteryVoltage, 2);
  out += " V";

  return out;
}
