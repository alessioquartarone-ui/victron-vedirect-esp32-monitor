/*
  Victron VE.Direct ESP32 Monitor V10.5.0 CYD PUBLIC WIZARD
  - WiFi già inserito
  - Display TFT_eSPI - CYD ILI9341 HSPI official pinout
  - Touch XPT2046
  - Web dashboard con grafico live
  - Login web
  - /json
  - /settings
  - upload firmware da browser: /update
  - Arduino OTA
  - mDNS: http://victron-monitor.local
  - V10.4.66: OTA recovery safe, boot fail counter 90s, ota-status pro, reboot recovery
  - V10.4.67: VE.Direct RX su connettore UART verde/GPIO3, test non stabile su CYD con CH340
  - V10.4.68: VE.Direct RX spostato su IO35, ingresso puro senza interferenze con USB/RGB
  - V10.4.69: spegnimento software remoto con timer configurabile e shutdown fino a reset
  - V10.4.71: retention backup 5/5
  - V10.4.73: VE.Direct RX spostato su IO27 per evitare interferenza con batteria ESP su GPIO34
  - V10.4.74: merge stabile IO27 + shutdown + plant-info fix + retention 5/5 + diagnostica + pagina rete/IP
  - V10.4.77: popup storico, tempi stati carica, UI refresh piu leggero
  - V10.4.78: pulizia WebUI, CSS pro comune, popup piu coerenti, cache headers JSON, link rete/API ordinati
  - V10.4.79: protezione storico da valori fuori scala/corrotti, popup con valori sanificati
  - V10.4.80: limiti hard per storico 7d/12h/31d, reset automatico slot giornalieri corrotti
  - V10.4.81: badge stato coerenti, auto check GitHub all apertura, popup esito OTA con data/ora, JSON pretty globale
  - V10.4.82: grafico live pannello scalato da dati impianto, legenda/valori live, popup storico piu ordinati, quick-check
  - V10.4.83: fix layout popup storico, grafici GX con scale chiare, no-data pulito e padding iPhone
  - V10.5.0: public-ready wizard, no private hardcoded WiFi, OTA public configurable, VE.Direct runtime pin default IO27
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>

// TFT_eSPI setup NON integrato qui.
// V10.4.8 usa User_Setup.h forzato dal workflow GitHub, identico al setup Arduino IDE locale funzionante.
// Questo evita conflitti tra define nello sketch e define della libreria.
// V10.4.55: TOUCH_CS definito prima di TFT_eSPI.h solo per evitare warning della libreria.
#ifndef TOUCH_CS
#define TOUCH_CS   33
#endif
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "public_config.h"
#include "public_wizard.h"

const char* WIFI_SSID = "";  // Public build: no hardcoded WiFi SSID
const char* WIFI_PASS = "";  // Public build: no hardcoded WiFi password
const char* HOSTNAME  = "victron-esp32-monitor";
const char* FW_VERSION = "V10.5.0-CYD-PUBLIC-WIZARD";
const char* FW_BUILD_DATE = __DATE__;
const char* FW_BUILD_TIME = __TIME__;

// Modalità configurazione Wi-Fi pubblica.
 // Nessun SSID/password privato hardcoded.
 // Se non trova credenziali WiFi salvate, crea AP: Victron-ESP32-Setup / 12345678
const char* WIFI_SETUP_AP = "Victron-ESP32-Setup";
const char* WIFI_SETUP_PASS = "12345678";
bool forceWifiPortalAtBoot = false;

const char* FW_NAME = "Victron VE.Direct ESP32 Monitor Public Wizard";

const char* WEB_USER = "admin";
const char* WEB_PASS = "admin";

// IP fisso: se vuoi cambiarlo, cambia qui.
IPAddress local_IP(0, 0, 0, 0);
IPAddress gateway(0, 0, 0, 0);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(0, 0, 0, 0);


#define LCD_BL 21

#ifndef TOUCH_CS
#define TOUCH_CS   33
#endif
#define TOUCH_IRQ  36
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_SCLK 25

// VE.Direct input - V10.4.73+ stable diymore profile
// Scheda diymore/CYD non classica: GPIO16 e GPIO17 sono usati dal LED RGB, quindi NON usarli per VE.Direct.
// Il connettore UART verde/giallo puo' passare dal CH340 USB e disturbare RX0/GPIO3.
// Questa versione usa IO27 per VE.Direct, solo ricezione, senza TX verso Victron.
// Motivo: IO35 interferiva con la lettura batteria ESP su GPIO34.
// Collegamento: TX VE.Direct oscillante -> IO27 CYD; GND VE.Direct -> GND CYD.
// Non collegare RX VE.Direct, +5V VE.Direct, TX CYD o 5V CYD.
#define VICTRON_RX 27
#define VICTRON_TX -1
#define VICTRON_BAUD 19200
const char* VICTRON_PORT_NAME = "IO27 VE.Direct RX battery safe";
const char* VICTRON_PORT_DETAIL = "RX GPIO27/IO27, TX disabilitato, baud 19200";

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS);  // V10.4.28: polling mode, niente IRQ per evitare reboot loop
WebServer server(80);
WiFiManager wifiManager;
HardwareSerial VictronSerial(2);
Preferences prefs;

// V10.4.73 - VE.Direct spostato su IO27 per evitare saturazione GPIO34 batteria ESP.
// Questi sono i fallback usati se in Preferences non ci sono URL salvati.
const char* GITHUB_VERSION_URL = "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/version.txt";
const char* GITHUB_BIN_URL     = "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.bin";
const char* GITHUB_SHA_URL     = "https://raw.githubusercontent.com/alessioquartarone-ui/victron-vedirect-esp32-monitor-ota/main/firmware/esp32-cyd-ili9341/latest.sha256";
const char* GITHUB_LOG_URL     = "";
bool littleFsReady = false;
unsigned long lastHistorySaveMs = 0;
unsigned long lastChargeHistorySaveMs = 0;
unsigned long lastGithubCheckMs = 0;
String githubLastStatus = "Non controllato";
String githubRemoteVersion = "N/D";
bool githubUpdateAvailable = false;
String rollbackStatus = "N/D";

String runtimeHostname() {
  String h = pubCfg.hostname;
  h.trim();
  if (h.length() == 0) h = String(HOSTNAME);
  return h;
}

String runtimeSetupApSsid() {
  String s = pubCfg.setupApSsid;
  s.trim();
  if (s.length() == 0) s = String(WIFI_SETUP_AP);
  return s;
}

String runtimeSetupApPass() {
  String s = pubCfg.setupApPassword;
  s.trim();
  if (s.length() < 8) s = String(WIFI_SETUP_PASS);
  return s;
}

// Bridge the public wizard settings to the legacy V10.4.x preference keys used
// by the existing dashboard/OTA/history pages. This keeps the old stable code
// intact while making the public first-boot wizard useful for the full firmware.
void applyPublicConfigToLegacyPrefs() {
  prefs.begin("victron", false);

  if (pubCfg.otaVersionUrl.length()) prefs.putString("gh_ver_url", pubCfg.otaVersionUrl);
  if (pubCfg.otaBinUrl.length()) prefs.putString("gh_bin_url", pubCfg.otaBinUrl);
  if (pubCfg.otaSha256Url.length()) prefs.putString("gh_sha_url", pubCfg.otaSha256Url);

  prefs.putFloat("esp_bat_mult", pubCfg.espBatteryMultiplier);

  prefs.putString("plant_name", pubCfg.plantName);
  prefs.putString("battery_name", pubCfg.batteryName);
  prefs.putString("battery_type", pubCfg.batteryType);
  prefs.putFloat("system_voltage", pubCfg.systemVoltage);
  prefs.putFloat("battery_capacity_ah", pubCfg.batteryCapacityAh);
  prefs.putFloat("panel_watts", pubCfg.panelWatts);
  prefs.putFloat("bat_low_v", pubCfg.batLowV);
  prefs.putFloat("bat_medium_v", pubCfg.batMediumV);
  prefs.putFloat("bat_full_v", pubCfg.batFullV);

  prefs.putInt("backup_keep", pubCfg.backupConfigMax);
  prefs.putInt("daily_backup_keep", pubCfg.backupHistoryMax);

  prefs.end();
}


// GitHub OTA progress reale: il download/update gira in background e la pagina web interroga /github-progress.
volatile bool githubOtaRunning = false;
volatile bool githubOtaDone = false;
volatile bool githubOtaOk = false;
volatile int githubOtaPercent = 0;
volatile uint32_t githubOtaWritten = 0;
volatile uint32_t githubOtaTotal = 0;
String githubOtaMessage = "Pronto";
TaskHandle_t githubOtaTaskHandle = NULL;

// Backup/Recovery clone progress: task in background + polling web page
volatile bool cloneTaskRunning = false;
volatile bool cloneTaskDone = false;
volatile bool cloneTaskOk = false;
volatile int cloneTaskPercent = 0;
String cloneTaskStage = "Pronto";
String cloneTaskDetail = "";
TaskHandle_t cloneTaskHandle = NULL;

void setBackupProgress(int pct, const String& stage) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  cloneTaskPercent = pct;
  cloneTaskStage = stage;
}

// Prototipi manuali necessari per Arduino CLI/core 2.x
void robustTftInit();
void drawDashboard();

// Stato OTA upload/restart condiviso anche con GitHub direct OTA
bool uploadAllowed = true;
String uploadError = "";
bool otaRestartPending = false;
unsigned long otaRestartAtMs = 0;
size_t otaWrittenBytes = 0;
size_t otaExpectedSize = 0;
uint32_t otaFreeAtStart = 0;
String otaFileName = "";

// V10.4.66 Professional Safe OTA / Recovery
const unsigned long SAFE_BOOT_CONFIRM_MS = 90000UL;
const int SAFE_BOOT_MAX_UNCONFIRMED = 3;
bool safeBootConfirmed = false;
unsigned long safeBootStartedMs = 0;
unsigned long lastWeeklyUpdateCheckMs = 0;
bool recoveryMode = false;
String lastScheduledUpdateStatus = "N/D";


String timeText();
String buildText();
bool timeIsValid();
void syncNtpNow();
void updateOtaTimeIfNtpBecomesValid();
void saveHistoryToFs();
bool loadHistoryFromFs();
void initLittleFs();
struct HistorySlot;
bool historyValueOk(float v, float minV, float maxV);
void sanitizeHistorySlotWithLimit(HistorySlot &s, float whLimit);
void sanitizeHistorySlot(HistorySlot &s);
void sanitizeAllHistorySlots();

String prettyJson(const String& raw);
void sendJsonPretty(const String& raw);
String wifiBadgeClass(int rssi);
String veBadgeClass();
String espBatteryBadgeClass(float pct);
String otaNotifyJson();
void markOtaNotifySeen();
void saveOtaNotify(const String& eventType, const String& title, const String& message, const String& level, const String& version);
void handleOtaNotifyJson();
void handleOtaNotifyClear();
void rollbackInitAndValidate();
void checkGithubUpdate(bool forced);
void saveOtaResult(bool ok, const String& detail);
void cleanRestartNow(const char* reason);
void safeBootStart();
void safeBootLoop();
void weeklyGithubUpdateLoop();
String getGithubShaUrl();
String getGithubChangelogUrl();
String httpGetString(const String& url, int timeoutMs);
String sha256HexOfStream(Stream& stream, int len, size_t* writtenOut, bool writeToUpdate);
void handleRecoveryPage();
void handleRebootRecovery();
String card(String title, String value, String extra);
String htmlHeader(String title);
String urlEncode(const String& in);
String getGithubVersionUrl();
String getGithubBinUrl();
int prefsGetIntSafe(const char* key, int fallback);
void prefsPutIntSafe(const char* key, int value);
String touchRotKey(const char* base);
int prefsGetTouchCal(const char* base, int fallback);
void prefsPutTouchCal(const char* base, int value);
void addEventLog(const String& type, const String& msg);
void sendActionPage(const String& title, const String& message, int refreshSeconds, const String& target);
void drawToolsPage();
void drawRecapPage();
void drawTouchCalTftPage();
void handleToolsTouch(int rawX, int rawY);
void handleTouchCalibrationTap(int rawX, int rawY);
bool touchPrecisionAvailable();
bool mapTouchRawToScreen(int rawX, int rawY, int* sx, int* sy);
int touchZoneFromScreenX(int sx);
int navButtonFromScreen(int sx, int sy);
int toolsButtonFromScreen(int sx, int sy);
int navButtonFromRaw(int rawX, int rawY);
void savePrecisionTouchCalibration();
bool touchZonesEnabled();
String touchNavigationModeText();
void handleTouchClear();
void toggleTftRotationFromTools();

float adcPinVoltage(int pin);
float lipoPercentFromVoltage(float v);
float espBatteryVoltage();
float espBatteryPercent();
String espBatteryStatusText();
String espBatteryConnectionText();
void handleBatteryInstalledToggle();
String batteryScanJson();
void handleBatteryPage();
void handleBatteryJson();
void handleBatScanPage();
void handleVictronDataPage();
void addVedirectRawLine(const String& line);
void restartVictronSerial(const String& reason);
void victronAutoRecoveryLoop();
void handleVedirectRestart();
void handleEnergyTodayPage();
void handleAlertsPage();
void handleAlertsJson();
void handleBatteryCalPage();
void handleOtaScheduleSave();
void handleTouchSavePoints();
void handleHistoryGxPage();
void handleSetupCheckPage();
float espBatteryMultiplier();
void loadEspBatteryCalibration();
void saveEspBatteryMultiplier(float m);
String systemHealthJson();
int alertCountNow();

// SD manager
bool sdMount(bool force = false);
void sdUnmount();
bool sdWipeRecursive(const String& path, uint32_t &files, uint32_t &dirs);
void handleSdFormat();
bool sdEnsureDir(const String& path);
String sdTypeText();
String formatBytes64(uint64_t bytes);
String sdInfoJson();
void handleSdPage();
void handleSdJson();
void handleSdMount();
void handleSdUnmount();
void handleFilesHubPage();
void handleSdFilesPage();
void handleSdViewFile();
void handleSdDownloadFile();
void handleSdDeleteFile();
String normalizeSdPath(String p);
bool safeSdBrowserPath(String p);
String fileMimeFromPath(const String& path);
String fileNameOnly(String path);

// Storage / SD logger
String storageMode();
bool storageUseSd();
bool storageUseInternal();
int sdLogIntervalSec();
String sdMonthDir();
String sdLogFileName();
String sdLogFileNameForDate(const String& date);
String sdCsvHeader();
String sdCsvCurrentLine();
bool sdEnsureReadyForWrite();
bool sdAppendLine(const String& path, const String& line, bool headerIfNew);
bool sdAppendLineProtected(const String& path, const String& line, bool headerIfNew);
void sdLoggerLoop();
void handleStoragePage();
void handleStorageSave();
void handleSdSnapshot();
void handleSdLogCsv();
void handleSdLogsPage();
void handleSdLogDownload();
void handleSdLogDelete();
void handleBackupToSd();
void handleFullBackupBackupPage();
void handleFullBackupBackupStart();
void handleBackupRecoveryPage();
void handleBackupProgressJson();
void cloneBackupTask(void* param);
bool createFullBackupBackup(String& detailOut);
bool createFullBackupBackup(const String& reason, String& detailOut);
void pruneBackupBackupsKeepTwo();
void autoBackupBeforeOta(const String& source);
void autoBackupAfterOtaIfNeeded();
String settingsBackupJson();
void handleStatsSdPage();
float lastNDaysWh(int days);
void handlePowerPage();
void handleNetworkPage();
void handleNetworkJson();
void handleShutdownPage();
void handleShutdownTimed();
void handleShutdownReset();
void enterSoftwareShutdown(uint32_t sleepMinutes, bool wakeByTimer, const String& reason);
void handleSdMaintenancePage();
void handlePlantInfoPage();
void handlePlantInfoSave();
void handleThresholdsPage();
void handleThresholdsSave();
void handleHealthPage();
void handleDiagnosticSnapshot();
void handleBackupRecoveryListPage();
void handleBackupRecoveryRestorePage();
void handleBackupRecoveryRestoreStart();
void sdCreateBaseDirs();
float thresholdBattLow();
float thresholdEspBatLow();
int thresholdWifiWeak();
int thresholdNoVedirectSec();
int thresholdSdFullPercent();
int healthScoreNow();
String healthStatusText();
String plantInfoSummary();
float configuredPanelWatts();
void handleQuickCheckPage();
void handleSdMaintenanceCleanLogs();
String resetReasonText();
String powerHealthText();
int sdCountEntries(const String& path, bool recursive);
uint64_t sdDirBytes(const String& path);
int displayAutoOffSeconds();
int displayAutoOffMinutes();
void handleDisplayAutoOffLoop();

// V10.4.63 - Recovery/log/theme/diagnostic extensions
void handleBackupRecoveryProPage();
void handleBackupConfigRestoreFromSd();
void handleDailyBackupsPage();
void handleDailyBackupNow();
void dailyConfigBackupLoop();
void pruneDailyConfigBackups(int keep);
void handleAlertsHistoryPage();
void alertHistoryLoop();
void handleSdRetentionPage();
void handleSdRetentionSave();
void applySdRetentionPolicy();
void setupBackupRetention5x5Defaults();
void handleDiagnosticRunPage();
void handleThemePage();
void handleThemeSave();
String alertSignatureText();
String uiTheme();

// V10.4.64 - advanced technical pages, safer recovery, API, NTP, VE.Direct raw, SD integrity
void handleRecoveryRestoreProPage();
void handleRecoveryRestoreFirmwareStart();
bool restoreFirmwareFromSdFile(const String& filePath, String& detailOut);
String findFirmwareInRecoveryFolder(const String& folder);
String latestRecoveryFolder();
void appWatchdogLoop();
void handleAppWatchdogPage();
void recordRebootHistory();
void handleRebootHistoryPage();
void handleRebootHistoryJson();
void handleTimeNtpPage();
void handleNtpSyncNow();
void addVedirectRawLine(const String& line);
void handleVedirectRawPage();
void handleVedirectRawJson();
String apiStatusJson();
void handleApiV1Status();
void handleApiV1Power();
void handleApiV1Victron();
void handleApiV1Battery();
void handleApiV1Sd();
void handleApiV1Alerts();
void handleApiV1Health();
void handleApiV1Config();
void handleApiV1Index();
void handleSdIntegrityPage();
void handleSdRepairCsvIndex();
void handleOfflineApPage();
void handleHardwareTestPage();
void handleSdWriteProtectionPage();
void handleSdWriteProtectionSave();
bool sdWritesAllowed();
bool sdAppendLineProtected(const String& path, const String& line, bool headerIfNew);

bool backlightOn = true;
bool otaNtpFixPending = false;
unsigned long lastOtaNtpFixTryMs = 0;
unsigned long lastDailyConfigBackupCheckMs = 0;
unsigned long lastAlertHistoryCheckMs = 0;
String lastAlertSignatureStored = "";

// V10.4.64 technical reliability state
unsigned long appWatchdogLastLoopMs = 0;
unsigned long appWatchdogLastWebMs = 0;
unsigned long appWatchdogLastSdMs = 0;
unsigned long appWatchdogLastVictronParserMs = 0;
uint32_t minFreeHeapSeen = 0xFFFFFFFF;
String appWatchdogLastStatus = "Avvio";
const int VEDIRECT_RAW_COUNT = 20;
String vedirectRawRing[VEDIRECT_RAW_COUNT];
int vedirectRawPos = 0;
int vedirectRawStored = 0;
bool sdWriteProtectEnabled = false;

float battV = NAN, battA = NAN, battW = NAN;
float panelV = NAN, panelW = NAN;
float yieldTodayKWh = NAN, yieldYesterdayKWh = NAN, yieldTotalKWh = NAN;
float maxPowerToday = NAN, maxPowerYesterday = NAN;

String chargeState = "N/D";
String mpptState = "N/D";
String errorState = "0";
String modelName = "N/D";
String serialNumber = "N/D";
String firmwareVer = "N/D";
String lastRawLine = "";

String rxLine;
unsigned long lastVictronMs = 0;
unsigned long lastVictronByteMs = 0;
unsigned long lastVictronReinitMs = 0;
uint32_t vedirectByteCount = 0;
uint32_t vedirectParsedLineCount = 0;
uint32_t vedirectBadLineCount = 0;
uint32_t vedirectReinitCount = 0;
String lastVictronReinitReason = "Mai";
unsigned long lastDrawMs = 0;
unsigned long lastTouchMs = 0;
unsigned long lastWiFiCheckMs = 0;
unsigned long lastUserActivityMs = 0;
unsigned long lastDisplayAutoOffCheckMs = 0;
int page = 0;
const int TFT_PAGE_COUNT = 10;
int displayRotation = 1;  // 1 = normale landscape, 3 = ruotato 180 gradi
bool touchReady = false;
bool touchInitTried = false;
unsigned long touchInitAfterMs = 0;
bool touchWasDown = false;
unsigned long lastTouchPollMs = 0;
unsigned long lastTouchActionMs = 0;
int lastTouchRawX = -1;
int lastTouchRawY = -1;
unsigned long lastTouchRawMs = 0;
bool victronSeen = false;

// SD card manager - CYD microSD typical pins
// Non viene montata automaticamente in modo aggressivo: la monti/smonti da /sd.
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCLK 18
SPIClass sdSPI(HSPI);
bool sdMounted = false;
bool sdEverTried = false;
String sdLastStatus = "Mai montata";
unsigned long sdLastActionMs = 0;
unsigned long lastSdLogMs = 0;
String lastSdLogStatus = "Logger SD non ancora eseguito";
unsigned long lastSdLogOkMs = 0;

// Calibrazione touch guidata su TFT
// V10.4.54: calibrazione precisa a 5 punti, separata per rotazione schermo.
bool tftTouchCalMode = false;
int tftTouchCalStep = 0;
const int TOUCH_CAL_COUNT = 5;
const int TOUCH_CAL_SX[TOUCH_CAL_COUNT] = {24, 296, 296, 24, 160};
const int TOUCH_CAL_SY[TOUCH_CAL_COUNT] = {24, 24, 216, 216, 120};
const char* TOUCH_CAL_LABEL[TOUCH_CAL_COUNT] = {"alto sinistra", "alto destra", "basso destra", "basso sinistra", "centro"};
int tftCalRawX[TOUCH_CAL_COUNT] = {-1, -1, -1, -1, -1};
int tftCalRawY[TOUCH_CAL_COUNT] = {-1, -1, -1, -1, -1};
// Compatibilita' con la vecchia calibrazione a zone.
int tftCalLeftX = -1;
int tftCalLeftY = -1;
int tftCalHomeX = -1;
int tftCalHomeY = -1;
int tftCalRightX = -1;
int tftCalRightY = -1;
unsigned long tftTouchCalStartedMs = 0;
int toolsPendingAction = -1;
unsigned long toolsPendingActionMs = 0;


unsigned long bootCounter = 0;
unsigned long lastGoodWiFiMs = 0;
unsigned long lastGoodVictronMs = 0;
unsigned long lastDiagSaveMs = 0;
bool lowPowerMode = true;

// Calibrazione lettura batteria ESP/LiPo su GPIO34. Default: misurato 4.158V / ADC 1.944V ~= 2.14
float espBatMultiplier = 2.14f;
bool espBatMultiplierLoaded = false;

// Italia: CET/CEST automatico
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";


struct HistorySlot {
  float wh;
  float maxW;
  float maxPanelV;
  float battMax;
  float battMin;
  float battSum;
  float panelSum;
  float loadWh;
  uint16_t errors;
  uint16_t samples;
};

HistorySlot hourly[24];
HistorySlot daily[31];
HistorySlot monthly[12];

struct ChargeStateSlot {
  uint32_t offSec;
  uint32_t bulkSec;
  uint32_t absorptionSec;
  uint32_t floatSec;
  uint32_t storageSec;
  uint32_t otherSec;
  uint16_t samples;
};

ChargeStateSlot chHourly[24];
ChargeStateSlot chDaily[31];
ChargeStateSlot chMonthly[12];

// Prototipi espliciti: evitano che il preprocessor Arduino generi prototipi
// prima della definizione di HistorySlot con esp32 core 2.0.0.
void resetSlot(HistorySlot &s);
void resetChargeSlot(ChargeStateSlot &s);
void addChargeSecondsToSlot(ChargeStateSlot &s, uint32_t seconds);
void addSampleToSlot(HistorySlot &s, float wh, float pW, float bV, float pV);
String chargeJsonFields(const ChargeStateSlot &s);
String historyJsonArray(HistorySlot *arr, int count, int currentIndex, const char* labelPrefix);
float slotAvgPanel(const HistorySlot& s);
float slotAvgBatt(const HistorySlot& s);
bool loadChargeHistoryFromFs();
void saveChargeHistoryToFs();
float slotAvgPanel(const HistorySlot& s);
float slotAvgBatt(const HistorySlot& s);
float slotBattMin(const HistorySlot& s);

int currentHourIndex = -1;
int currentDayIndex = -1;
int currentMonthIndex = -1;

unsigned long lastHistoryMs = 0;
float lastPanelWForEnergy = NAN;


bool requireAuth() {
  if (server.authenticate(WEB_USER, WEB_PASS)) return true;
  server.requestAuthentication();
  return false;
}

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(LCD_BL, on ? HIGH : LOW);
}

int displayAutoOffSeconds() {
  prefs.begin("victron", true);
  int sec = prefs.getInt("lcd_auto_sec", -1);
  if (sec < 0) {
    int oldMin = prefs.getInt("lcd_auto_min", 0);
    sec = oldMin * 60;
  }
  prefs.end();
  if (sec < 0) sec = 0;
  if (sec > 86400) sec = 86400;
  return sec;
}

int displayAutoOffMinutes() {
  int sec = displayAutoOffSeconds();
  if (sec <= 0) return 0;
  return (sec + 59) / 60;
}

String displayAutoOffText() {
  int sec = displayAutoOffSeconds();
  if (sec <= 0) return "Disattivato";
  if (sec < 60) return String(sec) + " secondi";
  if (sec == 60) return "1 minuto";
  if (sec % 60 == 0) return String(sec / 60) + " minuti";
  return String(sec) + " secondi";
}

void loadDisplayRotationSetting() {
  prefs.begin("victron", true);
  int r = prefs.getInt("tft_rotation", 1);
  prefs.end();
  // Manteniamo solo landscape: 1 normale, 3 ruotato 180°.
  // Le rotazioni 0/2 romperebbero la UI 320x240.
  displayRotation = (r == 3) ? 3 : 1;
}

String displayRotationText() {
  return displayRotation == 3 ? "Ruotato 180 gradi" : "Normale";
}

void handleDisplayAutoOffLoop() {
  if (!backlightOn) return;
  if (millis() - lastDisplayAutoOffCheckMs < 1000UL) return;
  lastDisplayAutoOffCheckMs = millis();

  int sec = displayAutoOffSeconds();
  if (sec <= 0) return;
  if (lastUserActivityMs == 0) lastUserActivityMs = millis();

  unsigned long timeoutMs = (unsigned long)sec * 1000UL;
  if (millis() - lastUserActivityMs >= timeoutMs) {
    setBacklight(false);
    addEventLog("LCD", "Auto OFF dopo " + displayAutoOffText());
  }
}

String fmt(float v, int dec, const char* unit) {
  if (isnan(v)) return "N/D";
  return String(v, dec) + unit;
}

String fmtBytes(uint32_t bytes) {
  float mb = bytes / 1048576.0f;
  String out = String(mb, 2);
  out.replace(".", ",");
  return out + " MB (" + String(bytes) + " byte)";
}

String fmtBytesStr(String v) {
  if (v == "" || v == "N/D") return "N/D";
  uint32_t n = (uint32_t) strtoul(v.c_str(), nullptr, 10);
  return fmtBytes(n);
}

String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

String uptimeText() {
  unsigned long s = millis() / 1000;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600; s %= 3600;
  unsigned long m = s / 60;
  String out;
  if (d) out += String(d) + "g ";
  if (h) out += String(h) + "h ";
  out += String(m) + "m";
  return out;
}

String csText(int code) {
  switch (code) {
    case 0: return "Spento";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    case 6: return "Storage";
    case 7: return "Equalize";
    case 245: return "Wake-up";
    case 252: return "EXT Control";
    default: return String(code);
  }
}

String mpptText(int code) {
  switch (code) {
    case 0: return "Spento";
    case 1: return "Limitato tensione";
    case 2: return "MPPT attivo";
    default: return String(code);
  }
}

bool victronOnline() {
  return victronSeen && (millis() - lastVictronMs < 9000);
}

uint16_t stateColor() {
  if (!victronOnline()) return TFT_ORANGE;
  if (errorState != "0" && errorState != "N/D") return TFT_RED;
  if (chargeState == "Float") return TFT_GREEN;
  if (chargeState == "Bulk" || chargeState == "Absorption") return TFT_YELLOW;
  return TFT_CYAN;
}

int batteryPercent(float v) {
  if (isnan(v)) return -1;
  if (v >= 12.75) return 100;
  if (v >= 12.60) return 90;
  if (v >= 12.45) return 80;
  if (v >= 12.30) return 70;
  if (v >= 12.20) return 60;
  if (v >= 12.10) return 50;
  if (v >= 12.00) return 40;
  if (v >= 11.90) return 30;
  if (v >= 11.80) return 20;
  if (v >= 11.70) return 10;
  return 0;
}

void wakeDisplay() {
  lastUserActivityMs = millis();
}

void parseVictronLine(const String& line) {
  int tab = line.indexOf('\t');
  if (tab < 0) return;

  String key = line.substring(0, tab);
  String val = line.substring(tab + 1);
  key.trim();
  val.trim();

  lastRawLine = key + "=" + val;
  addVedirectRawLine(lastRawLine);

  if (key == "V") battV = val.toFloat() / 1000.0;
  else if (key == "I") battA = val.toFloat() / 1000.0;
  else if (key == "VPV") panelV = val.toFloat() / 1000.0;
  else if (key == "PPV") panelW = val.toFloat();
  else if (key == "H19") yieldTotalKWh = val.toFloat() * 0.01;
  else if (key == "H20") yieldTodayKWh = val.toFloat() * 0.01;
  else if (key == "H21") maxPowerToday = val.toFloat();
  else if (key == "H22") yieldYesterdayKWh = val.toFloat() * 0.01;
  else if (key == "H23") maxPowerYesterday = val.toFloat();
  else if (key == "CS") chargeState = csText(val.toInt());
  else if (key == "MPPT") mpptState = mpptText(val.toInt());
  else if (key == "ERR") errorState = val;
  else if (key == "PID") modelName = val;
  else if (key == "SER#") serialNumber = val;
  else if (key == "FW") firmwareVer = val;

  if (!isnan(battV) && !isnan(battA)) battW = battV * battA;

  vedirectParsedLineCount++;
  victronSeen = true;
  lastVictronMs = millis();
}

void restartVictronSerial(const String& reason) {
  VictronSerial.end();
  delay(20);
  rxLine = "";
  VictronSerial.begin(VICTRON_BAUD, SERIAL_8N1, pubCfg.veDirectRxPin, VICTRON_TX);
  lastVictronReinitMs = millis();
  vedirectReinitCount++;
  lastVictronReinitReason = reason;
  addVedirectRawLine(String("UART_RESTART=") + reason);
}

void readVictron() {
  while (VictronSerial.available()) {
    char c = (char)VictronSerial.read();
    vedirectByteCount++;
    lastVictronByteMs = millis();
    if (c == '\n') {
      rxLine.trim();
      if (rxLine.length()) {
        if (rxLine.indexOf('\t') >= 0) parseVictronLine(rxLine);
        else {
          vedirectBadLineCount++;
          addVedirectRawLine(String("BAD_LINE=") + rxLine);
        }
      }
      rxLine = "";
    } else if (c != '\r') {
      if (rxLine.length() < 120) rxLine += c;
      else {
        vedirectBadLineCount++;
        rxLine = "";
      }
    }
  }
}

void victronAutoRecoveryLoop() {
  const unsigned long now = millis();
  if (victronOnline()) return;
  if (now < 30000UL) return;
  if (now - lastVictronReinitMs < 30000UL) return;
  restartVictronSerial("auto_offline_30s");
}



void drawRoundCard(int x, int y, int w, int h, uint16_t border, const char* title) {
  tft.drawRoundRect(x, y, w, h, 8, border);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x + 8, y + 7);
  tft.print(title);
}

void drawNavFooter(int active, int pages) {
  const int y = 216;
  tft.fillRect(0, y, 320, 24, TFT_BLACK);
  tft.drawFastHLine(0, y, 320, TFT_DARKGREY);

  tft.drawRoundRect(8, y + 3, 58, 18, 5, TFT_DARKGREY);
  tft.drawRoundRect(109, y + 3, 102, 18, 5, (active == 0) ? TFT_CYAN : TFT_DARKGREY);
  tft.drawRoundRect(254, y + 3, 58, 18, 5, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(28, y + 8);  tft.print("<");
  tft.setCursor(145, y + 8); tft.print("HOME");
  tft.setCursor(281, y + 8); tft.print(">");

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(8, y - 11);
  tft.print(active + 1);
  tft.print("/");
  tft.print(pages);

  int shown = pages > 7 ? 7 : pages;
  int first = active - shown / 2;
  if (first < 0) first = 0;
  if (first + shown > pages) first = pages - shown;
  if (first < 0) first = 0;
  int dotsStart = 235;
  for (int i = 0; i < shown; i++) {
    int idx = first + i;
    uint16_t c = (idx == active) ? TFT_CYAN : TFT_DARKGREY;
    tft.fillCircle(dotsStart + i * 11, y - 6, 3, c);
  }
}

void drawSmallStatusFooter() {
  tft.fillRect(0, 202, 320, 13, TFT_BLACK);
  tft.setTextSize(1);

  int ac = alertCountNow();
  if (ac > 0) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(6, 204);
    tft.print("ALERT ");
    tft.print(ac);
    tft.print(": ");
    if (!victronOnline()) tft.print("VE.Direct No Data");
    else if (WiFi.status() != WL_CONNECTED) tft.print("WiFi offline");
    else if (!isnan(espBatteryPercent()) && espBatteryPercent() < 25) tft.print("BAT ESP bassa");
    else if (errorState != "0" && errorState != "N/D") { tft.print("MPPT ERR "); tft.print(errorState); }
    else tft.print("Controlla web");
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(6, 204);
    tft.print(FW_VERSION);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(220, 204);
  tft.print(WiFi.localIP());
  drawNavFooter(page, TFT_PAGE_COUNT);
}

void drawMiniBar(int x, int y, int w, int h, float value, float maxValue, uint16_t color) {
  tft.drawRoundRect(x, y, w, h, 4, TFT_DARKGREY);
  if (isnan(value) || maxValue <= 0) return;
  int fill = (int)((value / maxValue) * (w - 4));
  if (fill < 0) fill = 0;
  if (fill > w - 4) fill = w - 4;
  tft.fillRoundRect(x + 2, y + 2, fill, h - 4, 3, color);
}


void drawTopBar(const char* title) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRoundRect(0, 0, 320, 34, 0, TFT_NAVY);
  tft.drawFastHLine(0, 34, 320, TFT_BLUE);

  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(8, 7);
  tft.print(title);

  uint16_t wifiCol = WiFi.isConnected() ? TFT_GREEN : TFT_RED;
  uint16_t veCol = victronOnline() ? TFT_GREEN : TFT_ORANGE;
  uint16_t sdCol = sdMounted ? TFT_GREEN : TFT_DARKGREY;
  int hs = healthScoreNow();
  uint16_t hCol = hs >= 85 ? TFT_GREEN : (hs >= 65 ? TFT_YELLOW : TFT_RED);

  tft.setTextSize(1);
  tft.fillRoundRect(190, 4, 38, 12, 4, wifiCol);
  tft.setTextColor(TFT_BLACK, wifiCol);
  tft.setCursor(197, 6); tft.print("WiFi");

  tft.fillRoundRect(232, 4, 30, 12, 4, veCol);
  tft.setTextColor(TFT_BLACK, veCol);
  tft.setCursor(240, 6); tft.print("VE");

  tft.fillRoundRect(266, 4, 24, 12, 4, sdCol);
  tft.setTextColor(TFT_BLACK, sdCol);
  tft.setCursor(271, 6); tft.print("SD");

  tft.setTextColor(hCol, TFT_NAVY);
  tft.setCursor(196, 21);
  tft.print("Health ");
  tft.print(hs);
  tft.print("/100");
}

void drawBatteryIcon(int x, int y, int pct) {
  tft.drawRect(x, y, 72, 30, TFT_WHITE);
  tft.fillRect(x + 72, y + 8, 5, 14, TFT_WHITE);
  int fillW = 0;
  uint16_t col = TFT_DARKGREY;

  if (pct >= 0) {
    fillW = map(pct, 0, 100, 0, 68);
    col = pct > 60 ? TFT_GREEN : (pct > 30 ? TFT_YELLOW : TFT_RED);
  }

  tft.fillRect(x + 2, y + 2, fillW, 26, col);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(x + 18, y + 10);
  if (pct >= 0) tft.printf("%d%%", pct);
  else tft.print("--%");
}


String tftFmt(float v, int dec, const char* unit) {
  if (isnan(v)) return String("--") + unit;
  return String(v, dec) + unit;
}

void drawWifiBars(int x, int y, int rssi) {
  int bars = 0;
  if (WiFi.isConnected()) {
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else bars = 1;
  }
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 4;
    uint16_t c = i < bars ? TFT_GREEN : TFT_DARKGREY;
    tft.fillRect(x + i * 7, y + (18 - h), 5, h, c);
  }
}

void drawFooterDots(int active, int pages) {
  drawNavFooter(active, pages);
}

void drawOverviewPage() {
  drawTopBar("Victron GX");

  int battPct = batteryPercent(battV);
  uint16_t st = stateColor();

  // Card solare grande
  tft.drawRoundRect(8, 40, 304, 58, 10, TFT_YELLOW);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 48);
  tft.print("SOLARE");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(4);
  tft.setCursor(18, 64);
  if (isnan(panelW)) tft.print("-- W");
  else tft.printf("%.0f W", panelW);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(190, 58);
  tft.print("PV ");
  if (isnan(panelV)) tft.print("--.-V");
  else tft.printf("%.1fV", panelV);
  tft.setCursor(190, 74);
  tft.print("Oggi ");
  if (isnan(yieldTodayKWh)) tft.print("-- kWh");
  else tft.printf("%.2fkWh", yieldTodayKWh);
  drawMiniBar(18, 88, 280, 7, isnan(panelW) ? 0 : panelW, 120.0, TFT_YELLOW);

  // Card batteria
  tft.drawRoundRect(8, 106, 304, 55, 10, TFT_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 114);
  tft.print("BATTERIA");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(18, 132);
  if (isnan(battV)) tft.print("--.- V");
  else tft.printf("%.2f V", battV);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(178, 133);
  if (isnan(battA)) tft.print("--.- A");
  else tft.printf("%.2f A", battA);
  drawMiniBar(18, 153, 190, 6, battPct < 0 ? 0 : battPct, 100.0, TFT_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(220, 151);
  if (battPct >= 0) tft.printf("%d%% stim.", battPct);
  else tft.print("SOC N/D");

  // Stato
  tft.drawRoundRect(8, 168, 304, 40, 10, st);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 176);
  tft.print("STATO");
  tft.setTextColor(st, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(18, 190);
  if (victronOnline()) tft.print(chargeState);
  else tft.print("Victron non connesso");

  drawWifiBars(260, 183, WiFi.RSSI());
  drawNavFooter(page, TFT_PAGE_COUNT);
}


void drawEspBatteryPage() {
  drawTopBar("Batt ESP");

  float lipoV = espBatteryVoltage();
  float pctF = espBatteryPercent();
  int pct = isnan(pctF) ? -1 : (int)(pctF + 0.5f);
  if (pct > 100) pct = 100;
  if (pct < 0 && !isnan(pctF)) pct = 0;

  uint16_t col = TFT_DARKGREY;
  if (!isnan(pctF)) col = pctF > 65 ? TFT_GREEN : (pctF > 30 ? TFT_YELLOW : TFT_RED);

  // Card principale stile GX: tensione grande + icona batteria ampia
  tft.drawRoundRect(8, 40, 304, 102, 10, col);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 48);
  tft.print("BATTERIA TAMPONE ESP / LiPo");

  tft.setTextSize(4);
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(18, 70);
  if (isnan(lipoV)) tft.print("--.-V");
  else tft.printf("%.2fV", lipoV);

  // Batteria grande a destra
  int bx = 205, by = 64, bw = 82, bh = 44;
  tft.drawRoundRect(bx, by, bw, bh, 5, TFT_WHITE);
  tft.fillRoundRect(bx + bw, by + 13, 7, 18, 3, TFT_WHITE);
  int fw = (pct >= 0) ? map(pct, 0, 100, 0, bw - 8) : 0;
  if (fw < 0) fw = 0;
  if (fw > bw - 8) fw = bw - 8;
  tft.fillRoundRect(bx + 4, by + 4, fw, bh - 8, 3, col);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, col);
  tft.setCursor(bx + 22, by + 14);
  if (pct >= 0) tft.printf("%d%%", pct);
  else tft.print("--");

  // Barra percentuale sotto la tensione
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 116);
  tft.print("Carica stimata");
  tft.drawRoundRect(105, 113, 185, 14, 4, TFT_DARKGREY);
  if (pct >= 0) {
    int bar = map(pct, 0, 100, 0, 181);
    tft.fillRoundRect(107, 115, bar, 10, 3, col);
  }

  // Due card piccole: percentuale e stato
  tft.drawRoundRect(8, 150, 145, 44, 8, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 158);
  tft.print("Percentuale");
  tft.setTextSize(2);
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(18, 174);
  if (pct >= 0) tft.printf("%d%%", pct);
  else tft.print("N/D");

  tft.drawRoundRect(167, 150, 145, 44, 8, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(177, 158);
  tft.print("Stato");
  tft.setTextSize(2);
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(177, 174);
  tft.print(espBatteryStatusText());

  // Riga tecnica compatta
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 200);
  tft.print("LiPo: ");
  tft.print(espBatteryConnectionText());
  tft.print("  GPIO34");

  drawNavFooter(page, TFT_PAGE_COUNT);
}

void drawHistoryTftPage() {
  drawTopBar("Storico PV");

  // V10.4.30 HISTORY PLUS:
  // grafico leggero a 24 barre + statistiche max/media/energia.
  // Niente refresh rapido, niente animazioni e niente fill multipli.
  float maxv = 10.0;
  float sumMaxW = 0.0;
  int valid = 0;
  float wh24 = 0.0;
  float battMin24 = 999.0;
  float battMax24 = 0.0;

  for (int i = 0; i < 24; i++) {
    if (hourly[i].samples > 0) {
      if (hourly[i].maxW > maxv) maxv = hourly[i].maxW;
      sumMaxW += hourly[i].maxW;
      wh24 += hourly[i].wh;
      if (hourly[i].battMin < battMin24) battMin24 = hourly[i].battMin;
      if (hourly[i].battMax > battMax24) battMax24 = hourly[i].battMax;
      valid++;
    }
  }

  float avgMaxW = valid > 0 ? (sumMaxW / valid) : NAN;

  drawRoundCard(8, 40, 304, 126, TFT_CYAN, "Potenza solare ultime 24 letture");

  const int chartX = 20;
  const int chartY = 62;
  const int chartW = 280;
  const int chartH = 82;
  const int baseY = chartY + chartH;

  // assi leggeri
  tft.drawFastHLine(chartX, baseY, chartW, TFT_DARKGREY);
  tft.drawFastVLine(chartX, chartY, chartH, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(chartX + 3, chartY + 2);
  tft.printf("%.0fW", maxv);
  tft.setCursor(chartX + 3, baseY - 10);
  tft.print("0W");

  for (int i = 0; i < 24; i++) {
    int idx = (currentHourIndex + 1 + i) % 24;
    int h = 0;
    if (hourly[idx].samples > 0 && maxv > 0) {
      h = (int)((hourly[idx].maxW / maxv) * (chartH - 12));
    }
    if (h < 1 && hourly[idx].samples > 0) h = 1;
    if (h > chartH - 12) h = chartH - 12;

    int x = chartX + 14 + i * 11;
    uint16_t c = hourly[idx].samples > 0 ? TFT_YELLOW : TFT_DARKGREY;
    tft.fillRect(x, baseY - h, 7, h, c);
  }

  // Card statistiche
  drawRoundCard(8, 172, 96, 37, TFT_DARKGREY, "Max");
  drawRoundCard(112, 172, 96, 37, TFT_DARKGREY, "Media");
  drawRoundCard(216, 172, 96, 37, TFT_DARKGREY, "Energia");

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(18, 190);
  tft.printf("%.0fW", maxv);

  tft.setCursor(122, 190);
  if (isnan(avgMaxW)) tft.print("--W");
  else tft.printf("%.0fW", avgMaxW);

  tft.setCursor(226, 190);
  if (!isnan(yieldTodayKWh)) tft.printf("%.2fk", yieldTodayKWh);
  else tft.printf("%.0fWh", wh24);

  // Riga secondaria batteria 24h
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 211);
  tft.print("Batt 24h: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (valid > 0 && battMin24 < 900.0) {
    tft.printf("%.2f-%.2fV", battMin24, battMax24);
  } else {
    tft.print("N/D");
  }

  drawNavFooter(page, TFT_PAGE_COUNT);
}

void drawOtaTftPage() {
  drawTopBar("OTA Cloud");
  drawRoundCard(8, 44, 304, 68, TFT_CYAN, "Firmware");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(18, 68);
  tft.print(FW_VERSION);
  tft.setCursor(18, 84);
  tft.print("Build: ");
  tft.print(buildText());

  drawRoundCard(8, 124, 304, 82, TFT_DARKGREY, "Aggiornamenti");
  tft.setTextSize(2);
  tft.setCursor(18, 152);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print("GitHub OTA");
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(18, 180);
  tft.print("Web: /updates  /github-update");
  drawFooterDots(page, TFT_PAGE_COUNT);
}

void drawBatteryPage() {
  drawTopBar("Batteria");

  int pct = batteryPercent(battV);

  drawRoundCard(8, 44, 304, 80, TFT_DARKGREY, "Tensione batteria");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(4);
  tft.setCursor(18, 73);
  if (isnan(battV)) tft.print("--.--V");
  else tft.printf("%.2fV", battV);

  drawBatteryIcon(230, 72, pct);

  drawRoundCard(8, 134, 145, 55, TFT_DARKGREY, "Corrente");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(18, 160);
  if (isnan(battA)) tft.print("--.-- A");
  else tft.printf("%.2f A", battA);

  drawRoundCard(167, 134, 145, 55, TFT_DARKGREY, "Potenza");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(177, 160);
  if (isnan(battW)) tft.print("--.- W");
  else tft.printf("%.1f W", battW);

  tft.setTextSize(2);
  tft.setCursor(10, 202);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.print("Stato: ");
  tft.setTextColor(stateColor(), TFT_BLACK);
  tft.print(chargeState);

  drawSmallStatusFooter();
}

void drawSolarPage() {
  drawTopBar("Solare");

  drawRoundCard(8, 44, 304, 92, TFT_DARKGREY, "Potenza pannello");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(5);
  tft.setCursor(18, 76);
  if (isnan(panelW)) tft.print("--W");
  else tft.printf("%.0fW", panelW);

  drawMiniBar(18, 121, 280, 8, isnan(panelW) ? 0 : panelW, 120.0, TFT_YELLOW);

  drawRoundCard(8, 146, 145, 52, TFT_DARKGREY, "Tensione PV");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(18, 171);
  if (isnan(panelV)) tft.print("--.-- V");
  else tft.printf("%.2f V", panelV);

  drawRoundCard(167, 146, 145, 52, TFT_DARKGREY, "Oggi");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(177, 171);
  if (isnan(yieldTodayKWh)) tft.print("-- kWh");
  else tft.printf("%.2f kWh", yieldTodayKWh);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 207);
  tft.print("Max oggi: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (isnan(maxPowerToday)) tft.print("--W");
  else tft.printf("%.0fW", maxPowerToday);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.print("  MPPT: ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print(mpptState);

  drawSmallStatusFooter();
}

void drawSystemPage() {
  drawTopBar("Sistema");

  drawRoundCard(8, 44, 304, 52, TFT_DARKGREY, "Indirizzo");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(18, 68);
  tft.print(WiFi.localIP());

  drawRoundCard(8, 106, 145, 54, TFT_DARKGREY, "WiFi");
  tft.setTextSize(2);
  tft.setTextColor(WiFi.isConnected() ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.setCursor(18, 132);
  tft.print(WiFi.isConnected() ? "Online" : "Offline");

  drawRoundCard(167, 106, 145, 54, TFT_DARKGREY, "Segnale");
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(177, 132);
  tft.printf("%d dBm", WiFi.RSSI());

  drawRoundCard(8, 170, 304, 45, TFT_DARKGREY, "Retroilluminazione");
  tft.setTextSize(2);
  tft.setCursor(18, 193);
  tft.setTextColor(backlightOn ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  tft.print(backlightOn ? "ON" : "OFF");

  drawSmallStatusFooter();
}

void drawDebugPage() {
  drawTopBar("Debug");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  drawRoundCard(8, 44, 304, 44, TFT_DARKGREY, "Firmware");
  tft.setCursor(18, 68);
  tft.print(FW_VERSION);

  drawRoundCard(8, 98, 304, 44, TFT_DARKGREY, "Victron model");
  tft.setCursor(18, 122);
  tft.print(modelName);

  drawRoundCard(8, 152, 304, 44, TFT_DARKGREY, "Seriale");
  tft.setCursor(18, 176);
  tft.print(serialNumber);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 211);
  tft.print("Raw: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(lastRawLine);

  drawSmallStatusFooter();
}



void drawRecapPage() {
  drawTopBar("Recap");
  HistorySlot *today = (currentDayIndex >= 0) ? &daily[currentDayIndex] : nullptr;
  int yIdx = (currentDayIndex >= 0) ? ((currentDayIndex + 30) % 31) : -1;
  HistorySlot *yday = (yIdx >= 0) ? &daily[yIdx] : nullptr;
  HistorySlot *month = (currentMonthIndex >= 0) ? &monthly[currentMonthIndex] : nullptr;
  float todayWh = today ? today->wh : 0;
  float yesterdayWh = yday ? yday->wh : 0;
  float monthWh = month ? month->wh : lastNDaysWh(31);
  float battMinToday = today ? slotBattMin(*today) : NAN;
  float battMaxToday = today ? today->battMax : NAN;

  drawRoundCard(8, 40, 145, 55, TFT_ORANGE, "Oggi");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(18, 62); tft.print(todayWh / 1000.0f, 2); tft.print(" kWh");

  drawRoundCard(167, 40, 145, 55, TFT_CYAN, "Ieri");
  tft.setTextSize(2);
  tft.setCursor(177, 62); tft.print(yesterdayWh / 1000.0f, 2); tft.print(" kWh");

  drawRoundCard(8, 104, 145, 55, TFT_GREEN, "Mese");
  tft.setTextSize(2);
  tft.setCursor(18, 126); tft.print(monthWh / 1000.0f, 1); tft.print(" kWh");

  drawRoundCard(167, 104, 145, 55, TFT_BLUE, "Max PV");
  tft.setTextSize(2);
  tft.setCursor(177, 126); tft.print(today ? today->maxW : panelW, 0); tft.print(" W");

  drawRoundCard(8, 168, 304, 38, victronOnline() ? TFT_DARKGREEN : TFT_ORANGE, "Stato sistema");
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(18, 188);
  tft.print("VE: "); tft.print(victronOnline() ? "OK" : "No Data");
  tft.print("  SD: "); tft.print(sdMounted ? "OK" : "NO");
  tft.print("  BAT: ");
  if (isnan(espBatteryPercent())) tft.print("N/D"); else { tft.print(espBatteryPercent(), 0); tft.print("%"); }
  tft.setCursor(18, 200);
  tft.print("Batt min/max: ");
  if (isnan(battMinToday) || isnan(battMaxToday)) tft.print("N/D");
  else { tft.print(battMinToday, 2); tft.print("/"); tft.print(battMaxToday, 2); tft.print("V"); }

  drawNavFooter(page, TFT_PAGE_COUNT);
}

void drawToolsPage() {
  drawTopBar("Tools");

  drawRoundCard(8, 40, 304, 78, TFT_CYAN, "Diagnostica rapida");
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(18, 61);  tft.print("IP: "); tft.print(WiFi.localIP());
  tft.setCursor(18, 77);  tft.print("WiFi: "); tft.print(WiFi.RSSI()); tft.print(" dBm");
  tft.setCursor(170, 77); tft.print("VE: "); tft.print(victronOnline() ? "OK" : "No Data");
  tft.setCursor(18, 93);  tft.print("BAT ESP: ");
  if (isnan(espBatteryVoltage())) tft.print("N/D");
  else { tft.print(espBatteryVoltage(), 2); tft.print("V "); tft.print(espBatteryPercent(), 0); tft.print("%"); }
  tft.setCursor(170, 93); tft.print("LiPo: "); tft.print(espBatteryConnectionText());
  tft.setCursor(18, 109); tft.print("SD: "); tft.print(sdMounted ? "montata" : "smontata");
  if (sdMounted) { tft.print(" "); tft.print(formatBytes64(SD.usedBytes())); tft.print("/"); tft.print(formatBytes64(SD.totalBytes())); }
  tft.setCursor(170, 109); tft.print("Sto: "); tft.print(storageMode());
  tft.setCursor(18, 121); tft.print("LCD auto: "); tft.print(displayAutoOffText());

  drawRoundCard(8, 126, 304, 42, TFT_DARKGREY, "Sistema");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(18, 146); tft.print("Heap: "); tft.print(ESP.getFreeHeap() / 1024); tft.print(" KB");
  tft.setCursor(170, 146); tft.print("Up: "); tft.print(uptimeText());
  tft.setCursor(18, 160); tft.print(FW_VERSION);

  // Azioni vere solo nella fascia pulsanti. Il footer sotto resta navigazione.
  tft.fillRoundRect(6, 176, 73, 28, 6, TFT_DARKGREY);
  tft.fillRoundRect(84, 176, 73, 28, 6, TFT_BLUE);
  tft.fillRoundRect(163, 176, 73, 28, 6, TFT_PURPLE);
  tft.fillRoundRect(241, 176, 73, 28, 6, TFT_RED);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(20, 186); tft.print("LCD OFF");
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(93, 186); tft.print("T CAL");
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.setCursor(180, 186); tft.print("ROTATE");
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setCursor(257, 186); tft.print("REBOOT");

  drawNavFooter(page, TFT_PAGE_COUNT);
}

void drawCrosshair(int x, int y, uint16_t color) {
  tft.drawCircle(x, y, 14, color);
  tft.drawCircle(x, y, 15, color);
  tft.drawLine(x - 22, y, x + 22, y, color);
  tft.drawLine(x, y - 22, x, y + 22, color);
  tft.fillCircle(x, y, 3, color);
}

void drawTouchCalTftPage() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 32, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(2);
  tft.setCursor(8, 7);
  tft.print("Touch Cal Pro");

  if (tftTouchCalStep < 0 || tftTouchCalStep >= TOUCH_CAL_COUNT) tftTouchCalStep = 0;

  int sx = TOUCH_CAL_SX[tftTouchCalStep];
  int sy = TOUCH_CAL_SY[tftTouchCalStep];

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, 44);
  tft.print("Tocca il CENTRO della croce");
  tft.setCursor(14, 60);
  tft.print("Punto "); tft.print(tftTouchCalStep + 1); tft.print("/5: ");
  tft.print(TOUCH_CAL_LABEL[tftTouchCalStep]);

  // Croci di riferimento: quella attiva e' verde, le altre sono grigie.
  for (int i = 0; i < TOUCH_CAL_COUNT; i++) {
    uint16_t c = (i == tftTouchCalStep) ? TFT_GREEN : TFT_DARKGREY;
    drawCrosshair(TOUCH_CAL_SX[i], TOUCH_CAL_SY[i], c);
  }

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(14, 184);
  tft.print("Raw ultimo: X="); tft.print(lastTouchRawX);
  tft.print(" Y="); tft.print(lastTouchRawY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(14, 202);
  tft.print("Rotazione "); tft.print(displayRotation == 3 ? "180" : "normale");
  tft.print(" - timeout 120s");
}

String touchPreciseKey(const char* base) {
  return String("tp_") + base + "_r" + String(displayRotation);
}

void savePrecisionTouchCalibration() {
  // Salva raw dei 5 punti per la rotazione corrente.
  for (int i = 0; i < TOUCH_CAL_COUNT; i++) {
    prefsPutIntSafe(touchPreciseKey((String("x") + String(i)).c_str()).c_str(), tftCalRawX[i]);
    prefsPutIntSafe(touchPreciseKey((String("y") + String(i)).c_str()).c_str(), tftCalRawY[i]);
  }
  prefsPutIntSafe(touchPreciseKey("valid").c_str(), 1);

  // Mantieni anche i vecchi riferimenti a zone per compatibilita'/fallback.
  prefsPutTouchCal("touch_x_left",  tftCalRawX[0]);
  prefsPutTouchCal("touch_x_right", tftCalRawX[1]);
  prefsPutTouchCal("touch_x_home",  tftCalRawX[4]);
  prefsPutTouchCal("touch_y_home",  tftCalRawY[4]);

  addEventLog("TOUCH", "Calibrazione precisa salvata rot=" + String(displayRotation));
}

bool touchPrecisionAvailable() {
  return prefsGetIntSafe(touchPreciseKey("valid").c_str(), 0) == 1;
}


bool touchZonesEnabled() {
  // 0 = solo pulsanti visibili (default, più sicuro)
  // 1 = zone laterali pagina precedente/successiva, stile vecchio
  return prefsGetIntSafe("touch_nav_mode", 0) == 1;
}

String touchNavigationModeText() {
  return touchZonesEnabled() ? "Zone laterali" : "Solo pulsanti";
}

int touchPreciseGet(const char* axis, int idx, int fallback) {
  String k = touchPreciseKey((String(axis) + String(idx)).c_str());
  return prefsGetIntSafe(k.c_str(), fallback);
}

float mapFloatClamp(float v, float inA, float inB, float outA, float outB) {
  if (fabs(inB - inA) < 1.0f) return (outA + outB) * 0.5f;
  float r = outA + (v - inA) * (outB - outA) / (inB - inA);
  float mn = min(outA, outB), mx = max(outA, outB);
  if (r < mn) r = mn;
  if (r > mx) r = mx;
  return r;
}

bool mapTouchRawToScreen(int rawX, int rawY, int* sx, int* sy) {
  if (!touchPrecisionAvailable()) return false;

  int rx[TOUCH_CAL_COUNT];
  int ry[TOUCH_CAL_COUNT];
  for (int i = 0; i < TOUCH_CAL_COUNT; i++) {
    rx[i] = touchPreciseGet("x", i, -1);
    ry[i] = touchPreciseGet("y", i, -1);
    if (rx[i] < 50 || rx[i] > 4095 || ry[i] < 50 || ry[i] > 4095) return false;
  }

  // I punti sono: 0 TL, 1 TR, 2 BR, 3 BL, 4 centro.
  float leftRawX   = (rx[0] + rx[3]) * 0.5f;
  float rightRawX  = (rx[1] + rx[2]) * 0.5f;
  float leftRawY   = (ry[0] + ry[3]) * 0.5f;
  float rightRawY  = (ry[1] + ry[2]) * 0.5f;
  float topRawX    = (rx[0] + rx[1]) * 0.5f;
  float bottomRawX = (rx[2] + rx[3]) * 0.5f;
  float topRawY    = (ry[0] + ry[1]) * 0.5f;
  float bottomRawY = (ry[2] + ry[3]) * 0.5f;

  bool screenXUsesRawX = fabs(rightRawX - leftRawX) >= fabs(rightRawY - leftRawY);
  bool screenYUsesRawY = fabs(bottomRawY - topRawY) >= fabs(bottomRawX - topRawX);

  float xVal = screenXUsesRawX ? rawX : rawY;
  float xA   = screenXUsesRawX ? leftRawX : leftRawY;
  float xB   = screenXUsesRawX ? rightRawX : rightRawY;

  float yVal = screenYUsesRawY ? rawY : rawX;
  float yA   = screenYUsesRawY ? topRawY : topRawX;
  float yB   = screenYUsesRawY ? bottomRawY : bottomRawX;

  int mx = (int)(mapFloatClamp(xVal, xA, xB, 24.0f, 296.0f) + 0.5f);
  int my = (int)(mapFloatClamp(yVal, yA, yB, 24.0f, 216.0f) + 0.5f);

  if (mx < 0) mx = 0; if (mx > 319) mx = 319;
  if (my < 0) my = 0; if (my > 239) my = 239;
  if (sx) *sx = mx;
  if (sy) *sy = my;
  return true;
}

int touchZoneFromScreenX(int sx) {
  if (sx < 90) return 0;
  if (sx > 230) return 2;
  return 1;
}

// V10.4.56: navigazione SOLO sui pulsanti visibili del footer.
// Evita cambio pagina quando tocchi card/dati nella parte sinistra/destra del display.
int navButtonFromScreen(int sx, int sy) {
  if (sy < 210 || sy > 239) return -1;
  if (sx >= 0 && sx <= 82) return 0;       // freccia sinistra
  if (sx >= 96 && sx <= 224) return 1;     // HOME
  if (sx >= 238 && sx <= 319) return 2;    // freccia destra
  return -1;
}

// Pulsanti azione della pagina Tools: LCD OFF / TOUCH CAL / REBOOT.
int toolsButtonFromScreen(int sx, int sy) {
  if (sy < 170 || sy > 210) return -1;
  if (sx >= 0 && sx <= 82) return 0;       // LCD OFF
  if (sx >= 80 && sx <= 160) return 1;     // TOUCH CAL
  if (sx >= 160 && sx <= 240) return 2;    // ROTATE
  if (sx >= 238 && sx <= 319) return 3;    // REBOOT
  return -1;
}

int navButtonFromRaw(int rawX, int rawY) {
  int sx = 160, sy = 120;
  if (mapTouchRawToScreen(rawX, rawY, &sx, &sy)) return navButtonFromScreen(sx, sy);

  // Fallback vecchio: tenta una stima Y, ma accetta solo tocchi molto in basso.
  int approxY = map(rawY, 250, 3850, 0, 240);
  if (approxY < 0) approxY = 0;
  if (approxY > 240) approxY = 240;
  if (displayRotation == 3) approxY = 240 - approxY;
  if (approxY < 210) return -1;
  return touchZoneFromRawX(rawX);
}

void finishTftTouchCalibration() {
  bool ok = true;
  for (int i = 0; i < TOUCH_CAL_COUNT; i++) {
    if (tftCalRawX[i] < 50 || tftCalRawY[i] < 50 || tftCalRawX[i] > 4095 || tftCalRawY[i] > 4095) ok = false;
  }
  // Controllo minimo: gli angoli devono essere abbastanza distanti sul touch raw.
  if (abs(tftCalRawX[1] - tftCalRawX[0]) + abs(tftCalRawY[1] - tftCalRawY[0]) < 250) ok = false;
  if (abs(tftCalRawX[3] - tftCalRawX[0]) + abs(tftCalRawY[3] - tftCalRawY[0]) < 250) ok = false;

  tft.fillScreen(TFT_BLACK);
  if (ok) {
    savePrecisionTouchCalibration();
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(22, 72); tft.print("Touch calibrato");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(18, 112); tft.print("Calibrazione precisa 5 punti salvata.");
    tft.setCursor(18, 130); tft.print("Rotazione: "); tft.print(displayRotation == 3 ? "180" : "normale");
    delay(1800);
  } else {
    addEventLog("TOUCH", "Cal precisa fallita");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(18, 72); tft.print("Calibrazione fallita");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(18, 112); tft.print("Punti non validi/troppo vicini.");
    tft.setCursor(18, 130); tft.print("Fallback vecchie zone mantenuto.");
    delay(1300);
  }
  tftTouchCalMode = false;
  tftTouchCalStep = 0;
  page = TFT_PAGE_COUNT - 1;
  lastDrawMs = millis();
  drawDashboard();
}

void handleTouchCalibrationTap(int rawX, int rawY) {
  if (!tftTouchCalMode) return;
  if (tftTouchCalStep < 0 || tftTouchCalStep >= TOUCH_CAL_COUNT) tftTouchCalStep = 0;

  tftCalRawX[tftTouchCalStep] = rawX;
  tftCalRawY[tftTouchCalStep] = rawY;
  addEventLog("TOUCH", "Cal punto " + String(tftTouchCalStep + 1) + " raw=" + String(rawX) + "," + String(rawY));

  tftTouchCalStep++;
  if (tftTouchCalStep >= TOUCH_CAL_COUNT) {
    finishTftTouchCalibration();
  } else {
    drawTouchCalTftPage();
  }
}

void startTftTouchCalibration() {
  tftTouchCalMode = true;
  tftTouchCalStep = 0;
  for (int i = 0; i < TOUCH_CAL_COUNT; i++) { tftCalRawX[i] = -1; tftCalRawY[i] = -1; }
  tftCalLeftX = tftCalLeftY = tftCalHomeX = tftCalHomeY = tftCalRightX = tftCalRightY = -1;
  tftTouchCalStartedMs = millis();
  addEventLog("TOUCH", "Calibrazione precisa TFT avviata");
  drawTouchCalTftPage();
}

int touchZoneFromRawX(int rawX) {
  int sx = 160, sy = 120;
  if (mapTouchRawToScreen(rawX, lastTouchRawY, &sx, &sy)) return touchZoneFromScreenX(sx);

  int sampleLeft = prefsGetTouchCal("touch_x_left", -1);
  int sampleRight = prefsGetTouchCal("touch_x_right", -1);
  int zone = 1;
  if (sampleLeft > 100 && sampleRight > 100 && abs(sampleRight - sampleLeft) > 250) {
    float nx = ((float)(rawX - sampleLeft) * 320.0f) / (float)(sampleRight - sampleLeft);
    if (nx < 0) nx = 0;
    if (nx > 320) nx = 320;
    if (nx < 106) zone = 0;
    else if (nx > 213) zone = 2;
    else zone = 1;
  } else {
    if (rawX < 1500) zone = 0;
    else if (rawX > 2600) zone = 2;
    else zone = 1;
    if (displayRotation == 3) zone = 2 - zone;
  }
  return zone;
}

bool toolsActionBandFromRawY(int rawY) {
  // I pulsanti Tools sono tra Y 176 e 204. Il footer navigazione e' sotto ~214.
  int sx = 160, sy = 120;
  if (mapTouchRawToScreen(lastTouchRawX, rawY, &sx, &sy)) {
    return sy >= 166 && sy <= 208;
  }

  // Fallback: usa la vecchia separazione basata su HOME raw Y.
  int homeY = prefsGetTouchCal("touch_y_home", -1);
  if (homeY > 100 && homeY < 4095) {
    return abs(rawY - homeY) < 1150;
  }

  int approxY = map(rawY, 250, 3850, 0, 240);
  if (approxY < 0) approxY = 0;
  if (approxY > 240) approxY = 240;
  if (displayRotation == 3) approxY = 240 - approxY;
  return approxY >= 166 && approxY <= 210;
}

void navigateByZone(int zone) {
  if (zone == 0) {
    page--;
    if (page < 0) page = TFT_PAGE_COUNT - 1;
  } else if (zone == 2) {
    page++;
    if (page >= TFT_PAGE_COUNT) page = 0;
  } else {
    page = 0;
  }
}

void drawToolsConfirm(const String& msg, uint16_t color) {
  tft.fillRect(8, 205, 304, 10, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(10, 207);
  tft.print(msg);
}


void toggleTftRotationFromTools() {
  displayRotation = (displayRotation == 3) ? 1 : 3;
  prefs.begin("victron", false);
  prefs.putInt("tft_rotation", displayRotation);
  prefs.end();
  addEventLog("LCD", "Rotazione TFT da Tools: " + displayRotationText());
  tft.setRotation(displayRotation);
  page = TFT_PAGE_COUNT - 1;
  drawToolsPage();
  drawToolsConfirm("Rotazione: " + displayRotationText(), TFT_CYAN);
}

void handleToolsTouch(int rawX, int rawY) {
  int sx = 160, sy = 120;
  bool mapped = mapTouchRawToScreen(rawX, rawY, &sx, &sy);

  if (mapped) {
    int toolBtn = toolsButtonFromScreen(sx, sy);
    int navBtn = navButtonFromScreen(sx, sy);

    if (toolBtn < 0 && navBtn >= 0) {
      navigateByZone(navBtn);
      addEventLog("TOUCH", "Tools footer nav sx=" + String(sx) + " sy=" + String(sy));
      drawDashboard();
      return;
    }

    if (toolBtn < 0) {
      addEventLog("TOUCH", "Tools touch ignorato sx=" + String(sx) + " sy=" + String(sy));
      return;
    }

    if (toolBtn == 0) {
      setBacklight(false);
      addEventLog("LCD", "Display spento da TOOLS");
      return;
    }
    if (toolBtn == 1) {
      startTftTouchCalibration();
      return;
    }
    if (toolBtn == 2) {
      toggleTftRotationFromTools();
      return;
    }

    // REBOOT con conferma: evita riavvii accidentali.
    if (toolsPendingAction != 3 || millis() - toolsPendingActionMs > 2500UL) {
      toolsPendingAction = 3;
      toolsPendingActionMs = millis();
      drawToolsConfirm("Tocca REBOOT ancora per confermare", TFT_ORANGE);
      addEventLog("SYS", "Reboot Tools in attesa conferma");
      return;
    }
    toolsPendingAction = -1;
    addEventLog("SYS", "Reboot da TOOLS");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(70, 104);
    tft.print("Riavvio...");
    delay(350);
    ESP.restart();
    return;
  }

  // Fallback senza calibrazione precisa: conserva sicurezza, ma solo se il tocco e' nella fascia bassa/footer o pulsanti.
  int zone = touchZoneFromRawX(rawX);
  if (!toolsActionBandFromRawY(rawY)) {
    int navBtn = navButtonFromRaw(rawX, rawY);
    if (navBtn >= 0) {
      navigateByZone(navBtn);
      addEventLog("TOUCH", "Tools footer nav raw x=" + String(rawX) + " y=" + String(rawY));
      drawDashboard();
    } else {
      addEventLog("TOUCH", "Tools touch ignorato raw x=" + String(rawX) + " y=" + String(rawY));
    }
    return;
  }

  int approxX = map(rawX, 250, 3850, 0, 320);
  if (approxX < 0) approxX = 0;
  if (approxX > 319) approxX = 319;
  if (displayRotation == 3) approxX = 319 - approxX;
  int toolBtn = toolsButtonFromScreen(approxX, 186);
  if (toolBtn == 0) {
    setBacklight(false);
    addEventLog("LCD", "Display spento da TOOLS");
  } else if (toolBtn == 1) {
    startTftTouchCalibration();
  } else if (toolBtn == 2) {
    toggleTftRotationFromTools();
  } else if (toolBtn == 3) {
    if (toolsPendingAction != 3 || millis() - toolsPendingActionMs > 2500UL) {
      toolsPendingAction = 3;
      toolsPendingActionMs = millis();
      drawToolsConfirm("Tocca REBOOT ancora per confermare", TFT_ORANGE);
      addEventLog("SYS", "Reboot Tools in attesa conferma");
      return;
    }
    toolsPendingAction = -1;
    addEventLog("SYS", "Reboot da TOOLS");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(70, 104);
    tft.print("Riavvio...");
    delay(350);
    ESP.restart();
  }
}



void drawDashboard() {
  // V10.4.30 CYD GX HISTORY PLUS
  // Grafica GX leggera e stabile:
  // - nessuna reinit TFT nel loop
  // - refresh lento
  // - 9 pagine locali: Overview, Solare, Batteria, Batteria ESP, Storico, OTA, Sistema, Debug, Tools
  // - touch riattivato solo per cambio pagina, con polling lento e bus separato
  static unsigned long lastAutoPageMs = 0;

  tft.setRotation(displayRotation);

  // Cambio pagina automatico ogni 30 secondi solo se non hai toccato di recente.
  if ((millis() - lastUserActivityMs > 18000UL) && (millis() - lastAutoPageMs > 22000UL)) {
    page++;
    if (page >= TFT_PAGE_COUNT) page = 0;
    lastAutoPageMs = millis();
  }

  switch (page) {
    case 0: drawOverviewPage(); break;
    case 1: drawSolarPage(); break;
    case 2: drawBatteryPage(); break;
    case 3: drawEspBatteryPage(); break;
    case 4: drawHistoryTftPage(); break;
    case 5: drawOtaTftPage(); break;
    case 6: drawSystemPage(); break;
    case 7: drawDebugPage(); break;
    case 8: drawRecapPage(); break;
    case 9: drawToolsPage(); break;
    default:
      page = 0;
      drawOverviewPage();
      break;
  }
}


void handleTouch() {
  // V10.4.57: touch configurabile:
  // - default: solo pulsanti visibili < HOME >
  // - opzionale da Web UI: zone laterali stile vecchio
  if (!touchReady) return;
  if (millis() - lastTouchPollMs < 120UL) return;
  lastTouchPollMs = millis();

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, HIGH);
  delayMicroseconds(20);

  if (tftTouchCalMode && millis() - tftTouchCalStartedMs > 120000UL) {
    tftTouchCalMode = false;
    tftTouchCalStep = 0;
    addEventLog("TOUCH", "Cal TFT timeout");
    page = TFT_PAGE_COUNT - 1;
    drawDashboard();
  }

  bool down = ts.touched();
  if (down && !touchWasDown && (millis() - lastTouchActionMs > 450UL)) {
    TS_Point p = ts.getPoint();

    // XPT2046 grezzo: usiamo soglie larghe per evitare calibrazione complessa.
    // Se la direzione risultasse invertita, basta scambiare le due azioni.
    int rawX = p.x;
    int rawY = p.y;
    lastTouchRawX = rawX;
    lastTouchRawY = rawY;
    lastTouchRawMs = millis();

    // Se il display e' spento, il primo tocco lo riaccende e basta.
    if (!backlightOn) {
      setBacklight(true);
      addEventLog("LCD", "Display riacceso da touch");
      lastTouchActionMs = millis();
      lastUserActivityMs = millis();
      drawDashboard();
      touchWasDown = down;
      return;
    }

    // Calibrazione touch direttamente dal TFT: nessuna soglia manuale.
    if (tftTouchCalMode) {
      handleTouchCalibrationTap(rawX, rawY);
      lastTouchActionMs = millis();
      lastUserActivityMs = millis();
      touchWasDown = down;
      return;
    }

    // Pagina TOOLS: azioni rapide sicure.
    if (page == TFT_PAGE_COUNT - 1) {
      handleToolsTouch(rawX, rawY);
      lastTouchActionMs = millis();
      lastUserActivityMs = millis();
      touchWasDown = down;
      return;
    }

    int navBtn = navButtonFromRaw(rawX, rawY);
    if (navBtn < 0) {
      if (touchZonesEnabled()) {
        int zone = touchZoneFromRawX(rawX);
        navigateByZone(zone);
        addEventLog("TOUCH", "Navigazione zone laterali raw x=" + String(rawX) + " y=" + String(rawY));
        lastTouchActionMs = millis();
        lastUserActivityMs = millis();
        drawDashboard();
        lastDrawMs = millis();
        touchWasDown = down;
        return;
      }
      int sx = 160, sy = 120;
      if (mapTouchRawToScreen(rawX, rawY, &sx, &sy)) {
        addEventLog("TOUCH", "Tocco ignorato fuori pulsanti sx=" + String(sx) + " sy=" + String(sy));
      } else {
        addEventLog("TOUCH", "Tocco ignorato fuori footer raw x=" + String(rawX) + " y=" + String(rawY));
      }
      lastTouchActionMs = millis();
      lastUserActivityMs = millis();
      touchWasDown = down;
      return;
    }

    if (navBtn == 0) {
      page--;
      if (page < 0) page = TFT_PAGE_COUNT - 1;
      addEventLog("TOUCH", "Pagina precedente bottone x=" + String(rawX));
    } else if (navBtn == 2) {
      page++;
      if (page >= TFT_PAGE_COUNT) page = 0;
      addEventLog("TOUCH", "Pagina successiva bottone x=" + String(rawX));
    } else {
      page = 0;
      addEventLog("TOUCH", "HOME bottone x=" + String(rawX));
    }

    lastTouchActionMs = millis();
    lastUserActivityMs = millis();
    drawDashboard();
    lastDrawMs = millis();  }

  touchWasDown = down;
}




void resetSlot(HistorySlot &s) {
  s.wh = 0;
  s.maxW = 0;
  s.maxPanelV = 0;
  s.battMax = 0;
  s.battMin = 999;
  s.battSum = 0;
  s.panelSum = 0;
  s.loadWh = 0;
  s.errors = 0;
  s.samples = 0;
}

void resetChargeSlot(ChargeStateSlot &s) {
  s.offSec = 0;
  s.bulkSec = 0;
  s.absorptionSec = 0;
  s.floatSec = 0;
  s.storageSec = 0;
  s.otherSec = 0;
  s.samples = 0;
}

void addChargeSecondsToSlot(ChargeStateSlot &s, uint32_t seconds) {
  if (seconds == 0 || seconds > 3600UL) return;
  String cs = chargeState;
  cs.toLowerCase();
  if (!victronOnline()) {
    s.otherSec += seconds;
  } else if (cs.indexOf("bulk") >= 0 || cs.indexOf("prima") >= 0) {
    s.bulkSec += seconds;
  } else if (cs.indexOf("absorption") >= 0 || cs.indexOf("assorb") >= 0) {
    s.absorptionSec += seconds;
  } else if (cs.indexOf("float") >= 0 || cs.indexOf("manten") >= 0) {
    s.floatSec += seconds;
  } else if (cs.indexOf("storage") >= 0) {
    s.storageSec += seconds;
  } else if (cs.indexOf("spento") >= 0 || cs.indexOf("off") >= 0) {
    s.offSec += seconds;
  } else {
    s.otherSec += seconds;
  }
  s.samples++;
}

String chargeJsonFields(const ChargeStateSlot &s) {
  uint32_t total = s.offSec + s.bulkSec + s.absorptionSec + s.floatSec + s.storageSec + s.otherSec;
  String j = "";
  j += ",\"off_sec\":" + String(s.offSec);
  j += ",\"bulk_sec\":" + String(s.bulkSec);
  j += ",\"absorption_sec\":" + String(s.absorptionSec);
  j += ",\"float_sec\":" + String(s.floatSec);
  j += ",\"storage_sec\":" + String(s.storageSec);
  j += ",\"other_sec\":" + String(s.otherSec);
  j += ",\"charge_total_sec\":" + String(total);
  j += ",\"charge_samples\":" + String(s.samples);
  return j;
}

String jsNum(float v, int dec) {
  if (isnan(v)) return "0";
  return String(v, dec);
}

// V10.4.80 - Protezione storico hard-limit.
// Il pannello utente e' 120 W: valori giornalieri tipo 24 kWh o Pmax da MW sono corruzione.
// Manteniamo limiti separati per ora/giorno/mese, cosi' il mensile non viene tagliato come il giornaliero.
const float HISTORY_MAX_REASONABLE_W = 300.0f;        // margine 2.5x su pannello 120 W
const float HISTORY_MAX_HOURLY_WH = 350.0f;           // 120 W * 1h + margine forte
const float HISTORY_MAX_DAILY_WH = 2500.0f;           // 2.5 kWh/giorno: sopra e' corruzione per questo impianto
const float HISTORY_MAX_MONTHLY_WH = 80000.0f;        // 80 kWh/mese: limite alto per 120 W
const float HISTORY_MAX_REASONABLE_PV = 80.0f;        // Victron 100/20: sopra e' fuori contesto pratico
const float HISTORY_MAX_REASONABLE_BAT = 80.0f;       // 12/24/48 V con margine

bool historyValueOk(float v, float minV, float maxV) {
  if (isnan(v) || isinf(v)) return false;
  if (v < minV || v > maxV) return false;
  return true;
}

void sanitizeHistorySlotWithLimit(HistorySlot &s, float whLimit) {
  bool hardReset = false;

  if (!historyValueOk(s.wh, 0.0f, whLimit)) hardReset = true;
  if (!historyValueOk(s.maxW, 0.0f, HISTORY_MAX_REASONABLE_W)) hardReset = true;
  if (!historyValueOk(s.maxPanelV, 0.0f, HISTORY_MAX_REASONABLE_PV)) hardReset = true;
  if (!historyValueOk(s.battMax, 0.0f, HISTORY_MAX_REASONABLE_BAT)) hardReset = true;
  if (s.battMin != 999 && !historyValueOk(s.battMin, 0.0f, HISTORY_MAX_REASONABLE_BAT)) hardReset = true;
  if (!historyValueOk(s.battSum, 0.0f, HISTORY_MAX_REASONABLE_BAT * 65535.0f)) hardReset = true;
  if (!historyValueOk(s.panelSum, 0.0f, HISTORY_MAX_REASONABLE_PV * 65535.0f)) hardReset = true;
  if (!historyValueOk(s.loadWh, 0.0f, whLimit)) hardReset = true;

  if (hardReset) {
    resetSlot(s);
  }
}

void sanitizeHistorySlot(HistorySlot &s) {
  sanitizeHistorySlotWithLimit(s, HISTORY_MAX_DAILY_WH);
}

void sanitizeAllHistorySlots() {
  for (int i = 0; i < 24; i++) sanitizeHistorySlotWithLimit(hourly[i], HISTORY_MAX_HOURLY_WH);
  for (int i = 0; i < 31; i++) sanitizeHistorySlotWithLimit(daily[i], HISTORY_MAX_DAILY_WH);
  for (int i = 0; i < 12; i++) sanitizeHistorySlotWithLimit(monthly[i], HISTORY_MAX_MONTHLY_WH);
}

void initHistoryIfNeeded() {
  int h = (millis() / 3600000UL) % 24;
  int d = (millis() / 86400000UL) % 31;
  int m = (millis() / 2592000000UL) % 12;

  if (currentHourIndex < 0) {
    currentHourIndex = h;
    currentDayIndex = d;
    currentMonthIndex = m;
  }

  if (h != currentHourIndex) {
    currentHourIndex = h;
    resetSlot(hourly[currentHourIndex]);
    resetChargeSlot(chHourly[currentHourIndex]);
  }

  if (d != currentDayIndex) {
    currentDayIndex = d;
    resetSlot(daily[currentDayIndex]);
    resetChargeSlot(chDaily[currentDayIndex]);
  }

  if (m != currentMonthIndex) {
    currentMonthIndex = m;
    resetSlot(monthly[currentMonthIndex]);
    resetChargeSlot(chMonthly[currentMonthIndex]);
  }
}

void addSampleToSlot(HistorySlot &s, float wh, float pW, float bV, float pV) {
  // Sanifica i campioni prima di inserirli nello storico.
  // Serve a evitare popup/grafici impossibili tipo P max da MW o Wh enormi.
  bool whOk = historyValueOk(wh, 0.0f, 25.0f); // 25 Wh ogni 10s e' gia' enorme; protegge da dt/campioni corrotti
  bool pOk = historyValueOk(pW, 0.0f, HISTORY_MAX_REASONABLE_W);
  bool pvOk = historyValueOk(pV, 0.0f, HISTORY_MAX_REASONABLE_PV);
  bool bOk = historyValueOk(bV, 0.0f, HISTORY_MAX_REASONABLE_BAT);

  if (whOk) s.wh += wh;
  if (pOk && pW > s.maxW) s.maxW = pW;
  if (pvOk && pV > s.maxPanelV) s.maxPanelV = pV;
  if (bOk) {
    if (bV > s.battMax) s.battMax = bV;
    if (bV < s.battMin) s.battMin = bV;
    s.battSum += bV;
  }
  if (pvOk) s.panelSum += pV;
  if (errorState != "0" && errorState != "N/D") s.errors++;
  if (bOk || pvOk || pOk) s.samples++;
  sanitizeHistorySlotWithLimit(s, HISTORY_MAX_MONTHLY_WH);
}

void updateHistory() {
  initHistoryIfNeeded();

  unsigned long now = millis();
  if (lastHistoryMs == 0) {
    lastHistoryMs = now;
    lastPanelWForEnergy = panelW;
    return;
  }

  if (now - lastHistoryMs < 10000UL) return;

  uint32_t dtMs = now - lastHistoryMs;
  float dtHours = dtMs / 3600000.0;
  uint32_t dtSec = dtMs / 1000UL;
  float p = historyValueOk(panelW, 0.0f, HISTORY_MAX_REASONABLE_W) ? panelW : 0;
  float safeBattV = historyValueOk(battV, 0.0f, HISTORY_MAX_REASONABLE_BAT) ? battV : NAN;
  float safePanelV = historyValueOk(panelV, 0.0f, HISTORY_MAX_REASONABLE_PV) ? panelV : NAN;
  float wh = p * dtHours;

  addSampleToSlot(hourly[currentHourIndex], wh, p, safeBattV, safePanelV);
  addSampleToSlot(daily[currentDayIndex], wh, p, safeBattV, safePanelV);
  addSampleToSlot(monthly[currentMonthIndex], wh, p, safeBattV, safePanelV);
  addChargeSecondsToSlot(chHourly[currentHourIndex], dtSec);
  addChargeSecondsToSlot(chDaily[currentDayIndex], dtSec);
  addChargeSecondsToSlot(chMonthly[currentMonthIndex], dtSec);

  lastHistoryMs = now;
  lastPanelWForEnergy = panelW;

  if (millis() - lastHistorySaveMs > 60000UL) {
    lastHistorySaveMs = millis();
    saveHistoryToFs();
  }
  if (millis() - lastChargeHistorySaveMs > 60000UL) {
    lastChargeHistorySaveMs = millis();
    saveChargeHistoryToFs();
  }
}

String historyJsonArray(HistorySlot *arr, int count, int currentIndex, const char* labelPrefix) {
  String out = "[";
  for (int i = 0; i < count; i++) {
    int idx = (currentIndex + 1 + i) % count;
    HistorySlot &s = arr[idx];
    sanitizeHistorySlot(s);

    float battAvg = s.samples ? s.battSum / s.samples : 0;
    float panelAvg = s.samples ? s.panelSum / s.samples : 0;
    float battMinSafe = (s.battMin == 999) ? 0 : s.battMin;

    if (i) out += ",";
    out += "{";
    out += "\"label\":\"";
    if (String(labelPrefix) == "G") {
      if (i == count - 1) out += "Oggi";
      else if (i == count - 2) out += "Ieri";
      else {
        out += String(count - 1 - i);
        out += " giorni fa";
      }
    } else if (String(labelPrefix) == "H") {
      out += "H";
      out += String(i - count + 1);
    } else {
      out += "M";
      out += String(i - count + 1);
    }
    out += "\",";
    out += "\"wh\":" + String(s.wh, 2) + ",";
    out += "\"kwh\":" + String(s.wh / 1000.0, 3) + ",";
    out += "\"maxw\":" + String(s.maxW, 1) + ",";
    out += "\"maxpv\":" + String(s.maxPanelV, 2) + ",";
    out += "\"battmax\":" + String(s.battMax, 2) + ",";
    out += "\"battmin\":" + String(battMinSafe, 2) + ",";
    out += "\"battavg\":" + String(battAvg, 2) + ",";
    out += "\"panelavg\":" + String(panelAvg, 2) + ",";
    out += "\"loadwh\":" + String(s.loadWh, 2) + ",";
    out += "\"errors\":" + String(s.errors) + ",";
    out += "\"samples\":" + String(s.samples);
    if (String(labelPrefix) == "H") out += chargeJsonFields(chHourly[idx]);
    else if (String(labelPrefix) == "G") out += chargeJsonFields(chDaily[idx]);
    else out += chargeJsonFields(chMonthly[idx]);
    out += "}";
  }
  out += "]";
  return out;
}



void initLittleFs() {
  if (littleFsReady) return;
  littleFsReady = LittleFS.begin(true);
  if (!littleFsReady) {
    Serial.println("LittleFS non disponibile: storico persistente disattivato");
  }
}

void saveHistoryToFs() {
  if (!littleFsReady) return;
  fs::File f = LittleFS.open("/history.bin", "w");
  if (!f) return;
  uint32_t magic = 0x85312026;
  f.write((uint8_t*)&magic, sizeof(magic));
  f.write((uint8_t*)&currentHourIndex, sizeof(currentHourIndex));
  f.write((uint8_t*)&currentDayIndex, sizeof(currentDayIndex));
  f.write((uint8_t*)&currentMonthIndex, sizeof(currentMonthIndex));
  f.write((uint8_t*)hourly, sizeof(hourly));
  f.write((uint8_t*)daily, sizeof(daily));
  f.write((uint8_t*)monthly, sizeof(monthly));
  f.close();
}

bool loadHistoryFromFs() {
  if (!littleFsReady || !LittleFS.exists("/history.bin")) return false;
  fs::File f = LittleFS.open("/history.bin", "r");
  if (!f) return false;
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x85312026) { f.close(); return false; }
  f.read((uint8_t*)&currentHourIndex, sizeof(currentHourIndex));
  f.read((uint8_t*)&currentDayIndex, sizeof(currentDayIndex));
  f.read((uint8_t*)&currentMonthIndex, sizeof(currentMonthIndex));
  f.read((uint8_t*)hourly, sizeof(hourly));
  f.read((uint8_t*)daily, sizeof(daily));
  f.read((uint8_t*)monthly, sizeof(monthly));
  f.close();
  sanitizeAllHistorySlots();
  return true;
}

void saveChargeHistoryToFs() {
  if (!littleFsReady) return;
  fs::File f = LittleFS.open("/charge_history.bin", "w");
  if (!f) return;
  uint32_t magic = 0x91352027;
  f.write((uint8_t*)&magic, sizeof(magic));
  f.write((uint8_t*)chHourly, sizeof(chHourly));
  f.write((uint8_t*)chDaily, sizeof(chDaily));
  f.write((uint8_t*)chMonthly, sizeof(chMonthly));
  f.close();
}

bool loadChargeHistoryFromFs() {
  if (!littleFsReady || !LittleFS.exists("/charge_history.bin")) return false;
  fs::File f = LittleFS.open("/charge_history.bin", "r");
  if (!f) return false;
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x91352027) { f.close(); return false; }
  f.read((uint8_t*)chHourly, sizeof(chHourly));
  f.read((uint8_t*)chDaily, sizeof(chDaily));
  f.read((uint8_t*)chMonthly, sizeof(chMonthly));
  f.close();
  return true;
}

String fsInfoJson() {
  String j = "{";
  if (!littleFsReady) {
    j += "\"ready\":false";
  } else {
    j += "\"ready\":true,";
    j += "\"total\":" + String((unsigned long)LittleFS.totalBytes()) + ",";
    j += "\"used\":" + String((unsigned long)LittleFS.usedBytes()) + ",";
    j += "\"history_file\":" + String(LittleFS.exists("/history.bin") ? "true" : "false") + ",";
    j += "\"charge_history_file\":" + String(LittleFS.exists("/charge_history.bin") ? "true" : "false");
  }
  j += "}";
  return j;
}

void rollbackInitAndValidate() {
  // V9: non conferma subito il firmware. La conferma vera viene fatta da safeBootLoop()
  // dopo WiFi + webserver + tempo minimo di stabilita.
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
      rollbackStatus = "Firmware nuovo in verifica: rollback attivo finche il boot non e' stabile";
    } else {
      rollbackStatus = "Partizione attiva valida";
    }
  } else {
    rollbackStatus = "Rollback state non disponibile";
  }
  prefs.begin("victron", false);
  prefs.putString("rollback_status", rollbackStatus);
  prefs.end();
}

void safeBootStart() {
  safeBootStartedMs = millis();

  prefs.begin("victron", false);
  int unconfirmed = prefs.getInt("boot_unconf", 0) + 1;
  if (unconfirmed < 1) unconfirmed = 1;
  prefs.putInt("boot_unconf", unconfirmed);
  prefs.putString("safe_boot_status", "Boot in verifica: attendo 90 secondi prima di confermare firmware stabile");
  prefs.putString("safe_boot_started", timeIsValid() ? timeText() : buildText());
  prefs.putString("last_boot_fw", FW_VERSION);
  prefs.putString("last_boot_build", buildText());

  if (unconfirmed >= SAFE_BOOT_MAX_UNCONFIRMED) {
    prefs.putBool("recovery_recommended", true);
    prefs.putString("recovery_reason", "Tre boot consecutivi non confermati: recovery automatica consigliata");
  }

  bool forceRecovery = prefs.getBool("force_recovery", false);
  bool recommendedRecovery = prefs.getBool("recovery_recommended", false);
  recoveryMode = forceRecovery || recommendedRecovery;

  prefs.putBool("recovery_active", recoveryMode);
  prefs.end();

  if (recoveryMode) {
    addEventLog("RECOVERY", "Recovery mode attiva al boot. Boot non confermati: " + String(unconfirmed));
  } else {
    addEventLog("BOOT", "Safe boot verifica avviata. Boot non confermati: " + String(unconfirmed));
  }
}

void safeBootLoop() {
  if (safeBootConfirmed) return;
  if (millis() - safeBootStartedMs < SAFE_BOOT_CONFIRM_MS) return;

  // V10.4.66: per considerare stabile il firmware servono almeno 90 secondi
  // e WiFi operativo. Il webserver è già stato avviato in setup().
  if (WiFi.status() != WL_CONNECTED) {
    prefs.begin("victron", false);
    prefs.putString("safe_boot_status", "In attesa conferma: 90s superati ma WiFi non connesso");
    prefs.end();
    return;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  bool rollbackMarked = false;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    rollbackMarked = (e == ESP_OK);
  } else {
    rollbackMarked = true;
  }

  safeBootConfirmed = true;
  recoveryMode = false;
  rollbackStatus = rollbackMarked ? "Firmware confermato stabile dopo 90 secondi: rollback annullato" : "Errore conferma rollback";

  prefs.begin("victron", false);
  prefs.putInt("boot_unconf", 0);
  prefs.putBool("recovery_recommended", false);
  prefs.putBool("force_recovery", false);
  prefs.putBool("recovery_active", false);
  prefs.putString("recovery_reason", "");
  prefs.putString("stable_fw", FW_VERSION);
  prefs.putString("stable_build", buildText());
  prefs.putString("stable_time", timeIsValid() ? timeText() : buildText());
  prefs.putString("safe_boot_status", rollbackStatus);
  prefs.putString("rollback_status", rollbackStatus);
  prefs.end();

  addEventLog("BOOT", rollbackStatus);
}

String otaPartitionJson() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  String j = "{";
  j += "\"running\":\"" + String(running ? running->label : "N/D") + "\",";
  j += "\"boot\":\"" + String(boot ? boot->label : "N/D") + "\",";
  j += "\"next\":\"" + String(next ? next->label : "N/D") + "\",";
  j += "\"free_sketch\":" + String((unsigned long)ESP.getFreeSketchSpace());
  j += "}";
  return j;
}

String httpGetString(const String& url, int timeoutMs = 9000) {
  if (url.length() < 8 || WiFi.status() != WL_CONNECTED) return "";
  githubOtaPercent = 12;
  githubOtaMessage = "Fase 3/9: connessione a GitHub...";
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = url.startsWith("https://");
  if (https) {
    secure.setInsecure();
    http.begin(secure, url);
  } else {
    http.begin(plain, url);
  }
  http.setTimeout(timeoutMs);
  int code = http.GET();
  githubOtaMessage = "Fase 4/9: download firmware...";
  String body = "";
  if (code == 200) body = http.getString();
  http.end();
  body.trim();
  return body;
}

String getGithubVersionUrl() {
  prefs.begin("victron", true);
  String s = prefs.getString("gh_ver_url", "");
  prefs.end();
  s.trim();
  if (s.length() == 0) s = pubCfg.otaVersionUrl;
  s.trim();
  if (s.length() == 0) s = String(GITHUB_VERSION_URL);
  return s;
}

String getGithubBinUrl() {
  prefs.begin("victron", true);
  String s = prefs.getString("gh_bin_url", "");
  prefs.end();
  s.trim();
  if (s.length() == 0) s = pubCfg.otaBinUrl;
  s.trim();
  if (s.length() == 0) s = String(GITHUB_BIN_URL);
  return s;
}

String getGithubShaUrl() {
  prefs.begin("victron", true);
  String s = prefs.getString("gh_sha_url", "");
  prefs.end();
  s.trim();
  if (s.length() == 0) s = pubCfg.otaSha256Url;
  s.trim();
  if (s.length() == 0) s = String(GITHUB_SHA_URL);
  return s;
}

String getGithubChangelogUrl() {
  prefs.begin("victron", true);
  String s = prefs.getString("gh_log_url", "");
  prefs.end();
  s.trim();
  if (s.length() == 0) s = String(GITHUB_LOG_URL);
  return s;
}


bool shouldMigrateOtaUrlToPublicRepo(const String& url) {
  String u = url;
  u.trim();
  if (u.length() == 0) return true;
  if (u.indexOf("alessioquartarone-ui/victron-vedirect-esp32-monitor-ota") >= 0) return false;
  if (u.indexOf("alessioquartarone-ui/victron-esp32-monitor-ota") >= 0) return true;
  if (u.indexOf("alessioquartarone-ui/victron-esp32-monitor/") >= 0) return true;
  if (u.indexOf("/victron-esp32-monitor/main/firmware/") >= 0) return true;
  return false;
}

void ensurePublicOtaRepoDefaults() {
  prefs.begin("victron", false);
  bool done = prefs.getBool("ota_pub_repo_65", false);
  if (!done) {
    String v = prefs.getString("gh_ver_url", "");
    String b = prefs.getString("gh_bin_url", "");
    String sh = prefs.getString("gh_sha_url", "");
    String lg = prefs.getString("gh_log_url", "");
    if (shouldMigrateOtaUrlToPublicRepo(v)) prefs.putString("gh_ver_url", GITHUB_VERSION_URL);
    if (shouldMigrateOtaUrlToPublicRepo(b)) prefs.putString("gh_bin_url", GITHUB_BIN_URL);
    if (shouldMigrateOtaUrlToPublicRepo(sh)) prefs.putString("gh_sha_url", GITHUB_SHA_URL);
    if (shouldMigrateOtaUrlToPublicRepo(lg) && String(GITHUB_LOG_URL).length()) prefs.putString("gh_log_url", GITHUB_LOG_URL);
    prefs.putBool("ota_pub_repo_65", true);
  }
  prefs.end();
}

String cleanSha256(String v) {
  v.trim();
  int sp = v.indexOf(' ');
  if (sp > 0) v = v.substring(0, sp);
  v.trim();
  v.toLowerCase();
  return v;
}

String sha256HexOfStream(Stream& stream, int len, size_t* writtenOut, bool writeToUpdate) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  uint8_t buff[1024];
  int remaining = len;
  size_t written = 0;
  unsigned long lastData = millis();
  while (remaining > 0) {
    size_t avail = stream.available();
    if (avail == 0) {
      if (millis() - lastData > 20000UL) break;
      delay(1);
      continue;
    }
    size_t toRead = avail;
    if (toRead > sizeof(buff)) toRead = sizeof(buff);
    if (toRead > (size_t)remaining) toRead = remaining;
    int r = stream.readBytes(buff, toRead);
    if (r <= 0) break;
    lastData = millis();
    mbedtls_sha256_update(&ctx, buff, r);
    if (writeToUpdate) {
      size_t w = Update.write(buff, r);
      if (w != (size_t)r) break;
    }
    written += r;
    if (writeToUpdate && len > 0) {
      githubOtaWritten = written;
      githubOtaTotal = len;
      githubOtaPercent = (int)((written * 100UL) / (uint32_t)len);
      if (githubOtaPercent > 100) githubOtaPercent = 100;
    }
    remaining -= r;
    yield();
  }
  uint8_t out[32];
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
  if (writtenOut) *writtenOut = written;
  char hex[65];
  for (int i=0; i<32; i++) sprintf(hex + i*2, "%02x", out[i]);
  hex[64] = 0;
  return String(hex);
}

void saveGithubUrls(const String& versionUrl, const String& binUrl) {
  prefs.begin("victron", false);
  prefs.putString("gh_ver_url", versionUrl);
  prefs.putString("gh_bin_url", binUrl);
  prefs.putString("gh_sha_url", server.hasArg("sha") ? server.arg("sha") : "");
  prefs.putString("gh_log_url", server.hasArg("log") ? server.arg("log") : "");
  prefs.putBool("gh_weekly", server.hasArg("weekly") && server.arg("weekly") == "1");
  prefs.end();
}

void checkGithubUpdate(bool forced = false) {
  if (!forced && millis() - lastGithubCheckMs < 3600000UL) return;
  lastGithubCheckMs = millis();
  githubUpdateAvailable = false;
  String versionUrl = getGithubVersionUrl();
  if (versionUrl.length() == 0) {
    githubLastStatus = "Configura l'URL versione dalla pagina GitHub Update";
    githubRemoteVersion = "N/D";
    return;
  }
  String remote = httpGetString(versionUrl);
  if (remote.length() == 0) {
    githubLastStatus = "Controllo GitHub fallito o URL non raggiungibile";
    prefs.begin("victron", false);
    prefs.putString("gh_last_check_time", timeIsValid() ? timeText() : buildText());
    prefs.putString("gh_last_check_status", githubLastStatus);
    prefs.putString("gh_last_remote", githubRemoteVersion);
    prefs.end();
    return;
  }
  githubRemoteVersion = remote;
  githubUpdateAvailable = (remote != String(FW_VERSION));
  githubLastStatus = githubUpdateAvailable ? "Nuovo firmware disponibile" : "Firmware gia aggiornato";
  prefs.begin("victron", false);
  prefs.putString("gh_last_check_time", timeIsValid() ? timeText() : buildText());
  prefs.putString("gh_last_check_status", githubLastStatus);
  prefs.putString("gh_last_remote", githubRemoteVersion);
  prefs.end();
}

bool performGithubBinUpdate(String& result) {
  String binUrl = getGithubBinUrl();
  if (binUrl.length() == 0) { result = "URL firmware .bin non configurato"; return false; }
  if (WiFi.status() != WL_CONNECTED) { result = "WiFi non connesso"; return false; }

  githubOtaPercent = 1;
  githubOtaMessage = "Fase 1/9: controllo SD e backup versione attuale...";
  autoBackupBeforeOta("GitHub OTA");

  githubOtaPercent = 8;
  githubOtaMessage = "Fase 2/9: lettura SHA256 remoto...";
  String shaUrl = getGithubShaUrl();
  String expectedSha = shaUrl.length() ? cleanSha256(httpGetString(shaUrl, 9000)) : "";
  bool shaRequired = expectedSha.length() == 64;

  githubOtaPercent = 12;
  githubOtaMessage = "Fase 3/9: connessione a GitHub...";
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = binUrl.startsWith("https://");
  if (https) { secure.setInsecure(); http.begin(secure, binUrl); }
  else { http.begin(plain, binUrl); }
  http.setTimeout(30000);
  int code = http.GET();
  githubOtaMessage = "Fase 4/9: download firmware...";
  if (code != HTTP_CODE_OK) { result = "Download firmware fallito, HTTP " + String(code); http.end(); return false; }

  int len = http.getSize();
  uint32_t freeSpace = ESP.getFreeSketchSpace();
  if (len <= 0) { result = "Dimensione firmware non valida"; http.end(); return false; }
  if ((uint32_t)len > freeSpace) {
    result = "Firmware troppo grande. Bin " + fmtBytes(len) + " spazio OTA " + fmtBytes(freeSpace);
    http.end(); return false;
  }
  if (shaUrl.length() && !shaRequired) {
    result = "SHA256 remoto non valido o non leggibile. Update bloccato per sicurezza.";
    http.end(); return false;
  }
  githubOtaMessage = "Fase 5/9: preparazione partizione OTA...";
  if (!Update.begin(len)) { result = "Update.begin fallito: controlla partizione OTA"; http.end(); return false; }

  githubOtaMessage = "Fase 6/9: scrittura firmware e calcolo SHA256...";
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  String actualSha = sha256HexOfStream(*stream, len, &written, true);
  bool writeOk = (written == (size_t)len);
  bool shaOk = (!shaRequired || actualSha == expectedSha);

  githubOtaMessage = "Fase 7/9: verifica integrita' firmware...";
  if (!writeOk || !shaOk) {
    Update.abort();
    result = "Update GitHub bloccato. Scritti " + String((unsigned long)written) + "/" + String(len);
    if (shaRequired) result += " SHA atteso " + expectedSha + " calcolato " + actualSha;
    http.end(); return false;
  }

  githubOtaMessage = "Fase 8/9: finalizzazione OTA...";
  bool ok = Update.end(true);
  if (!ok || Update.hasError()) {
    result = "Update GitHub fallito in Update.end(true). Errore " + String(Update.getError());
    http.end(); return false;
  }
  http.end();

  githubOtaMessage = "Fase 9/9: salvataggio stato e riavvio...";
  result = "GitHub OTA OK. Firmware verificato" + String(shaRequired ? " con SHA256" : " senza SHA256") + ". Riavvio in corso.";
  saveOtaResult(true, "Aggiornamento avviato da GitHub direct OTA. " + result);
  prefs.begin("victron", false);
  prefs.putString("last_sha256", actualSha);
  prefs.putString("last_remote_version", githubRemoteVersion);
  prefs.putBool("clone_after_ota", true);
  prefs.putString("clone_after_src", "GitHub OTA");
  prefs.end();
  // V10.4.25: niente secondo reboot automatico post-OTA.
  // Causava: progress 90% -> nero/lampeggi -> ripartenza da 0 al primo avvio.
  otaRestartPending = true;
  otaRestartAtMs = millis() + 10000UL;
  return true;
}

String getUpdateMessage() {
  prefs.begin("victron", false);
  String msg = prefs.getString("update_msg", "");
  prefs.end();
  return msg;
}

void setUpdateMessage(const String& msg) {
  prefs.begin("victron", false);
  prefs.putString("update_msg", msg);
  prefs.end();
}

void clearUpdateMessage() {
  prefs.begin("victron", false);
  prefs.remove("update_msg");
  prefs.end();
}

String prefGet(const char* key, const String& fallback = "N/D") {
  prefs.begin("victron", true);
  String v = prefs.getString(key, fallback);
  prefs.end();
  return v;
}

int prefsGetIntSafe(const char* key, int fallback) {
  prefs.begin("victron", true);
  int v = prefs.getInt(key, fallback);
  prefs.end();
  return v;
}

void prefsPutIntSafe(const char* key, int value) {
  prefs.begin("victron", false);
  prefs.putInt(key, value);
  prefs.end();
}

String touchRotKey(const char* base) {
  return String(base) + "_r" + String(displayRotation);
}

int prefsGetTouchCal(const char* base, int fallback) {
  // Prima cerca la calibrazione specifica per la rotazione corrente.
  // Se non esiste, usa il valore storico solo per rotazione normale.
  String k = touchRotKey(base);
  int v = prefsGetIntSafe(k.c_str(), -32768);
  if (v != -32768) return v;

  if (displayRotation == 1) return prefsGetIntSafe(base, fallback);

  // Rotazione 180°: i vecchi punti touch della rotazione normale non sono affidabili.
  // Per X sinistra/destra possiamo invertire i vecchi riferimenti come fallback temporaneo.
  if (String(base) == "touch_x_left") {
    int legacyRight = prefsGetIntSafe("touch_x_right", -32768);
    if (legacyRight != -32768) return legacyRight;
  }
  if (String(base) == "touch_x_right") {
    int legacyLeft = prefsGetIntSafe("touch_x_left", -32768);
    if (legacyLeft != -32768) return legacyLeft;
  }
  return fallback;
}

void prefsPutTouchCal(const char* base, int value) {
  String k = touchRotKey(base);
  prefsPutIntSafe(k.c_str(), value);
  // Manteniamo anche i vecchi nomi in rotazione normale per compatibilita' con backup/import.
  if (displayRotation == 1) prefsPutIntSafe(base, value);
  prefsPutIntSafe("touch_cal_rotation", displayRotation);
}

String jsonEsc(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", " ");
  s.replace("\r", " ");
  return s;
}


String prettyJson(const String& raw) {
  String out;
  out.reserve(raw.length() + 64);
  int indent = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw[i];
    if (inString) {
      out += c;
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') { inString = true; out += c; continue; }
    if (c == '{' || c == '[') {
      out += c;
      out += '\n';
      indent += 2;
      for (int k = 0; k < indent; k++) out += ' ';
    } else if (c == '}' || c == ']') {
      out += '\n';
      indent -= 2;
      if (indent < 0) indent = 0;
      for (int k = 0; k < indent; k++) out += ' ';
      out += c;
    } else if (c == ',') {
      out += c;
      out += '\n';
      for (int k = 0; k < indent; k++) out += ' ';
    } else if (c == ':') {
      out += ": ";
    } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      // ignora spazi fuori dalle stringhe: li rigeneriamo noi ordinati
    } else {
      out += c;
    }
  }
  return out;
}

void sendJsonPretty(const String& raw) {
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json; charset=utf-8", prettyJson(raw));
}

String wifiBadgeClass(int rssi) {
  if (WiFi.status() != WL_CONNECTED) return "bad";
  if (rssi > -65) return "ok";
  if (rssi > -75) return "warn";
  if (rssi > -85) return "weak";
  return "bad";
}

String veBadgeClass() {
  if (victronOnline()) return "ok";
  if (lastVictronMs > 0 && millis() - lastVictronMs < 120000UL) return "warn";
  return "bad";
}

String espBatteryBadgeClass(float pct) {
  if (isnan(pct) || pct <= 0) return "bad";
  if (pct >= 90) return "ok";
  if (pct >= 50) return "warn";
  if (pct >= 20) return "weak";
  return "bad";
}

void saveOtaNotify(const String& eventType, const String& title, const String& message, const String& level, const String& version) {
  prefs.begin("victron", false);
  uint32_t id = prefs.getUInt("ota_note_id", 0) + 1;
  prefs.putUInt("ota_note_id", id);
  prefs.putBool("ota_note_seen", false);
  prefs.putString("ota_note_type", eventType);
  prefs.putString("ota_note_title", title);
  prefs.putString("ota_note_msg", message);
  prefs.putString("ota_note_level", level);
  prefs.putString("ota_note_ver", version);
  prefs.putString("ota_note_time", timeIsValid() ? timeText() : buildText());
  prefs.end();
}

String otaNotifyJson() {
  prefs.begin("victron", true);
  String j = "{";
  j += "\"id\":" + String(prefs.getUInt("ota_note_id", 0)) + ",";
  j += "\"seen\":" + String(prefs.getBool("ota_note_seen", true) ? "true" : "false") + ",";
  j += "\"type\":\"" + jsonEsc(prefs.getString("ota_note_type", "none")) + "\",";
  j += "\"title\":\"" + jsonEsc(prefs.getString("ota_note_title", "N/D")) + "\",";
  j += "\"message\":\"" + jsonEsc(prefs.getString("ota_note_msg", "")) + "\",";
  j += "\"level\":\"" + jsonEsc(prefs.getString("ota_note_level", "info")) + "\",";
  j += "\"version\":\"" + jsonEsc(prefs.getString("ota_note_ver", "N/D")) + "\",";
  j += "\"time\":\"" + jsonEsc(prefs.getString("ota_note_time", "N/D")) + "\",";
  j += "\"installed\":\"" + String(FW_VERSION) + "\",";
  j += "\"remote\":\"" + jsonEsc(githubRemoteVersion) + "\"";
  j += "}";
  prefs.end();
  return j;
}

void markOtaNotifySeen() {
  prefs.begin("victron", false);
  prefs.putBool("ota_note_seen", true);
  prefs.end();
}

void handleOtaNotifyJson() {
  if (!requireAuth()) return;
  sendJsonPretty(otaNotifyJson());
}

void handleOtaNotifyClear() {
  if (!requireAuth()) return;
  markOtaNotifySeen();
  sendJsonPretty("{\"ok\":true}");
}

void saveOtaResult(bool ok, const String& detail) {
  saveOtaNotify("ota", ok ? "Aggiornamento completato" : "Errore aggiornamento", detail, ok ? "ok" : "bad", FW_VERSION);
  prefs.begin("victron", false);
  prefs.putString("ota_status", ok ? "OK" : "ERRORE");
  prefs.putString("ota_detail", detail);
  prefs.putString("ota_fw_before", FW_VERSION);
  prefs.putString("ota_build_before", buildText());

  if (ok) {
    // Non salvo una data falsa durante l upload: dopo il flash l ESP32
    // si riavvia e l'orologio NTP puo' non essere ancora pronto.
    // Salvo solo il pending; il timestamp reale verra' scritto appena
    // WiFi+NTP saranno validi con anno >= 2024.
    prefs.putString("ota_time", "In attesa riavvio + NTP");
    prefs.putString("ota_pending_from", buildText());
    prefs.putBool("ota_pending", true);
    prefs.putBool("ota_needs_ntp_fix", true);
  } else {
    prefs.putString("ota_time", timeIsValid() ? timeText() : buildText());
    prefs.putBool("ota_pending", false);
    prefs.putBool("ota_needs_ntp_fix", false);
  }
  prefs.end();
}

void saveCurrentFirmwareInfo() {
  prefs.begin("victron", false);
  prefs.putString("current_fw", FW_VERSION);
  prefs.putString("current_name", FW_NAME);
  prefs.putString("current_build", buildText());
  prefs.end();
}

void saveFirmwareInstallIfChanged() {
  prefs.begin("victron", false);
  String prevFw = prefs.getString("installed_fw", "");
  String prevBuild = prefs.getString("installed_build", "");
  String nowFw = String(FW_VERSION);
  String nowBuild = buildText();

  String oldInstalledTime = prefs.getString("installed_time", "");
  bool otaPending = prefs.getBool("ota_pending", false);
  bool firmwareChanged = (prevFw != nowFw || prevBuild != nowBuild);

  // Registra sempre quale firmware/build e' installato, ma NON inventa
  // una data OTA se NTP non e' ancora valido.
  if (firmwareChanged || oldInstalledTime == "" || oldInstalledTime == "N/D" || otaPending) {
    prefs.putString("installed_fw", nowFw);
    prefs.putString("installed_name", FW_NAME);
    prefs.putString("installed_build", nowBuild);
  }

  if (otaPending || firmwareChanged) {
    prefs.putString("ota_status", "OK");
    prefs.putString("ota_fw_after", nowFw);
    prefs.putString("ota_build_after", nowBuild);
    if (firmwareChanged) {
      uint32_t id = prefs.getUInt("ota_note_id", 0) + 1;
      prefs.putUInt("ota_note_id", id);
      prefs.putBool("ota_note_seen", false);
      prefs.putString("ota_note_type", "install");
      prefs.putString("ota_note_title", "Aggiornamento completato");
      prefs.putString("ota_note_msg", "Firmware installato correttamente dopo riavvio.");
      prefs.putString("ota_note_level", "ok");
      prefs.putString("ota_note_ver", nowFw);
      prefs.putString("ota_note_time", timeIsValid() ? timeText() : buildText());
    }

    if (timeIsValid()) {
      String realTime = timeText();
      prefs.putString("ota_time", realTime);
      prefs.putString("installed_time", realTime);
      prefs.putString("ota_detail", "Nuovo firmware rilevato al boot dopo flash/OTA. Ora reale NTP salvata.");
      prefs.putBool("ota_pending", false);
      prefs.putBool("ota_needs_ntp_fix", false);
      otaNtpFixPending = false;
    } else {
      // Lascia il pending attivo: verra' corretto in loop appena NTP e' pronto.
      prefs.putString("ota_time", "In attesa NTP");
      prefs.putString("installed_time", "In attesa NTP");
      prefs.putString("ota_detail", "Firmware aggiornato/rilevato. Attendo sincronizzazione NTP per salvare data e ora reali.");
      prefs.putBool("ota_pending", true);
      prefs.putBool("ota_needs_ntp_fix", true);
      otaNtpFixPending = true;
    }
  } else {
    otaNtpFixPending = prefs.getBool("ota_needs_ntp_fix", false) || prefs.getBool("ota_pending", false);
  }
  prefs.end();
}

void updateOtaTimeIfNtpBecomesValid() {
  // Fix definitivo: se l'OTA e' pendente, non scriviamo fallback.
  // Aspettiamo davvero WiFi + NTP valido e poi salviamo data/ora reali.
  prefs.begin("victron", true);
  bool pending = prefs.getBool("ota_pending", false);
  bool needsFix = prefs.getBool("ota_needs_ntp_fix", false);
  prefs.end();

  if (!otaNtpFixPending && !pending && !needsFix) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastOtaNtpFixTryMs < 10000UL) return;
  lastOtaNtpFixTryMs = millis();

  if (!timeIsValid()) {
    syncNtpNow();
  }
  if (!timeIsValid()) return;

  String realTime = timeText();
  prefs.begin("victron", false);
  prefs.putString("ota_time", realTime);
  prefs.putString("installed_time", realTime);
  prefs.putString("ota_detail", "Ora OTA/installazione salvata automaticamente appena NTP e' diventato disponibile.");
  prefs.putBool("ota_pending", false);
  prefs.putBool("ota_needs_ntp_fix", false);
  prefs.end();
  otaNtpFixPending = false;
}


String timeText() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    return buildText();
  }
  if ((timeinfo.tm_year + 1900) < 2024) {
    return buildText();
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &timeinfo);
  return String(buf);
}

bool timeIsValid() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return false;
  return (timeinfo.tm_year + 1900) >= 2024;
}

String buildDateIt() {
  String d = String(FW_BUILD_DATE); // esempio: "May 12 2026"
  String mon = d.substring(0, 3);
  String day = d.substring(4, 6);
  day.trim();
  String year = d.substring(7);
  String mm = "01";
  if (mon == "Jan") mm = "01";
  else if (mon == "Feb") mm = "02";
  else if (mon == "Mar") mm = "03";
  else if (mon == "Apr") mm = "04";
  else if (mon == "May") mm = "05";
  else if (mon == "Jun") mm = "06";
  else if (mon == "Jul") mm = "07";
  else if (mon == "Aug") mm = "08";
  else if (mon == "Sep") mm = "09";
  else if (mon == "Oct") mm = "10";
  else if (mon == "Nov") mm = "11";
  else if (mon == "Dec") mm = "12";
  if (day.length() == 1) day = "0" + day;
  return day + "/" + mm + "/" + year;
}

String buildText() {
  return buildDateIt() + " " + String(FW_BUILD_TIME);
}

void syncNtpNow() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  unsigned long start = millis();
  while (!timeIsValid() && millis() - start < 2500) {
    delay(100);
  }
}

void loadBootCounter() {
  prefs.begin("victron", false);
  bootCounter = prefs.getULong("boots", 0) + 1;
  prefs.putULong("boots", bootCounter);
  prefs.end();
}

void saveDiagnostics() {
  if (millis() - lastDiagSaveMs < 30000UL) return;
  lastDiagSaveMs = millis();

  prefs.begin("victron", false);
  prefs.putString("last_ip", WiFi.localIP().toString());
  prefs.putInt("last_rssi", WiFi.RSSI());
  prefs.putString("last_fw", FW_VERSION);
  prefs.putString("last_time", timeText());
  prefs.end();
}

String diagnosticsJson() {
  String json = "{";
  json += "\"firmware_name\":\"" + String(FW_NAME) + "\",";
  json += "\"firmware_version\":\"" + String(FW_VERSION) + "\",";
  json += "\"firmware_build\":\"" + buildText() + "\",";
  json += "\"time\":\"" + timeText() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"hostname\":\"" + String(HOSTNAME) + "\",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"victron_online\":" + String(victronOnline() ? "true" : "false") + ",";
  json += "\"boots\":" + String(bootCounter) + ",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  return json;
}


void cleanRestartNow(const char* reason) {
  // Spegne backlight e porta i CS alti prima del reset software.
  // Evita lampeggi bianco/nero e bus SPI lasciato selezionato durante restart.
  (void)reason;
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
#ifdef TOUCH_CS
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#endif
  delay(250);
  ESP.restart();
}

void checkFailsafe() {
  if (WiFi.status() == WL_CONNECTED) {
    lastGoodWiFiMs = millis();
  }

  if (victronOnline()) {
    lastGoodVictronMs = millis();
  }

  // Se Wi-Fi resta giù per 10 minuti, riavvia.
  if (lastGoodWiFiMs > 0 && millis() - lastGoodWiFiMs > 600000UL) {
    setUpdateMessage("Riavvio automatico: WiFi assente per oltre 10 minuti");
    delay(200);
    cleanRestartNow("wifi_failsafe");
  }
}

void weeklyGithubUpdateLoop() {
  if (millis() - lastWeeklyUpdateCheckMs < 60000UL) return;
  lastWeeklyUpdateCheckMs = millis();
  if (WiFi.status() != WL_CONNECTED || !timeIsValid()) return;

  prefs.begin("victron", true);
  bool enabled = prefs.getBool("gh_weekly", true);
  int autoDay = prefs.getInt("gh_auto_day", 0);      // 0=domenica ... 6=sabato
  int autoHour = prefs.getInt("gh_auto_hour", 9);
  int autoMinute = prefs.getInt("gh_auto_min", 0);
  String lastKey = prefs.getString("gh_weekly_last", "");
  prefs.end();

  if (!enabled) return;
  if (autoDay < 0 || autoDay > 6) autoDay = 0;
  if (autoHour < 0 || autoHour > 23) autoHour = 9;
  if (autoMinute < 0 || autoMinute > 59) autoMinute = 0;

  struct tm ti;
  if (!getLocalTime(&ti, 50)) return;

  // Finestra di 5 minuti, cosi' non salta il controllo se NTP arriva leggermente in ritardo.
  if (ti.tm_wday != autoDay || ti.tm_hour != autoHour) return;
  if (ti.tm_min < autoMinute || ti.tm_min > autoMinute + 4) return;

  char keyBuf[24];
  snprintf(keyBuf, sizeof(keyBuf), "%04d-%02d-%02d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, autoHour, autoMinute);
  String runKey = String(keyBuf);
  if (runKey == lastKey) return;

  addEventLog("OTA", "Controllo automatico programmato avviato");
  checkGithubUpdate(true);
  if (githubUpdateAvailable) {
    String res;
    bool ok = performGithubBinUpdate(res);
    lastScheduledUpdateStatus = ok ? ("Update automatico avviato: " + res) : ("Update automatico fallito: " + res);
  } else {
    lastScheduledUpdateStatus = "Controllo automatico: nessun aggiornamento disponibile";
  }

  prefs.begin("victron", false);
  prefs.putString("gh_weekly_last", runKey);
  prefs.putString("gh_weekly_status", lastScheduledUpdateStatus);
  prefs.putString("gh_last_check_time", timeText());
  prefs.putString("gh_last_check_status", lastScheduledUpdateStatus);
  prefs.putString("gh_last_remote", githubRemoteVersion);
  prefs.end();
  if (githubUpdateAvailable) {
    saveOtaNotify("weekly", lastScheduledUpdateStatus.indexOf("fallito") >= 0 ? "Errore aggiornamento automatico" : "Aggiornamento automatico avviato", lastScheduledUpdateStatus, lastScheduledUpdateStatus.indexOf("fallito") >= 0 ? "bad" : "ok", githubRemoteVersion);
  } else {
    saveOtaNotify("weekly", "Controllo automatico OTA", "Nessun aggiornamento disponibile. Versione attuale: " + String(FW_VERSION), "info", githubRemoteVersion);
  }
}

void handleRecoveryPage() {
  if (!requireAuth()) return;

  if (server.hasArg("clear")) {
    prefs.begin("victron", false);
    prefs.putBool("force_recovery", false);
    prefs.putBool("recovery_recommended", false);
    prefs.putBool("recovery_active", false);
    prefs.putInt("boot_unconf", 0);
    prefs.putString("recovery_reason", "");
    prefs.putString("safe_boot_status", "Recovery flag cancellati manualmente");
    prefs.end();
    recoveryMode = false;
    addEventLog("RECOVERY", "Recovery flag cancellati da WebUI");
  }

  if (server.hasArg("force")) {
    prefs.begin("victron", false);
    prefs.putBool("force_recovery", true);
    prefs.putString("recovery_reason", "Recovery forzata manualmente da WebUI");
    prefs.end();
    recoveryMode = true;
    addEventLog("RECOVERY", "Recovery forzata da WebUI");
  }

  prefs.begin("victron", true);
  int unconf = prefs.getInt("boot_unconf", 0);
  bool forced = prefs.getBool("force_recovery", false);
  bool recommended = prefs.getBool("recovery_recommended", false);
  bool rec = forced || recommended || prefs.getBool("recovery_active", false);
  String safe = prefs.getString("safe_boot_status", "N/D");
  String reason = prefs.getString("recovery_reason", "N/D");
  String stableFw = prefs.getString("stable_fw", "N/D");
  String stableTime = prefs.getString("stable_time", "N/D");
  String lastPre = prefs.getString("last_pre_ota_status", "N/D");
  prefs.end();

  String html = htmlHeader("Recovery / Safe Mode");
  html += "<h1>Recovery / Safe Mode</h1><p><a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/firmware'>Firmware</a> &middot; <a href='/ota-status'>OTA Status</a> &middot; <a href='/'>Dashboard</a></p>";

  html += "<div class='grid'>";
  html += card("Recovery mode", rec ? "ATTIVA" : "NON ATTIVA", "Forzata: " + String(forced ? "SI" : "NO") + "<br>Automatica consigliata: " + String(recommended ? "SI" : "NO"));
  html += card("Boot non confermati", String(unconf) + " / " + String(SAFE_BOOT_MAX_UNCONFIRMED), "Conferma stabile dopo " + String(SAFE_BOOT_CONFIRM_MS / 1000UL) + " secondi con WiFi OK");
  html += card("Firmware stabile", esc(stableFw), "Ultima conferma: " + esc(stableTime));
  html += card("Backup pre-OTA", esc(lastPre), "SD: " + String(sdMounted ? "montata" : "non montata"));
  html += "</div>";

  html += "<div class='card'><div class='t'>Stato sicurezza boot</div>";
  html += "<p><b>Safe boot:</b> " + esc(safe) + "</p>";
  html += "<p><b>Rollback:</b> " + esc(rollbackStatus) + "</p>";
  html += "<p><b>Motivo recovery:</b> " + esc(reason) + "</p>";
  html += "<p><a class='button' href='/recovery?clear=1'>Cancella recovery flag</a><a class='button' href='/recovery?force=1'>Forza recovery flag</a><a class='button' href='/reboot-recovery' onclick=\"return confirm('Riavviare ora in Recovery Mode?')\">Riavvia ora in Recovery</a></p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Funzioni disponibili</div>";
  html += "<p>Da qui puoi recuperare il dispositivo se un update non parte bene. Le funzioni pesanti restano fuori da questa pagina.</p>";
  html += "<p><a class='button' href='/update'>OTA locale</a><a class='button' href='/github-update'>GitHub Update</a><a class='button' href='/recovery-restore-pro'>Restore firmware da SD</a><a class='button' href='/backup-recovery-pro'>Backup/Recovery Pro</a><a class='button' href='/diag'>Diagnostica</a></p>";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleRebootRecovery() {
  if (!requireAuth()) return;
  prefs.begin("victron", false);
  prefs.putBool("force_recovery", true);
  prefs.putBool("recovery_active", true);
  prefs.putString("recovery_reason", "Riavvio manuale in Recovery richiesto da WebUI");
  prefs.putString("safe_boot_status", "Riavvio manuale in Recovery in corso");
  prefs.end();
  addEventLog("RECOVERY", "Riavvio manuale in Recovery richiesto da WebUI");
  sendActionPage("Riavvio in Recovery", "Recovery flag salvato. La CYD si riavvia tra pochi secondi.", 8, "/recovery");
  otaRestartPending = true;
  otaRestartAtMs = millis() + 2500UL;
}



String htmlHeader(String title) {
  String h;
  h += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>" + title + "</title>";
  h += "<meta name='theme-color' content='#08111f'><meta name='apple-mobile-web-app-capable' content='yes'>";
  h += "<style>";
  h += "body{font-family:Arial,Helvetica,sans-serif;background:linear-gradient(180deg,#08111f,#0d1117 260px);color:#e6edf3;margin:0;padding:14px;padding-bottom:120px}";
  h += "a{color:#79c0ff;text-decoration:none}.button{display:inline-block;background:#21262d;border:1px solid #30363d;border-radius:12px;padding:10px 14px;margin:4px}h1{margin:0 0 4px;color:#e6f3ff;font-size:30px}.sub{color:#8b949e;margin-bottom:16px;line-height:1.5}";
  h += ".top{background:rgba(22,27,34,.92);border:1px solid #30363d;border-radius:18px;padding:16px;margin-bottom:14px;box-shadow:0 8px 30px #0007}";
  h += ".nav{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}.nav a{background:#1f6feb22;border:1px solid #30363d;border-radius:999px;padding:8px 12px;color:#c9d1d9}";
  h += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}";
  h += ".card{background:rgba(22,27,34,.96);border:1px solid #30363d;border-radius:18px;padding:16px;box-shadow:0 8px 24px #0005}";
  h += ".card.good{border-color:#23863655}.card.warn{border-color:#d2992255}.card.blue{border-color:#1f6feb66}.card.danger{border-color:#ff7b7255}";
  h += ".t{color:#8b949e;font-size:13px;text-transform:uppercase;letter-spacing:.09em}.v{font-size:36px;color:#7ee787;margin-top:8px;font-weight:800}.e{color:#c9d1d9;margin-top:8px;line-height:1.4}";
  h += ".metricrow{display:flex;justify-content:space-between;gap:10px;margin-top:8px}.label{color:#8b949e}.value{color:#e6edf3;font-weight:bold}.quickgrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.quick{display:block;background:#161b22;border:1px solid #30363d;border-radius:16px;padding:14px;color:#e6edf3}.quick b{display:block;font-size:18px;color:#79c0ff;margin-bottom:6px}.quick span{display:block;color:#8b949e;line-height:1.35;font-size:14px}";
  h += ".status{display:inline-block;border-radius:999px;padding:5px 10px;font-size:13px;border:1px solid #30363d;background:#161b22}.ok{color:#7ee787}.warn{color:#f2cc60}.bad{color:#ff7b72}";
  h += "button,input{font-size:17px;border-radius:12px;border:1px solid #30363d;padding:10px;background:#21262d;color:#e6edf3}";
  h += "canvas{width:100%;height:180px;background:#0b1220;border-radius:14px;margin-top:12px;border:1px solid #30363d}";
  h += ".banner{background:#12351f;border:1px solid #2ea043;color:#7ee787;border-radius:14px;padding:14px;margin:12px 0;font-weight:bold}.bigtest{font-size:38px;font-weight:950;color:#7ee787;line-height:1.05}.pill{display:inline-block;border:1px solid #30363d;border-radius:999px;padding:6px 10px;margin:3px;background:#161b22;color:#c9d1d9;text-decoration:none}.barbg{height:12px;background:#0b1220;border:1px solid #30363d;border-radius:999px;overflow:hidden}.barfg{height:100%;background:#7ee787;width:0%}";
  h += ".fw{font-size:13px;color:#8b949e;margin:6px 0 0}.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0}.tabs button{cursor:pointer}.tablewrap{overflow:auto}.hist{width:100%;border-collapse:collapse;margin-top:12px}.hist th,.hist td{border-bottom:1px solid #30363d;padding:9px;text-align:right}.hist th:first-child,.hist td:first-child{text-align:left}";
  h += ".chrono{background:#2f73ad;border-radius:16px;padding:0;overflow:auto;color:white;margin-top:14px;border:1px solid #6fa8d7}.chrono table{border-collapse:collapse;width:100%;min-width:760px;background:#2f73ad}.chrono th,.chrono td{border-left:1px solid rgba(255,255,255,.18);padding:8px;text-align:center;vertical-align:bottom}.chrono th{color:#e8f5ff;font-weight:700;background:#347dbd}.barcell{height:230px;position:relative;background:linear-gradient(90deg,rgba(0,0,0,.08),rgba(255,255,255,.04))}.bar{position:absolute;bottom:0;left:28%;width:44%;background:#9ec2df;border-top:2px solid #d7e8f5}.barload{position:absolute;bottom:0;left:28%;width:44%;background:#f4f4f4;border-top:2px solid white}.rowtitle{text-align:left!important;color:#dcefff;font-size:15px;background:rgba(0,0,0,.12)}.chrono .small{font-size:13px;color:#dcefff}.bluehead{background:#3887cc;padding:10px;font-size:20px;font-weight:bold}";
  h += ".batHero{position:relative;overflow:hidden;border-radius:22px;border:1px solid #23863677;background:radial-gradient(circle at 20% 0%,#1f6feb55,transparent 32%),linear-gradient(135deg,#0d1117,#12201a 70%,#16261d);padding:18px;box-shadow:0 14px 36px #0009}.batHero:after{content:'';position:absolute;right:-35px;top:-35px;width:130px;height:130px;border-radius:50%;background:#7ee78722}.batValue{font-size:58px;font-weight:950;color:#7ee787;letter-spacing:-2px;line-height:.95;margin:12px 0}.batGauge{height:34px;background:#0b1220;border:1px solid #30363d;border-radius:999px;padding:4px;overflow:hidden}.batFill{height:100%;border-radius:999px;background:linear-gradient(90deg,#2ea043,#7ee787);box-shadow:0 0 18px #7ee78777}.batGrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:12px}.batMini{background:#0d1117cc;border:1px solid #30363d;border-radius:14px;padding:12px}.batMini b{display:block;font-size:20px;color:#e6edf3;margin-top:4px}.batIcon{float:right;width:86px;height:38px;border:3px solid #e6edf3;border-radius:6px;position:relative;margin-top:6px}.batIcon:after{content:'';position:absolute;right:-10px;top:10px;width:7px;height:16px;background:#e6edf3;border-radius:2px}.batIcon span{display:block;height:100%;border-radius:3px;background:#7ee787}.batWarn{border-color:#d29922!important}.batBad{border-color:#ff7b72!important}";

  h += ".gxHero{border:1px solid #30363d;border-radius:24px;padding:18px;margin-bottom:14px;background:radial-gradient(circle at 20% -20%,#1f6feb55,transparent 36%),linear-gradient(135deg,#0b1220,#101820 60%,#102418);box-shadow:0 16px 40px #0009;overflow:hidden}";
  h += ".gxTop{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}.gxTitle{font-size:30px;font-weight:950;color:#e6f3ff;line-height:1}.gxSub{color:#8b949e;margin-top:6px}.gxPills{display:flex;gap:7px;flex-wrap:wrap;justify-content:flex-end}.gxPill{border:1px solid #30363d;background:#0d1117bb;border-radius:999px;padding:6px 10px;color:#c9d1d9;font-size:13px}.gxGrid{display:grid;grid-template-columns:1.25fr 1fr;gap:12px;margin-top:14px}.gxBig{border:1px solid #30363d;border-radius:20px;padding:16px;background:rgba(13,17,23,.76)}.gxBig.sun{border-color:#d2992255;background:radial-gradient(circle at 95% 10%,#d2992244,transparent 28%),rgba(13,17,23,.78)}.gxBig.bat{border-color:#2ea04355;background:radial-gradient(circle at 95% 10%,#2ea04344,transparent 28%),rgba(13,17,23,.78)}.gxLabel{color:#8b949e;text-transform:uppercase;letter-spacing:.12em;font-size:12px}.gxValue{font-size:52px;font-weight:950;letter-spacing:-2px;color:#f2cc60;line-height:.98;margin:10px 0}.gxBig.bat .gxValue{color:#7ee787}.gxDetails{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:10px}.gxMini{background:#0b1220;border:1px solid #30363d;border-radius:14px;padding:10px}.gxMini span{display:block;color:#8b949e;font-size:12px}.gxMini b{display:block;color:#e6edf3;font-size:18px;margin-top:2px}.gxBar{height:16px;background:#050b14;border:1px solid #30363d;border-radius:999px;overflow:hidden;margin-top:10px}.gxBarFill{height:100%;border-radius:999px;background:linear-gradient(90deg,#9e6a03,#f2cc60)}.gxBig.bat .gxBarFill{background:linear-gradient(90deg,#238636,#7ee787)}.gxSide{display:grid;grid-template-columns:1fr;gap:12px}.gxStatus{border:1px solid #30363d;border-radius:18px;padding:14px;background:rgba(13,17,23,.78)}.gxStatus .state{font-size:24px;font-weight:900;color:#79c0ff;margin-top:6px}.gxActions{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:14px}.gxAction{background:#161b22;border:1px solid #30363d;border-radius:16px;padding:13px;color:#e6edf3;text-align:center}.gxAction b{display:block;color:#79c0ff;margin-bottom:4px}.gxAction span{font-size:12px;color:#8b949e}.gxSection{margin-top:16px}.gxSectionTitle{display:flex;align-items:center;gap:8px;color:#e6edf3;font-weight:900;font-size:16px;margin:4px 0 8px}.gxSectionTitle span{color:#8b949e;font-size:12px;font-weight:500}.gxSection .gxActions{margin-top:0}.gxAction.warn b{color:#f2cc60}.gxAction.good b{color:#7ee787}.gxAction.danger b{color:#ff7b72}";
  h += ".dataTable{width:100%;border-collapse:collapse;margin-top:10px}.dataTable td{border-bottom:1px solid #30363d;padding:10px}.dataTable td:first-child{color:#8b949e}.dataTable td:last-child{text-align:right;font-weight:800;color:#e6edf3}.liveSmall{font-size:12px;color:#8b949e;margin-top:6px}.gxPill.ok{color:#7ee787}.gxPill.warn{color:#f2cc60}.gxPill.bad{color:#ff7b72}";

  h += "@media(max-width:650px){body{padding:10px}h1{font-size:26px}.grid{grid-template-columns:1fr}.quickgrid{grid-template-columns:1fr}.v{font-size:32px}.card{padding:14px}.nav a{padding:8px 10px}.gxGrid{grid-template-columns:1fr}.gxActions{grid-template-columns:repeat(2,1fr)}.gxValue{font-size:44px}.gxTop{display:block}.gxPills{justify-content:flex-start;margin-top:10px}}";

  h += ".gxSection{margin-top:14px;border:1px solid #30363d;border-radius:20px;background:rgba(13,17,23,.72);padding:14px;box-shadow:0 10px 28px #0005}.gxSectionTitle{font-size:18px;font-weight:900;color:#e6f3ff;margin-bottom:10px}.gxSectionTitle span{display:block;font-size:12px;font-weight:500;color:#8b949e;margin-top:2px}.gxActions{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.gxAction{display:block;border:1px solid #30363d;border-radius:16px;background:linear-gradient(180deg,#161b22,#0d1117);padding:13px;color:#e6edf3;min-height:56px}.gxAction b{display:block;color:#79c0ff;font-size:17px;margin-bottom:4px}.gxAction span{display:block;color:#8b949e;font-size:13px;line-height:1.28}.gxAction.good b{color:#7ee787}.gxAction.warn b{color:#f2cc60}.gxAction.danger b{color:#ff7b72}";
  h += ".gxHero{border:1px solid #30363d;border-radius:24px;padding:18px;margin-bottom:14px;background:radial-gradient(circle at 20% -20%,#1f6feb55,transparent 36%),linear-gradient(135deg,#0b1220,#101820 60%,#102418);box-shadow:0 16px 40px #0009;overflow:hidden}.gxTop{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}.gxTitle{font-size:30px;font-weight:950;color:#e6f3ff;line-height:1}.gxPills{display:flex;gap:7px;flex-wrap:wrap;justify-content:flex-end}.gxPill{border:1px solid #30363d;background:#0d1117bb;border-radius:999px;padding:6px 10px;color:#c9d1d9;font-size:13px}.gxGrid{display:grid;grid-template-columns:1.25fr 1fr;gap:12px;margin-top:14px}.gxBig{border:1px solid #30363d;border-radius:20px;padding:16px;background:rgba(13,17,23,.76)}.gxBig.sun{border-color:#d2992255;background:radial-gradient(circle at 95% 10%,#d2992244,transparent 28%),rgba(13,17,23,.78)}.gxBig.bat{border-color:#2ea04355;background:radial-gradient(circle at 95% 10%,#2ea04344,transparent 28%),rgba(13,17,23,.78)}.gxLabel{color:#8b949e;text-transform:uppercase;letter-spacing:.12em;font-size:12px}.gxValue{font-size:52px;font-weight:950;letter-spacing:-2px;color:#f2cc60;line-height:.98;margin:10px 0}.gxBig.bat .gxValue{color:#7ee787}.gxDetails{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:10px}.gxMini{background:#0b1220;border:1px solid #30363d;border-radius:14px;padding:10px}.gxMini span{display:block;color:#8b949e;font-size:12px}.gxMini b{display:block;color:#e6edf3;font-size:18px;margin-top:2px}.gxBar{height:16px;background:#050b14;border:1px solid #30363d;border-radius:999px;overflow:hidden;margin-top:10px}.gxBarFill{height:100%;border-radius:999px;background:linear-gradient(90deg,#9e6a03,#f2cc60)}.gxBig.bat .gxBarFill{background:linear-gradient(90deg,#238636,#7ee787)}.gxSide{display:grid;grid-template-columns:1fr;gap:12px}.gxStatus{border:1px solid #30363d;border-radius:18px;padding:14px;background:rgba(13,17,23,.78)}.state{font-size:26px;font-weight:900;color:#7ee787;margin-top:8px}.gxSub{color:#8b949e;margin-top:6px;line-height:1.35}";
  h += ".nav{position:sticky;top:0;z-index:5;background:rgba(13,17,23,.72);backdrop-filter:blur(8px);border:1px solid #30363d;border-radius:16px;padding:8px}.card{backdrop-filter:blur(4px)}@media(max-width:850px){.gxActions{grid-template-columns:repeat(2,1fr)}.gxGrid{grid-template-columns:1fr}.gxTop{display:block}.gxPills{justify-content:flex-start;margin-top:10px}}";

  // V10.4.78 - WebUI Cleanup Pro: ritocco grafico comune senza toccare TFT/touch/VE.Direct.
  h += "*{box-sizing:border-box}html{scroll-behavior:smooth}body{font-size:16px;-webkit-font-smoothing:antialiased}";
  h += ".top,.card,.gxHero,.gxSection{transition:transform .16s ease,border-color .16s ease,box-shadow .16s ease}.card:hover,.gxSection:hover{border-color:#58a6ff66;box-shadow:0 12px 30px #0007}";
  h += ".button,.pill,.gxAction,.quick{transition:transform .14s ease,filter .14s ease,border-color .14s ease}.button:hover,.pill:hover,.gxAction:hover,.quick:hover{transform:translateY(-1px);filter:brightness(1.08);border-color:#58a6ff99}";
  h += ".nav a{font-weight:700}.nav a:hover{background:#1f6feb44;color:#e6f3ff}.gxPill.ok,.status.ok{border-color:#2ea04366;background:#12351f;color:#7ee787}.gxPill.bad,.status.bad{border-color:#ff7b7266;background:#351316;color:#ffb4ad}.gxPill.warn,.status.warn{border-color:#d2992266;background:#30250b;color:#f2cc60}.gxPill.weak,.status.weak{border-color:#fb850066;background:#332006;color:#ffb86b}.otaModal{position:fixed;inset:0;background:#0008;display:flex;align-items:center;justify-content:center;z-index:9999;padding:18px}.otaModalBox{max-width:420px;width:100%;background:#0d1117;border:1px solid #30363d;border-radius:20px;padding:18px;box-shadow:0 20px 60px #000b}.otaModalBox h2{margin:0 0 8px}.otaModalBox .time{color:#8b949e;font-size:13px;margin-top:8px}.otaModalBox.ok{border-color:#2ea04388}.otaModalBox.bad{border-color:#ff7b7288}.otaModalBox.warn,.otaModalBox.weak{border-color:#d2992288}";
  h += ".histTip,.smartTip{background:#0b1220;border:1px solid #58a6ff;border-radius:14px;padding:10px;box-shadow:0 10px 30px #0008;z-index:8;font-size:13px;line-height:1.35}.barSel{background:#f2cc60!important;box-shadow:0 0 0 2px #f2cc6044}.barStd{background:#79c0ff}.muted{color:#8b949e}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}";
  h += "pre{font-size:13px;line-height:1.35}.dataTable tr:hover,.hist tr:hover{background:#1f6feb18}.sectionNote{color:#8b949e;font-size:13px;line-height:1.45;margin-top:6px}";
  h += ".chartMeta,.chartStats{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}.chartMeta span,.chartStats span{background:#0b1220;border:1px solid #30363d;border-radius:999px;padding:6px 10px;color:#8b949e}.chartMeta b,.chartStats b{color:#e6edf3}.chartWrap{display:grid;grid-template-columns:58px 1fr;gap:8px;align-items:stretch}.chartWrap canvas{margin-top:0}.yAxis{display:flex;flex-direction:column;justify-content:space-between;text-align:right;color:#8b949e;font-size:12px;padding:10px 0}.tipTitle{font-weight:900;font-size:15px;margin-bottom:6px;color:#fff}.tipGrid{display:grid;grid-template-columns:1fr auto;gap:3px 10px;margin-top:5px}.tipGrid span{color:#c9d1d9}.tipGrid b{color:#fff;text-align:right}.liveChartCard .sectionNote{margin-bottom:8px}.histTipBox{display:none;background:#0b1220;border:1px solid #58a6ff;border-radius:14px;padding:12px;margin:10px 0 12px;box-shadow:0 10px 26px #0007;font-size:14px;line-height:1.35}.histTipBox .tipTitle{font-size:16px}.histChartArea{display:flex;align-items:end;gap:6px;height:130px;border-bottom:1px solid rgba(255,255,255,.35);padding-top:8px}.noData{color:#8b949e;font-style:italic;margin-top:6px}.canvasHint{color:#8b949e;font-size:13px;margin:6px 0 0}.gxChartMeta{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}.gxChartMeta span{background:#0b1220;border:1px solid #30363d;border-radius:999px;padding:6px 10px;color:#8b949e}.gxChartMeta b{color:#e6edf3}";
  h += "@media(max-width:650px){.gxAction{min-height:50px}.gxAction b{font-size:16px}.gxAction span{font-size:12px}.tabs button{flex:1}.hist th,.hist td{padding:7px}.smartTip,.histTip{font-size:12px}}";
  String theme = uiTheme();
  if (theme == "light") {
    h += "body{background:#f4f7fb!important;color:#101820!important} .card,.top,.gxSection,.gxHero,.gxBig,.gxStatus,.gxAction{background:#ffffff!important;color:#101820!important;border-color:#d0d7de!important;box-shadow:0 8px 20px #0001!important}.t,.label,.gxSub,.gxMini span,.gxAction span{color:#57606a!important}.v,.gxTitle,.value,.gxMini b{color:#0969da!important}.button{background:#f6f8fa!important;color:#0969da!important;border-color:#d0d7de!important}.nav{background:#ffffffcc!important}.dataTable td,.hist th,.hist td{border-color:#d0d7de!important;color:#24292f!important}";
  } else if (theme == "victron") {
    h += "body{background:linear-gradient(180deg,#042b46,#07131f 260px)!important}.gxHero{background:radial-gradient(circle at 20% -20%,#00a3e055,transparent 36%),linear-gradient(135deg,#042b46,#07131f 70%,#053b5c)!important}.gxAction b,.gxPill.ok{color:#00a3e0!important}.v,.gxBig.bat .gxValue{color:#7ee787!important}";
  } else if (theme == "compact") {
    h += "body{padding:8px!important}.card,.gxHero,.gxSection{padding:10px!important;border-radius:14px!important}.gxActions{gap:6px!important}.gxAction{padding:9px!important;min-height:42px!important}.gxValue{font-size:36px!important}.v{font-size:28px!important}h1{font-size:24px!important}";
  }
  h += "</style></head><body>";
  return h;
}


String urlEncode(const String& in) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String card(String title, String value, String extra = "") {
  String s;
  s += "<div class='card'><div class='t'>" + title + "</div><div class='v'>" + value + "</div>";
  if (extra.length()) s += "<div class='e'>" + extra + "</div>";
  s += "</div>";
  return s;
}

void handleRoot() {
  if (publicWizardShouldRedirectToSetup()) {
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "Redirecting to setup wizard...");
    return;
  }
  if (!requireAuth()) return;

  String html = htmlHeader("Victron Monitor");
  html += "<div class='top'>";
  html += "<h1>Victron Monitor</h1>";
  html += "<div class='fw'>Firmware: " + String(FW_NAME) + " - " + String(FW_VERSION) + "</div>";
  html += "<div class='fw'>Ora: " + timeText() + " &middot; Boot: " + String(bootCounter) + " &middot; Heap: " + String(ESP.getFreeHeap()) + "</div>";
  html += "<div class='sub'>IP " + WiFi.localIP().toString();
  html += " &middot; " + String(victronOnline() ? "VE.Direct online" : "VE.Direct non collegato");
  html += " &middot; RSSI " + String(WiFi.RSSI()) + " dBm";
  html += "</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/updates'>Aggiornamenti & Sicurezza</a><a href='/data-center'>Dati & Storico</a><a href='/victron-data'>Dati Victron</a><a href='/energy-today'>Energia oggi</a><a href='/history-gx'>Storico GX</a><a href='/alerts'>Alert</a><a href='/setup-check'>Setup</a><a href='/settings'>Impostazioni</a><a href='/system-pro'>Sistema</a><a href='/battery'>Batteria ESP</a><a href='/power'>Power</a><a href='/network'>Rete/IP</a><a href='/files'>File & Log</a><a href='/diag'>Diagnostica</a><a href='/quick-check'>Check rapido</a></div>";
  html += "</div>";


  float cfgPanelW = configuredPanelWatts();
  if (!isfinite(cfgPanelW) || cfgPanelW < 1.0f) cfgPanelW = 120.0f;

    String updateMsg = getUpdateMessage();
  if (updateMsg.length()) {
    html += "<div class='banner'>" + esc(updateMsg) + "</div>";
    clearUpdateMessage();
  }

  {
    int pvPct = (int)constrain(panelW / max(1.0f, cfgPanelW) * 100.0f, 0.0f, 100.0f);
    int batPct = batteryPercent(battV);
    float ev = espBatteryVoltage();
    float ep = espBatteryPercent();
    int ew = (int)constrain(isnan(ep) ? 0 : ep, 0, 100);
    String ec = espBatteryStatusText();
    String ve = victronOnline() ? "Online" : "Offline";
    String wf = WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi KO";

    html += "<div class='gxHero'>";
    html += "<div class='gxTop'><div><div class='gxTitle'>Victron GX Dashboard</div><div class='gxSub'>Monitor solare, batteria impianto e batteria ESP</div></div>";
    html += "<div class='gxPills'><span class='gxPill " + wifiBadgeClass(WiFi.RSSI()) + "' id='badgeWifi'>" + wf + "</span><span class='gxPill " + veBadgeClass() + "' id='badgeVe'>VE.Direct " + ve + "</span><span class='gxPill " + espBatteryBadgeClass(ep) + "' id='badgeEsp'>BAT ESP " + String(isnan(ep)?0:ep,0) + "%</span><span class='gxPill' id='badgeClock'>" + timeText() + "</span></div></div>";

    html += "<div class='gxGrid'>";
    html += "<div class='gxBig sun'><div class='gxLabel'>Solare</div><div class='gxValue'><span id='pw'>" + fmt(panelW,0,"") + "</span> W</div>";
    html += "<div class='gxBar'><div id='pvbar' class='gxBarFill' style='width:" + String(pvPct) + "%'></div></div>";
    html += "<div class='gxDetails'><div class='gxMini'><span>PV voltage</span><b><span id='pv'>" + fmt(panelV,2,"") + "</span> V</b></div><div class='gxMini'><span>Oggi</span><b><span id='yt'>" + fmt(yieldTodayKWh,2,"") + "</span> kWh</b></div></div></div>";

    html += "<div class='gxSide'>";
    html += "<div class='gxBig bat'><div class='gxLabel'>Batteria impianto</div><div class='gxValue' style='font-size:40px'><span id='bv'>" + fmt(battV,2,"") + "</span> V</div>";
    html += "<div class='gxBar'><div id='batbar' class='gxBarFill' style='width:" + String(batPct) + "%'></div></div>";
    html += "<div class='gxDetails'><div class='gxMini'><span>Corrente</span><b><span id='ba'>" + fmt(battA,2,"") + "</span> A</b></div><div class='gxMini'><span>Potenza</span><b><span id='bw'>" + fmt(battW,1,"") + "</span> W</b></div></div></div>";

    html += "<div class='gxStatus'><div class='gxLabel'>Stato carica</div><div class='state'><span id='cs'>" + esc(chargeState) + "</span></div><div class='gxSub'>Errore: <span id='err'>" + esc(errorState) + "</span> &middot; Totale: <span id='ytot'>" + fmt(yieldTotalKWh,2,"") + "</span> kWh</div></div>";
    html += "</div></div>";

    html += "<div class='gxSection'><div class='gxSectionTitle'>Monitoraggio <span>dati Victron e produzione</span></div><div class='gxActions'>";
    html += "<a class='gxAction' href='/energy-today'><b>Energia</b><span>Produzione oggi</span></a><a class='gxAction' href='/plant-info'><b>Impianto</b><span>Dati pannello/batteria</span></a>";
    html += "<a class='gxAction' href='/history-gx'><b>Storico GX</b><span>Grafici web</span></a>";
    html += "<a class='gxAction' href='/stats-sd'><b>Statistiche</b><span>Recap SD</span></a>";
    html += "<a class='gxAction' href='/victron-data'><b>VE.Direct</b><span>Dati tecnici</span></a>";
    html += "</div></div>";

    html += "<div class='gxSection'><div class='gxSectionTitle'>Storage & microSD <span>log, CSV e memoria</span></div><div class='gxActions'>";
    html += "<a class='gxAction' href='/sd'><b>MicroSD</b><span>Monta / smonta / formatta</span></a><a class='gxAction' href='/files'><b>File & Log</b><span>Apri, scarica, elimina</span></a><a class='gxAction' href='/diag-snapshot'><b>Snapshot</b><span>Pacchetto diagnostico</span></a>";
    html += "<a class='gxAction' href='/storage'><b>Storage</b><span>" + storageMode() + "</span></a>";
    html += "<a class='gxAction' href='/sd-logs'><b>Log SD</b><span>CSV giornalieri</span></a><a class='gxAction' href='/sd-maintenance'><b>Manutenzione</b><span>Pulisci / verifica</span></a><a class='gxAction' href='/sd-retention'><b>Retention</b><span>Pulizia auto</span></a><a class='gxAction' href='/sd-integrity'><b>Integrita' SD</b><span>Indice e CSV</span></a><a class='gxAction' href='/sd-write-protection'><b>Scrittura SD</b><span>Protezione</span></a>";
    html += "<a class='gxAction' href='/history.csv'><b>CSV</b><span>Export storico</span></a>";
    html += "</div></div>";

    html += "<div class='gxSection'><div class='gxSectionTitle'>Backup / Recovery <span>ripristino e sicurezza</span></div><div class='gxActions'>";
    html += "<a class='gxAction good' href='/backup-recovery'><b>Backup / Recovery</b><span>Backup completo</span></a><a class='gxAction' href='/backup-recovery-pro'><b>Recovery Pro</b><span>Firmware/config</span></a><a class='gxAction warn' href='/recovery-restore-pro'><b>Restore firmware</b><span>Da SD con verifica</span></a><a class='gxAction' href='/backup-list'><b>Ripristino</b><span>Lista backup SD</span></a><a class='gxAction' href='/daily-backups'><b>Backup giornalieri</b><span>Config ultimi 5</span></a>";
    html += "<a class='gxAction' href='/settings-backup'><b>Backup config</b><span>Esporta / importa</span></a>";
    html += "<a class='gxAction' href='/backup-sd'><b>Backup SD</b><span>Salva config su SD</span></a>";
    html += "<a class='gxAction' href='/recovery'><b>Safe Mode</b><span>Recovery firmware</span></a>";
    html += "</div></div>";

    html += "<div class='gxSection'><div class='gxSectionTitle'>Aggiornamenti <span>firmware e OTA</span></div><div class='gxActions'>";
    html += "<a class='gxAction warn' href='/updates'><b>OTA</b><span>GitHub & locale</span></a>";
    html += "<a class='gxAction' href='/ota-center'><b>OTA Center</b><span>Programmazione</span></a>";
    html += "<a class='gxAction' href='/github-update'><b>GitHub</b><span>Update remoto</span></a>";
    html += "<a class='gxAction' href='/firmware'><b>Firmware</b><span>Upload locale</span></a>";
    html += "</div></div>";

    html += "<div class='gxSection'><div class='gxSectionTitle'>Diagnostica & dispositivo <span>stato, alert e strumenti</span></div><div class='gxActions'>";
    html += "<a class='gxAction' href='/health'><b>Health</b><span>" + String(healthScoreNow()) + "/100</span></a><a class='gxAction' href='/alerts'><b>Alert</b><span>" + String(alertCountNow()) + " attivi</span></a>";
    html += "<a class='gxAction' href='/setup-check'><b>Setup</b><span>Checklist</span></a>";
    html += "<a class='gxAction' href='/logs'><b>Log</b><span>Eventi</span></a>";
    html += "<a class='gxAction' href='/system-pro'><b>Sistema</b><span>RSSI " + String(WiFi.RSSI()) + " dBm</span></a>";
    html += "<a class='gxAction' href='/power'><b>Power</b><span>Alimentazione & reboot</span></a><a class='gxAction' href='/battery'><b>BAT ESP</b><span>" + String(isnan(ep)?0:ep,0) + "% - " + ec + "</span></a>";
    html += "<a class='gxAction' href='/thresholds'><b>Soglie</b><span>Alert configurabili</span></a><a class='gxAction' href='/alerts-history'><b>Storico alert</b><span>Log mensile</span></a><a class='gxAction good' href='/quick-check'><b>Check rapido</b><span>Stato essenziale</span></a><a class='gxAction good' href='/diagnostic-run'><b>Diagnostica completa</b><span>Check completo</span></a><a class='gxAction' href='/theme'><b>Tema</b><span>Web UI</span></a><a class='gxAction' href='/touch-cal'><b>Touch</b><span>Calibra</span></a><a class='gxAction' href='/watchdog'><b>Watchdog</b><span>Affidabilita'</span></a><a class='gxAction' href='/reboot-history'><b>Reboot</b><span>Storico reset</span></a><a class='gxAction' href='/hardware-test'><b>Hardware test</b><span>TFT/SD/VE</span></a><a class='gxAction' href='/vedirect-raw'><b>VE Raw</b><span>Debug seriale</span></a><a class='gxAction' href='/time-ntp'><b>Ora/NTP</b><span>Log precisi</span></a><a class='gxAction' href='/api/v1'><b>API v1</b><span>JSON ordinati</span></a>";
    html += "</div></div>";
    html += "</div>";

    String eclass = ec == "Critica" ? "danger" : (ec == "Bassa" ? "warn" : "good");
    html += "<div class='card " + eclass + "'><div class='t'>Batteria ESP / LiPo tampone</div>";
    html += "<div style='display:flex;align-items:center;justify-content:space-between;gap:12px'><div><div class='v' style='font-size:34px'><span id='espv'>" + String(isnan(ev) ? 0 : ev,2) + "</span> V</div><div class='e'><b><span id='esppct'>" + String(isnan(ep) ? 0 : ep,0) + "</span>%</b> - <span id='espstat'>" + ec + "</span></div></div><div class='batIcon'><span id='espicon' style='width:" + String(ew) + "%'></span></div></div>";
    html += "<div class='barbg' style='height:14px;margin-top:12px'><div id='espbar' class='barfg' style='width:" + String(ew) + "%'></div></div>";
    html += "<div class='e'><a class='button' href='/battery'>Apri Batteria ESP</a> <a class='button' href='/bat-scan'>BAT scan</a></div></div>";
  }

  // Accessi rapidi rimossi: le funzioni sono ora nelle sezioni ordinate.


  html += "<div class='card liveChartCard'><div class='t'>Grafico live potenza pannello</div>";
  html += "<div class='sectionNote'>Linea gialla = Watt istantanei dal Victron. Scala basata su potenza pannello configurata in Impianto.</div>";
  html += "<div class='chartMeta'><span>Attuale <b id='chartNow'>" + fmt(panelW,0,"") + " W</b></span><span>Pannello <b id='chartPlant'>" + String(cfgPanelW,0) + " W</b></span><span>Scala <b id='chartScale'>0-" + String(cfgPanelW,0) + " W</b></span></div>";
  html += "<div class='chartWrap'><div class='yAxis'><span id='yTop'>" + String(cfgPanelW,0) + " W</span><span id='yMid'>" + String(cfgPanelW/2.0f,0) + " W</span><span>0 W</span></div><canvas id='chart' width='600' height='170'></canvas></div>";
  html += "<div class='chartStats'><span>Max live <b id='chartMax'>0 W</b></span><span>Media live <b id='chartAvg'>0 W</b></span><span>Campioni <b id='chartSamples'>0</b></span></div>";
  html += "<div class='e'><a class='pill' href='/data-center'>Centro dati</a><a class='pill' href='/history-compact?type=daily'>Storico</a><a class='pill' href='/updates'>Aggiornamenti</a></div></div>";

  html += "<div class='card'>";
  html += "<div class='t'>Cronologia produzione</div>";
  html += "<div class='tabs'>";
  html += "<button id='btnDaily' onclick=\"loadChrono('daily');return false;\">7 giorni</button>";
  html += "<button id='btnHourly' onclick=\"loadChrono('hourly');return false;\">12 ore</button>";
  html += "<button id='btnDaily31' onclick=\"loadChrono('daily31');return false;\">31 giorni</button>";
  html += "<button id='btnMonthly' onclick=\"loadChrono('monthly');return false;\">12 mesi</button>";
  html += "</div>";
  html += "<div class='chrono' id='chronoBox'><div class='bluehead'>Caricamento cronologia...</div></div>";
  html += "</div>";



  html += R"rawliteral(
<script>
let data=[Number(((document.getElementById('chartNow')||{}).innerText||'0').replace(/[^0-9.]/g,''))||0];
let configuredPanelW=Number(((document.getElementById('chartPlant')||{}).innerText||'120').replace(/[^0-9.]/g,''))||120;
function draw(){
 const c=document.getElementById('chart'),x=c.getContext('2d'),w=c.width,h=c.height;
 const scale=Math.max(1,Number(configuredPanelW)||120);
 x.clearRect(0,0,w,h); x.strokeStyle='#30363d'; x.lineWidth=1;
 for(let i=0;i<5;i++){let y=i*h/4; x.beginPath(); x.moveTo(0,y); x.lineTo(w,y); x.stroke();}
 x.strokeStyle='rgba(242,204,96,.32)'; x.setLineDash([5,5]);
 [0.5,1.0].forEach(fr=>{let y=h-(fr*h); x.beginPath(); x.moveTo(0,y); x.lineTo(w,y); x.stroke();});
 x.setLineDash([]);
 if(data.length<2)return;
 x.strokeStyle='#f2cc60'; x.lineWidth=3; x.beginPath();
 data.forEach((v,i)=>{let px=i*(w/(data.length-1)); let py=h-(Math.max(0,Math.min(scale,v))/scale*h); if(i==0)x.moveTo(px,py); else x.lineTo(px,py);});
 x.stroke();
 const maxLive=Math.max(0,...data), avg=data.reduce((a,b)=>a+(Number(b)||0),0)/Math.max(1,data.length);
 st('chartMax',maxLive.toFixed(0)+' W'); st('chartAvg',avg.toFixed(0)+' W'); st('chartSamples',data.length);
}
function st(id,v){const e=document.getElementById(id); if(e)e.innerText=v;}
function sw(id,p){const e=document.getElementById(id); if(e)e.style.width=Math.max(0,Math.min(100,p))+'%';}
async function tick(){
 try{
  const r=await fetch('/json?_='+Date.now(),{cache:'no-store'}); const j=await r.json();
  st('bv',Number(j.battery_voltage).toFixed(2));
  st('ba',Number(j.battery_current).toFixed(2));
  st('bw',Number(j.battery_power).toFixed(1));
  st('pw',Number(j.panel_power).toFixed(0));
  st('pv',Number(j.panel_voltage).toFixed(2));
  st('yt',Number(j.yield_today_kwh).toFixed(2));
  st('ytot',Number(j.yield_total_kwh).toFixed(2));
  st('cs',j.charge_state||'N/D');
  st('err',j.error||'0');
  st('espv',Number(j.esp_battery_voltage).toFixed(2));
  st('esppct',Number(j.esp_battery_percent).toFixed(0));
  st('espstat',j.esp_battery_status||'N/D');
  configuredPanelW=Math.max(1,Number(j.panel_configured_w)||configuredPanelW||120);
  const pvPct=Math.max(0,Math.min(100,(Number(j.panel_power)||0)/configuredPanelW*100));
  st('chartNow',Number(j.panel_power||0).toFixed(0)+' W'); st('chartPlant',configuredPanelW.toFixed(0)+' W'); st('chartScale','0-'+configuredPanelW.toFixed(0)+' W'); st('yTop',configuredPanelW.toFixed(0)+' W'); st('yMid',(configuredPanelW/2).toFixed(0)+' W');
  const batPct=Math.max(0,Math.min(100,((Number(j.battery_voltage)||0)-11.8)/(12.8-11.8)*100));
  sw('pvbar',pvPct); sw('batbar',batPct); sw('espbar',Number(j.esp_battery_percent)||0); sw('espicon',Number(j.esp_battery_percent)||0);
  st('badgeWifi',(Number(j.wifi_rssi)||0)==0?'WiFi':'WiFi '+j.wifi_rssi+' dBm');
  st('badgeVe',j.online?'VE.Direct Online':'VE.Direct Offline');
  st('badgeEsp','BAT ESP '+Number(j.esp_battery_percent).toFixed(0)+'%');
  st('badgeClock',new Date().toLocaleTimeString());
  data.push(Number(j.panel_power)||0); if(data.length>90)data.shift(); draw();
 }catch(e){}
}
const DASH_REFRESH_MS=6000; setInterval(tick,DASH_REFRESH_MS); tick();


function fmtDur(sec){sec=Math.max(0,Number(sec)||0);const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60);if(h>0)return h+'h '+String(m).padStart(2,'0')+'m';return m+'m';}
function popupText(x){
 const total=Number(x.charge_total_sec)||0;
 const wh=Number(x.wh)||0, mw=Number(x.maxw)||0, mpv=Number(x.maxpv)||0, bmin=Number(x.battmin)||0, bmax=Number(x.battmax)||0;
 if(wh<=0 && mw<=0 && mpv<=0 && bmin<=0 && bmax<=0 && total<=0){return '<div class="tipTitle">'+x.label+'</div><div class="noData">Nessun dato registrato per questo periodo.</div>';}
 let state='';
 if(total>0){state='<div class="tipGrid"><span>Bulk</span><b>'+fmtDur(x.bulk_sec)+'</b><span>Assorbimento</span><b>'+fmtDur(x.absorption_sec)+'</b><span>Float</span><b>'+fmtDur(x.float_sec)+'</b><span>Off</span><b>'+fmtDur(x.off_sec)+'</b></div>';}
 const batt=(bmin>0&&bmax>0)?(bmin.toFixed(2)+'-'+bmax.toFixed(2)+' V'):'N/D';
 return '<div class="tipTitle">'+x.label+'</div><div class="tipGrid"><span>Produzione</span><b>'+Math.round(wh)+' Wh</b><span>P max</span><b>'+Math.round(mw)+' W</b><span>PV max</span><b>'+mpv.toFixed(2)+' V</b><span>Batt.</span><b>'+batt+'</b></div>'+state;
}
function stateRows(x){
 const total=Math.max(1,Number(x.charge_total_sec)||0);
 const arr=[['Bulk',x.bulk_sec],['Assorbimento',x.absorption_sec],['Float',x.float_sec],['Spento',x.off_sec],['Altro',x.other_sec]];
 return arr.map(a=>'<div style="display:flex;align-items:center;gap:8px;margin:4px 0"><span style="width:92px">'+a[0]+'</span><div style="flex:1;height:8px;background:#1f2937;border-radius:8px;overflow:hidden"><div style="width:'+Math.round((Number(a[1]||0)/total)*100)+'%;height:8px;background:#58a6ff"></div></div><b>'+fmtDur(a[1])+'</b></div>').join('');
}
async function loadChrono(type){
 try{
  ['btnDaily','btnHourly','btnDaily31','btnMonthly'].forEach(id=>{const b=document.getElementById(id); if(b)b.style.opacity='.65';});
  const active = type==='hourly' ? 'btnHourly' : (type==='daily31' ? 'btnDaily31' : (type==='monthly' ? 'btnMonthly' : 'btnDaily'));
  const ab=document.getElementById(active); if(ab)ab.style.opacity='1';
  const apiType = (type==='daily31') ? 'daily' : type;
  const ctl=new AbortController(); const to=setTimeout(()=>ctl.abort(),8000);
  const r=await fetch('/history?type='+apiType+'&_='+Date.now(),{cache:'no-store',signal:ctl.signal});
  clearTimeout(to);
  if(!r.ok) throw new Error('HTTP '+r.status);
  const rows=await r.json();
  if(!Array.isArray(rows)) throw new Error('JSON storico non valido');
  const n = type==='daily' ? 7 : (type==='hourly' ? 12 : (type==='daily31' ? 31 : 12));
  const last = rows.slice(-n);
  const maxWh = Math.max(10,...last.map(x=>Number(x.wh)||0));
  let html="<div style='padding:14px'><div id='chronoTip' class='histTipBox'></div><div class='histChartArea'>";
  last.forEach((x,i)=>{
    let wh=Number(x.wh)||0; let h=Math.max(3,Math.round((wh/maxWh)*92));
    html+="<div style='flex:1;text-align:center;cursor:pointer' onclick='showChronoTip("+i+")'><div id='bar"+i+"' style='height:"+h+"px;background:#d7e8f5;border-radius:6px 6px 0 0'></div></div>";
  });
  html+="</div><div style='display:grid;grid-template-columns:repeat("+last.length+",1fr);gap:6px;margin-top:6px;font-size:11px;color:#dcefff;text-align:center'>";
  last.forEach(x=>html+="<div>"+String(x.label).replace(' giorni fa','g fa')+"</div>");
  html+="</div>";
  let cur=last[last.length-1]||{};
  html+="<div id='stateBox' style='margin-top:12px;background:rgba(0,0,0,.18);border-radius:14px;padding:10px'>"+stateRows(cur)+"</div>";
  html+="<div style='display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:12px'>";
  html+="<div><b>Produzione</b><br>"+Math.round(Number(cur.wh)||0)+" Wh</div>";
  html+="<div><b>P max</b><br>"+Math.round(Number(cur.maxw)||0)+" W</div>";
  html+="<div><b>V max</b><br>"+Number(cur.maxpv||0).toFixed(2)+" V</div>";
  html+="<div><b>Batt.</b><br>"+Number(cur.battmin||0).toFixed(2)+"-"+Number(cur.battmax||0).toFixed(2)+" V</div>";
  html+="</div><div style='margin-top:14px'><a class='button' href='/history-compact?type="+type+"'>Apri pagina storico</a></div></div>";
  document.getElementById('chronoBox').innerHTML=html;
  window.chronoRows=last; showChronoTip(last.length-1);
 }catch(e){const cb=document.getElementById('chronoBox'); if(cb) cb.innerHTML="<div class='bluehead'>Errore caricamento cronologia<br><small>"+(e&&e.message?e.message:'riprova')+"</small><br><button onclick=\"loadChrono('daily')\">Riprova</button></div>";}
}
function showChronoTip(i){
 const x=(window.chronoRows||[])[i]; if(!x)return;
 const tip=document.getElementById('chronoTip'); if(!tip)return;
 tip.innerHTML=popupText(x); tip.style.display='block';
 (window.chronoRows||[]).forEach((_,k)=>{const b=document.getElementById('bar'+k); if(b)b.style.background=k===i?'#f2cc60':'#d7e8f5';});
 const sb=document.getElementById('stateBox'); if(sb)sb.innerHTML=stateRows(x);
}
setTimeout(()=>loadChrono('daily'),250);

function escapeHtml(v){return String(v).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]||c));}
function badgeClassWifi(r){r=Number(r); if(!isFinite(r)||r===0)return 'bad'; if(r>-65)return 'ok'; if(r>-75)return 'warn'; if(r>-85)return 'weak'; return 'bad';}
function badgeClassPct(p){p=Number(p); if(!isFinite(p)||p<=0)return 'bad'; if(p>=90)return 'ok'; if(p>=50)return 'warn'; if(p>=20)return 'weak'; return 'bad';}
function setPillClass(id,cls){const e=document.getElementById(id); if(!e)return; e.className='gxPill '+cls;}
async function checkOtaNotice(){
 try{
  const r=await fetch('/ota-notify.json?_='+Date.now(),{cache:'no-store'}); const j=await r.json();
  if(!j || j.seen || !j.id || j.type==='none') return;
  const d=document.createElement('div'); d.className='otaModal';
  const level=(j.level||'info');
  d.innerHTML='<div class="otaModalBox '+level+'"><h2>'+escapeHtml(j.title||'Aggiornamento')+'</h2><p>'+escapeHtml(j.message||'')+'</p><div class="time">Ora evento: '+escapeHtml(j.time||'N/D')+'<br>Versione: '+escapeHtml(j.version||'N/D')+'</div><p style="margin-top:14px"><button class="button" id="otaCloseBtn">Chiudi</button></p></div>';
  document.body.appendChild(d);
  document.getElementById('otaCloseBtn').onclick=async()=>{try{await fetch('/ota-notify-clear',{cache:'no-store'});}catch(e){} d.remove();};
 }catch(e){}
}
checkOtaNotice();




</script>
)rawliteral";

  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}



void handleHistoryPage() {
  if (!requireAuth()) return;

  String type = server.hasArg("type") ? server.arg("type") : "daily";
  String title = "7 giorni";
  int count = 7;
  int sourceCount = 31;
  HistorySlot *arr = daily;
  int current = currentDayIndex < 0 ? 0 : currentDayIndex;
  const char* prefix = "G";

  if (type == "hourly") {
    title = "24 ore";
    count = 24;
    sourceCount = 24;
    arr = hourly;
    current = currentHourIndex < 0 ? 0 : currentHourIndex;
    prefix = "H";
  } else if (type == "monthly") {
    title = "12 mesi";
    count = 12;
    sourceCount = 12;
    arr = monthly;
    current = currentMonthIndex < 0 ? 0 : currentMonthIndex;
    prefix = "M";
  } else if (type == "daily31") {
    title = "31 giorni";
    count = 31;
    sourceCount = 31;
    arr = daily;
    current = currentDayIndex < 0 ? 0 : currentDayIndex;
    prefix = "G";
  }

  String rows = historyJsonArray(arr, sourceCount, current, prefix);

  String html = htmlHeader("Storico");
  html += "<h1>Storico compatto</h1><p><a href='/'>Dashboard</a> &middot; <a href='/history-gx'>Storico GX</a> &middot; <a href='/api/history?type=" + type + "'>JSON</a></p>";
  html += "<div class='tabs'>";
  html += "<a class='button' href='/history-page?type=hourly'>24h</a> ";
  html += "<a class='button' href='/history-page?type=daily'>7g</a> ";
  html += "<a class='button' href='/history-page?type=daily31'>31g</a> ";
  html += "<a class='button' href='/history-page?type=monthly'>12m</a>";
  html += "</div>";
  html += "<div class='card'><div class='t'>" + title + "</div><div id='histCompact'></div></div>";
  html += "<script>const allRows=" + rows + "; const viewCount=" + String(count) + ";</script>";
  html += R"rawliteral(
<script>
function fmtDur(sec){sec=Math.max(0,Number(sec)||0);const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60);if(h>0)return h+'h '+String(m).padStart(2,'0')+'m';return m+'m';}
function popupText(x){const total=Number(x.charge_total_sec)||0;const wh=Number(x.wh)||0,mw=Number(x.maxw)||0,mpv=Number(x.maxpv)||0,bmin=Number(x.battmin)||0,bmax=Number(x.battmax)||0;if(wh<=0&&mw<=0&&mpv<=0&&bmin<=0&&bmax<=0&&total<=0)return '<div class="tipTitle">'+x.label+'</div><div class="noData">Nessun dato registrato per questo periodo.</div>';let st='';if(total>0)st='<div class="tipGrid"><span>Bulk</span><b>'+fmtDur(x.bulk_sec)+'</b><span>Assorbimento</span><b>'+fmtDur(x.absorption_sec)+'</b><span>Float</span><b>'+fmtDur(x.float_sec)+'</b><span>Off</span><b>'+fmtDur(x.off_sec)+'</b></div>';const batt=(bmin>0&&bmax>0)?(bmin.toFixed(2)+'-'+bmax.toFixed(2)+' V'):'N/D';return '<div class="tipTitle">'+x.label+'</div><div class="tipGrid"><span>Produzione</span><b>'+Math.round(wh)+' Wh</b><span>P max</span><b>'+Math.round(mw)+' W</b><span>PV max</span><b>'+mpv.toFixed(2)+' V</b><span>Batt.</span><b>'+batt+'</b></div>'+st;}
function stateRows(x){const total=Math.max(1,Number(x.charge_total_sec)||0);const arr=[['Bulk',x.bulk_sec],['Assorbimento',x.absorption_sec],['Float',x.float_sec],['Spento',x.off_sec],['Altro',x.other_sec]];return arr.map(a=>'<div style="display:flex;align-items:center;gap:8px;margin:5px 0"><span style="width:110px">'+a[0]+'</span><div style="flex:1;height:8px;background:#1f2937;border-radius:8px;overflow:hidden"><div style="width:'+Math.round((Number(a[1]||0)/total)*100)+'%;height:8px;background:#58a6ff"></div></div><b>'+fmtDur(a[1])+'</b></div>').join('');}
const data=allRows.slice(-viewCount);const max=Math.max(10,...data.map(x=>Number(x.wh)||0));let html="<div><div id='histTip' class='histTipBox'></div><div style='display:flex;align-items:end;gap:6px;height:150px;border-bottom:1px solid #30363d;margin-top:15px;padding-top:8px'>";
data.forEach((x,i)=>{let h=Math.max(3,Math.round(((Number(x.wh)||0)/max)*98));html+="<div style='flex:1;text-align:center;cursor:pointer' onclick='showHistTip("+i+")'><div id='hbar"+i+"' style='height:"+h+"px;background:#79c0ff;border-radius:7px 7px 0 0'></div></div>";});
html+="</div><div style='display:grid;grid-template-columns:repeat("+data.length+",1fr);gap:5px;font-size:11px;color:#8b949e;text-align:center;margin-top:6px'>";data.forEach(x=>html+="<div>"+String(x.label).replace(' giorni fa','g')+"</div>");html+="</div><div id='stateHist' class='card' style='margin-top:14px'></div><div id='summaryHist' style='display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-top:12px'></div></div>";document.getElementById('histCompact').innerHTML=html;
function showHistTip(i){const x=data[i];if(!x)return;document.getElementById('histTip').innerHTML=popupText(x);document.getElementById('histTip').style.display='block';data.forEach((_,k)=>{const b=document.getElementById('hbar'+k);if(b)b.style.background=k===i?'#f2cc60':'#79c0ff';});document.getElementById('stateHist').innerHTML='<div class="t">Tempo stati carica</div>'+stateRows(x);document.getElementById('summaryHist').innerHTML="<div class='card'><div class='t'>Produzione</div><div class='v' style='font-size:24px'>"+Math.round(x.wh||0)+" Wh</div></div><div class='card'><div class='t'>P max</div><div class='v' style='font-size:24px'>"+Math.round(x.maxw||0)+" W</div></div><div class='card'><div class='t'>PV max</div><div class='v' style='font-size:22px'>"+Number(x.maxpv||0).toFixed(2)+" V</div></div><div class='card'><div class='t'>Batt min/max</div><div class='v' style='font-size:20px'>"+Number(x.battmin||0).toFixed(2)+"-"+Number(x.battmax||0).toFixed(2)+" V</div></div>";}
showHistTip(data.length-1);
</script>
)rawliteral";
  html += "</body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}



void handleHistoryCompact() {
  if (!requireAuth()) return;

  String type = server.hasArg("type") ? server.arg("type") : "daily";
  HistorySlot *arr = daily;
  int count = 7;
  int sourceCount = 31;
  int current = currentDayIndex < 0 ? 0 : currentDayIndex;
  const char* prefix = "G";
  String title = "Ultimi 7 giorni";

  if (type == "hourly") {
    arr = hourly; count = 12; sourceCount = 24; current = currentHourIndex < 0 ? 0 : currentHourIndex; prefix = "H"; title = "Ultime 12 ore";
  } else if (type == "daily31") {
    arr = daily; count = 31; sourceCount = 31; current = currentDayIndex < 0 ? 0 : currentDayIndex; prefix = "G"; title = "Ultimi 31 giorni";
  } else if (type == "monthly") {
    arr = monthly; count = 12; sourceCount = 12; current = currentMonthIndex < 0 ? 0 : currentMonthIndex; prefix = "M"; title = "Ultimi 12 mesi";
  }

  String raw = historyJsonArray(arr, sourceCount, current, prefix);

  String html = htmlHeader("Storico compatto");
  html += "<h1>Storico compatto</h1>";
  html += "<p><a href='/'>Dashboard</a> &middot; <a href='/history-gx'>Storico GX</a> &middot; <a href='/api/history?type=" + type + "'>JSON</a></p>";
  html += "<div class='card'>";
  html += "<div class='tabs'>";
  html += "<a class='button' href='/history-compact?type=hourly'>12h</a>";
  html += "<a class='button' href='/history-compact?type=daily'>7g</a>";
  html += "<a class='button' href='/history-compact?type=daily31'>31g</a>";
  html += "<a class='button' href='/history-compact?type=monthly'>12m</a>";
  html += "</div>";
  html += "<div class='t'>" + title + "</div>";
  html += "<div id='hc'></div>";
  html += "</div>";

  html += "<script>const allRows=" + raw + "; const rows=allRows.slice(-" + String(count) + ");</script>";
  html += R"rawliteral(
<script>
function fmtDur(sec){sec=Math.max(0,Number(sec)||0);const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60);if(h>0)return h+'h '+String(m).padStart(2,'0')+'m';return m+'m';}
function popupText(x){const total=Number(x.charge_total_sec)||0;const wh=Number(x.wh)||0,mw=Number(x.maxw)||0,mpv=Number(x.maxpv)||0,bmin=Number(x.battmin)||0,bmax=Number(x.battmax)||0;if(wh<=0&&mw<=0&&mpv<=0&&bmin<=0&&bmax<=0&&total<=0)return '<div class="tipTitle">'+x.label+'</div><div class="noData">Nessun dato registrato per questo periodo.</div>';let st='';if(total>0)st='<div class="tipGrid"><span>Bulk</span><b>'+fmtDur(x.bulk_sec)+'</b><span>Assorbimento</span><b>'+fmtDur(x.absorption_sec)+'</b><span>Float</span><b>'+fmtDur(x.float_sec)+'</b><span>Off</span><b>'+fmtDur(x.off_sec)+'</b></div>';const batt=(bmin>0&&bmax>0)?(bmin.toFixed(2)+'-'+bmax.toFixed(2)+' V'):'N/D';return '<div class="tipTitle">'+x.label+'</div><div class="tipGrid"><span>Produzione</span><b>'+Math.round(wh)+' Wh</b><span>P max</span><b>'+Math.round(mw)+' W</b><span>PV max</span><b>'+mpv.toFixed(2)+' V</b><span>Batt.</span><b>'+batt+'</b></div>'+st;}
function stateRows(x){const total=Math.max(1,Number(x.charge_total_sec)||0);const arr=[['Bulk',x.bulk_sec],['Assorbimento',x.absorption_sec],['Float',x.float_sec],['Spento',x.off_sec],['Altro',x.other_sec]];return arr.map(a=>'<div style="display:flex;align-items:center;gap:8px;margin:5px 0"><span style="width:110px">'+a[0]+'</span><div style="flex:1;height:8px;background:#1f2937;border-radius:8px;overflow:hidden"><div style="width:'+Math.round((Number(a[1]||0)/total)*100)+'%;height:8px;background:#58a6ff"></div></div><b>'+fmtDur(a[1])+'</b></div>').join('');}
const max=Math.max(10,...rows.map(x=>Number(x.wh)||0));let html="<div><div id='tip' class='histTipBox'></div><div style='height:155px;display:flex;gap:6px;align-items:end;border-bottom:1px solid #30363d;margin-top:14px;padding-top:8px'>";
rows.forEach((x,i)=>{const h=Math.max(3,Math.round(((Number(x.wh)||0)/max)*100));html+="<div style='flex:1;text-align:center;cursor:pointer' onclick='showTip("+i+")'><div id='cbar"+i+"' style='height:"+h+"px;background:#58a6ff;border-radius:8px 8px 0 0'></div></div>";});
html+="</div><div style='display:grid;grid-template-columns:repeat("+rows.length+",1fr);gap:4px;font-size:10px;color:#8b949e;text-align:center;margin-top:6px'>";rows.forEach(x=>html+="<div>"+String(x.label).replace(' giorni fa','g')+"</div>");html+="</div><div id='chargeBox' class='card' style='margin-top:14px'></div><div id='quickBox' style='display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-top:12px'></div></div>";document.getElementById('hc').innerHTML=html;
function showTip(i){const x=rows[i];if(!x)return;document.getElementById('tip').innerHTML=popupText(x);document.getElementById('tip').style.display='block';rows.forEach((_,k)=>{const b=document.getElementById('cbar'+k);if(b)b.style.background=k===i?'#f2cc60':'#58a6ff';});document.getElementById('chargeBox').innerHTML='<div class="t">Tempo stati carica</div>'+stateRows(x);document.getElementById('quickBox').innerHTML="<div class='card'><div class='t'>Produzione</div><div class='v' style='font-size:25px'>"+Math.round(x.wh||0)+" Wh</div></div><div class='card'><div class='t'>P max</div><div class='v' style='font-size:25px'>"+Math.round(x.maxw||0)+" W</div></div><div class='card'><div class='t'>PV max</div><div class='v' style='font-size:25px'>"+Number(x.maxpv||0).toFixed(2)+" V</div></div><div class='card'><div class='t'>Batt min/max</div><div class='v' style='font-size:20px'>"+Number(x.battmin||0).toFixed(2)+"-"+Number(x.battmax||0).toFixed(2)+" V</div></div>";}
showTip(rows.length-1);
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void handleHistory() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  String type = server.hasArg("type") ? server.arg("type") : "hourly";

  if (type == "daily") {
    sendJsonPretty(historyJsonArray(daily, 31, currentDayIndex < 0 ? 0 : currentDayIndex, "G"));
  } else if (type == "monthly") {
    sendJsonPretty(historyJsonArray(monthly, 12, currentMonthIndex < 0 ? 0 : currentMonthIndex, "M"));
  } else {
    sendJsonPretty(historyJsonArray(hourly, 24, currentHourIndex < 0 ? 0 : currentHourIndex, "H"));
  }
}

void handleChargeHistory() {
  // Stesso JSON di /history, con campi durata stati carica gia' integrati: bulk_sec, absorption_sec, float_sec, off_sec.
  handleHistory();
}


void handleJson() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  String json = "{";
  json += "\"firmware_name\":\"" + String(FW_NAME) + "\",";
  json += "\"firmware_version\":\"" + String(FW_VERSION) + "\",";
  json += "\"firmware_build\":\"" + buildText() + "\",";
  json += "\"battery_voltage\":" + String(isnan(battV) ? 0 : battV, 3) + ",";
  json += "\"battery_current\":" + String(isnan(battA) ? 0 : battA, 3) + ",";
  json += "\"battery_power\":" + String(isnan(battW) ? 0 : battW, 2) + ",";
  json += "\"panel_voltage\":" + String(isnan(panelV) ? 0 : panelV, 3) + ",";
  json += "\"panel_power\":" + String(isnan(panelW) ? 0 : panelW, 1) + ",";
  json += "\"panel_configured_w\":" + String(configuredPanelWatts(), 0) + ",";
  json += "\"yield_today_kwh\":" + String(isnan(yieldTodayKWh) ? 0 : yieldTodayKWh, 3) + ",";
  json += "\"yield_total_kwh\":" + String(isnan(yieldTotalKWh) ? 0 : yieldTotalKWh, 3) + ",";
  json += "\"charge_state\":\"" + chargeState + "\",";
  json += "\"mppt_state\":\"" + mpptState + "\",";
  json += "\"error\":\"" + errorState + "\",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"esp_battery_voltage\":" + String(isnan(espBatteryVoltage()) ? 0 : espBatteryVoltage(), 3) + ",";
  json += "\"esp_battery_percent\":" + String(isnan(espBatteryPercent()) ? 0 : espBatteryPercent(), 0) + ",";
  json += "\"esp_battery_status\":\"" + espBatteryStatusText() + "\",";
  json += "\"esp_battery_connected\":\"" + espBatteryConnectionText() + "\",";
  json += "\"alert_count\":" + String(alertCountNow()) + ",";
  json += "\"online\":" + String(victronOnline() ? "true" : "false");
  json += "}";
  sendJsonPretty(json);
}


float adcPinVoltage(int pin) {
  // Lettura media ADC ESP32. Non modifica pin usati dal TFT.
  // La tensione restituita è quella vista dal pin ADC, NON necessariamente la tensione reale LiPo.
  const int samples = 16;
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  float raw = sum / (float)samples;
  return (raw / 4095.0f) * 3.30f;
}

float lipoPercentFromVoltage(float v) {
  if (isnan(v) || v <= 0) return NAN;
  if (v >= 4.20f) return 100.0f;
  if (v >= 4.10f) return 90.0f + (v - 4.10f) * 100.0f;
  if (v >= 4.00f) return 80.0f + (v - 4.00f) * 100.0f;
  if (v >= 3.90f) return 65.0f + (v - 3.90f) * 150.0f;
  if (v >= 3.80f) return 50.0f + (v - 3.80f) * 150.0f;
  if (v >= 3.70f) return 35.0f + (v - 3.70f) * 150.0f;
  if (v >= 3.60f) return 20.0f + (v - 3.60f) * 150.0f;
  if (v >= 3.45f) return 5.0f + (v - 3.45f) * 100.0f;
  if (v >= 3.30f) return (v - 3.30f) * 33.3f;
  return 0.0f;
}


void loadEspBatteryCalibration() {
  if (espBatMultiplierLoaded) return;
  prefs.begin("victron", true);
  espBatMultiplier = prefs.getFloat("esp_bat_mult", pubCfg.espBatteryMultiplier);
  prefs.end();
  if (espBatMultiplier < 0.10f || espBatMultiplier > 10.00f) espBatMultiplier = pubCfg.espBatteryMultiplier;
  espBatMultiplierLoaded = true;
}

float espBatteryMultiplier() {
  loadEspBatteryCalibration();
  return espBatMultiplier;
}

void saveEspBatteryMultiplier(float m) {
  if (m < 1.20f || m > 3.20f) return;
  prefs.begin("victron", false);
  prefs.putFloat("esp_bat_mult", m);
  prefs.end();
  espBatMultiplier = m;
  espBatMultiplierLoaded = true;
}

float espBatteryVoltage() {
  // CYD: dallo scan ADC il candidato e' GPIO34. Moltiplicatore calibrazione salvabile da /battery-cal.
  int adcPin = pubCfg.espBatteryAdcPin;
  if (adcPin < 0) return NAN;
  float pinV = adcPinVoltage(adcPin);
  float v = pinV * espBatteryMultiplier();
  if (v < 2.0f || v > 4.6f) return NAN;
  return v;
}

float espBatteryPercent() {
  return lipoPercentFromVoltage(espBatteryVoltage());
}

String espBatteryStatusText() {
  float v = espBatteryVoltage();
  if (isnan(v)) return "N/D";
  if (v >= 4.12f) return "Piena";
  if (v >= 3.85f) return "OK";
  if (v >= 3.60f) return "Bassa";
  return "Critica";
}

String espBatteryConnectionText() {
  // GPIO34 misura il nodo BAT, ma senza un pin CHG/STAT dedicato non puo' distinguere
  // con certezza assoluta tra LiPo collegata e caricatore a vuoto.
  prefs.begin("victron", true);
  bool installed = prefs.getBool("esp_bat_installed", false);
  prefs.end();
  if (installed) return "LiPo collegata";
  float v = espBatteryVoltage();
  if (isnan(v)) return "Non rilevata";
  return "Non confermata";
}

String batteryScanJson() {
  const int pins[] = {32, 33, 34, 35, 36, 39};
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"note\":\"pin_voltage is ADC pin voltage. lipo_x2 assumes a 1:1 divider. Find the pin closest to BAT voltage/2.\",";
  j += "\"usb_or_5v_present\":" + String(backlightOn ? "true" : "true") + ",";
  j += "\"pins\":[";
  for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
    int pin = pins[i];
    uint32_t sumRaw = 0;
    for (int s = 0; s < 16; s++) { sumRaw += analogRead(pin); delay(1); }
    float raw = sumRaw / 16.0f;
    float pinV = (raw / 4095.0f) * 3.30f;
    float lipo2 = pinV * 2.0f;
    float pct2 = lipoPercentFromVoltage(lipo2);
    if (i) j += ",";
    j += "{";
    j += "\"gpio\":" + String(pin) + ",";
    j += "\"raw\":" + String(raw, 0) + ",";
    j += "\"pin_voltage\":" + String(pinV, 3) + ",";
    j += "\"lipo_x2_voltage\":" + String(lipo2, 3) + ",";
    j += "\"lipo_x2_percent\":" + String(isnan(pct2) ? 0 : pct2, 0);
    j += "}";
  }
  j += "]}";
  return j;
}


String formatBytes64(uint64_t bytes) {
  double b = (double)bytes;
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) return String(b / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
  if (bytes >= 1024ULL * 1024ULL) return String(b / (1024.0 * 1024.0), 1) + " MB";
  if (bytes >= 1024ULL) return String(b / 1024.0, 1) + " KB";
  return String((unsigned long)bytes) + " B";
}

String sdTypeText() {
  if (!sdMounted) return "N/D";
  uint8_t type = SD.cardType();
  if (type == CARD_NONE) return "Nessuna";
  if (type == CARD_MMC) return "MMC";
  if (type == CARD_SD) return "SDSC";
  if (type == CARD_SDHC) return "SDHC/SDXC";
  return "Sconosciuta";
}

bool sdMount(bool force) {
  sdEverTried = true;
  if (sdMounted && !force) return true;
  if (sdMounted && force) sdUnmount();

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(30);

  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 4000000, "/sd", 5)) {
    sdMounted = false;
    sdLastStatus = "Mount fallito: controlla inserimento, formato FAT32 e pin SD";
    sdLastActionMs = millis();
    addEventLog("SD", sdLastStatus);
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    SD.end();
    sdMounted = false;
    sdLastStatus = "Nessuna microSD rilevata";
    sdLastActionMs = millis();
    addEventLog("SD", sdLastStatus);
    return false;
  }

  sdMounted = true;
  sdLastStatus = "MicroSD montata: " + sdTypeText() + ", " + formatBytes64(SD.usedBytes()) + " / " + formatBytes64(SD.totalBytes()) + " usati";
  sdLastActionMs = millis();
  addEventLog("SD", sdLastStatus);
  return true;
}

void sdUnmount() {
  if (sdMounted) SD.end();
  sdMounted = false;
  sdLastStatus = "MicroSD smontata";
  sdLastActionMs = millis();
  addEventLog("SD", sdLastStatus);
}

String sdInfoJson() {
  String j = "{";
  j += "\"mounted\":" + String(sdMounted ? "true" : "false") + ",";
  j += "\"ever_tried\":" + String(sdEverTried ? "true" : "false") + ",";
  j += "\"status\":\"" + esc(sdLastStatus) + "\",";
  j += "\"cs\":" + String(SD_CS) + ",";
  j += "\"sck\":" + String(SD_SCLK) + ",";
  j += "\"miso\":" + String(SD_MISO) + ",";
  j += "\"mosi\":" + String(SD_MOSI) + ",";
  if (sdMounted) {
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    uint64_t freeB = (total > used) ? (total - used) : 0;
    float pct = total ? ((float)used * 100.0f / (float)total) : 0;
    j += "\"type\":\"" + sdTypeText() + "\",";
    j += "\"total\":" + String((double)total, 0) + ",";
    j += "\"used\":" + String((double)used, 0) + ",";
    j += "\"free\":" + String((double)freeB, 0) + ",";
    j += "\"used_percent\":" + String(pct, 1) + ",";
    j += "\"total_text\":\"" + formatBytes64(total) + "\",";
    j += "\"used_text\":\"" + formatBytes64(used) + "\",";
    j += "\"free_text\":\"" + formatBytes64(freeB) + "\"";
  } else {
    j += "\"type\":\"N/D\",\"total\":0,\"used\":0,\"free\":0,\"used_percent\":0,\"total_text\":\"0 B\",\"used_text\":\"0 B\",\"free_text\":\"0 B\"";
  }
  j += "}";
  return j;
}

void handleSdJson() {
  if (!requireAuth()) return;
  sendJsonPretty(sdInfoJson());
}

void handleSdMount() {
  if (!requireAuth()) return;
  bool ok = sdMount(true);
  sendActionPage("MicroSD", ok ? "MicroSD montata correttamente." : sdLastStatus, 2, "/sd");
}

void handleSdUnmount() {
  if (!requireAuth()) return;
  sdUnmount();
  sendActionPage("MicroSD", "MicroSD smontata. Ora puoi rimuoverla in sicurezza.", 2, "/sd");
}

bool sdWipeRecursive(const String& path, uint32_t &files, uint32_t &dirs) {
  if (!sdMounted) return false;
  File dir = SD.open(path);
  if (!dir) return false;

  if (!dir.isDirectory()) {
    dir.close();
    bool ok = SD.remove(path);
    if (ok) files++;
    return ok;
  }

  File entry = dir.openNextFile();
  while (entry) {
    String name = String(entry.name());
    bool isDir = entry.isDirectory();
    entry.close();

    if (name.length() > 0 && name != "/" && name != path) {
      if (isDir) {
        sdWipeRecursive(name, files, dirs);
        if (SD.rmdir(name)) dirs++;
      } else {
        if (SD.remove(name)) files++;
      }
    }
    entry = dir.openNextFile();
    yield();
  }
  dir.close();
  return true;
}

void handleSdFormat() {
  if (!requireAuth()) return;

  if (!server.hasArg("confirm") || server.arg("confirm") != "YES") {
    String html = htmlHeader("Formatta MicroSD");
    html += "<h1>Formatta MicroSD</h1><p><a href='/sd'>Torna a MicroSD</a> &middot; <a href='/'>Dashboard</a></p>";
    html += "<div class='card warn'><div class='t'>Attenzione</div><div class='v'>Cancella contenuto SD</div>";
    html += "<p>Questa funzione elimina file e cartelle dalla microSD. Non formatta a basso livello la scheda, ma la svuota come una formattazione rapida per il datalogger.</p>";
    html += "<p><b>Prima dello svuotamento il firmware prova a salvare automaticamente la configurazione essenziale su LittleFS e su SD.</b></p>";
    html += "<div class='batGauge'><div id='fmtbar' class='batFill' style='width:0%'></div></div>";
    html += "<p><a class='button danger' href='/sd-format?confirm=YES' onclick=\"document.getElementById('fmtbar').style.width='100%';this.innerHTML='Formattazione...';\">Conferma formatta SD</a> ";
    html += "<a class='button' href='/sd'>Annulla</a></p></div>";
    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }

  if (!sdMounted && !sdMount(false)) {
    sendActionPage("Formatta MicroSD", "SD non montata: " + sdLastStatus, 3, "/sd");
    return;
  }

  // Backup essenziale prima dello svuotamento SD: non è un clone completo, ma salva configurazione critica anche in LittleFS.
  if (littleFsReady) {
    File lf = LittleFS.open("/pre_format_settings.json", "w");
    if (lf) { lf.print(settingsBackupJson()); lf.close(); }
  }
  if (sdMounted) {
    sdEnsureDir("/config");
    File sf = SD.open("/config/pre_format_settings.json", FILE_WRITE);
    if (sf) { sf.print(settingsBackupJson()); sf.close(); }
  }

  uint32_t files = 0, dirs = 0;
  sdLastStatus = "Formattazione rapida in corso...";
  sdLastActionMs = millis();
  addEventLog("SD", "Format rapido avviato");

  bool ok = sdWipeRecursive("/", files, dirs);

  sdLastStatus = ok ? ("Format rapido completato: eliminati " + String(files) + " file e " + String(dirs) + " cartelle") : "Format rapido non completato";
  sdLastActionMs = millis();
  addEventLog("SD", sdLastStatus);

  String html = htmlHeader("Formatta MicroSD");
  html += "<meta http-equiv='refresh' content='4;url=/sd'>";
  html += "<h1>Formatta MicroSD</h1>";
  html += "<div class='batHero'><div class='t'>Operazione completata</div><div class='batValue' style='font-size:34px'>100%</div>";
  html += "<div class='e'>" + esc(sdLastStatus) + "</div>";
  html += "<div class='batGauge'><div class='batFill' style='width:100%'></div></div>";
  html += "<div class='batGrid'><div class='batMini'><span>File</span><b>" + String(files) + "</b></div><div class='batMini'><span>Cartelle</span><b>" + String(dirs) + "</b></div><div class='batMini'><span>Redirect</span><b>4s</b></div></div>";
  html += "<p><a class='button' href='/sd'>Torna a MicroSD</a></p></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSdPage() {
  if (!requireAuth()) return;
  if (server.hasArg("mount")) { handleSdMount(); return; }
  if (server.hasArg("unmount")) { handleSdUnmount(); return; }

  String html = htmlHeader("MicroSD");
  html += "<h1>MicroSD</h1><p><a href='/'>Dashboard</a> &middot; <a href='/system-pro'>Sistema</a> &middot; <a href='/sd.json'>JSON SD</a></p>";

  if (sdMounted) {
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    uint64_t freeB = (total > used) ? (total - used) : 0;
    int pct = total ? (int)constrain(((float)used * 100.0f / (float)total), 0.0f, 100.0f) : 0;
    html += "<div class='batHero'><div class='t'>Scheda microSD montata</div>";
    html += "<div class='batValue' style='font-size:44px'>" + formatBytes64(freeB) + "</div>";
    html += "<div class='e'>Spazio libero su " + formatBytes64(total) + " totali &middot; Tipo: " + sdTypeText() + "</div>";
    html += "<div class='batGauge'><div class='batFill' style='width:" + String(pct) + "%'></div></div>";
    html += "<div class='batGrid'><div class='batMini'><span>Usata</span><b>" + formatBytes64(used) + "</b></div><div class='batMini'><span>Libera</span><b>" + formatBytes64(freeB) + "</b></div><div class='batMini'><span>Occupata</span><b>" + String(pct) + "%</b></div></div>";
    html += "<p><a class='button' href='/sd?unmount=1'>Smonta SD</a> <a class='button' href='/sd?mount=1'>Rimonta</a> <a class='button danger' href='/sd-format'>Formatta SD</a> <a class='button' href='/sd-files'>Apri file SD</a> <a class='button' href='/sd.json'>JSON</a></p></div>";
  } else {
    html += "<div class='card warn'><div class='t'>Scheda microSD non montata</div><div class='v'>Smontata</div><div class='e'>" + esc(sdLastStatus) + "</div>";
    html += "<p><a class='button' href='/sd?mount=1'>Monta SD</a> <a class='button danger' href='/sd-format'>Formatta SD</a> <a class='button' href='/sd-files'>Apri file SD</a> <a class='button' href='/sd.json'>JSON</a></p></div>";
  }

  html += "<div class='grid'>";
  html += card("Pin SD", "CS " + String(SD_CS), "SCK " + String(SD_SCLK) + "<br>MISO " + String(SD_MISO) + "<br>MOSI " + String(SD_MOSI));
  html += card("Stato", sdMounted ? "Montata" : "Smontata", esc(sdLastStatus));
  html += card("Logger", storageUseSd() ? "Attivo" : "Non attivo", "Modalita: " + storageMode() + "<br>Ultimo: " + esc(lastSdLogStatus) + "<br><a class='button' href='/storage'>Storage</a>");
  html += "</div>";
  html += "<div class='card'><div class='t'>Nota</div><p>Usa microSD formattata FAT32. Prima di sfilarla premi sempre <b>Smonta SD</b>.</p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}




String normalizeSdPath(String p) {
  p.trim();
  p.replace("\\", "/");
  if (p.length() == 0) p = "/";
  if (p.startsWith("/sd/")) p = p.substring(3);
  if (p == "/sd") p = "/";
  if (!p.startsWith("/")) p = "/" + p;
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

bool safeSdBrowserPath(String p) {
  p = normalizeSdPath(p);
  if (p.length() == 0) return false;
  if (!p.startsWith("/")) return false;
  if (p.indexOf("..") >= 0) return false;
  if (p.indexOf("\\") >= 0) return false;
  if (p.indexOf("%00") >= 0) return false;
  return true;
}

String fileNameOnly(String path) {
  path = normalizeSdPath(path);
  int i = path.lastIndexOf('/');
  if (i < 0) return path;
  String n = path.substring(i + 1);
  if (n.length() == 0) return "sd-root";
  return n;
}

String fileMimeFromPath(const String& path) {
  String p = path; p.toLowerCase();
  if (p.endsWith(".html") || p.endsWith(".htm")) return "text/html";
  if (p.endsWith(".csv")) return "text/csv";
  if (p.endsWith(".json")) return "application/json";
  if (p.endsWith(".txt") || p.endsWith(".log")) return "text/plain";
  if (p.endsWith(".bin")) return "application/octet-stream";
  if (p.endsWith(".gz")) return "application/gzip";
  return "application/octet-stream";
}

bool isReadableTextFile(String path) {
  String p = path; p.toLowerCase();
  return p.endsWith(".txt") || p.endsWith(".log") || p.endsWith(".csv") || p.endsWith(".json") || p.endsWith(".md") || p.endsWith(".ini") || p.endsWith(".cfg");
}

String sdParentPath(String p) {
  p = normalizeSdPath(p);
  if (p == "/") return "/";
  int i = p.lastIndexOf('/');
  if (i <= 0) return "/";
  return p.substring(0, i);
}

String sdFileBrowserLinks(const String& path, bool isDir) {
  String p = normalizeSdPath(path);
  String q = esc(p);
  String s;
  if (isDir) {
    s += "<a class='button' href='/sd-files?p=" + q + "'>Apri</a>";
  } else {
    if (isReadableTextFile(p)) s += "<a class='button' href='/sd-view?p=" + q + "'>Apri</a>";
    s += " <a class='button' href='/sd-download?p=" + q + "'>Scarica</a>";
  }
  if (p != "/") s += " <a class='button danger' href='/sd-delete?p=" + q + "'>Elimina</a>";
  return s;
}

void handleFilesHubPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("File & Log");
  html += "<h1>File & Log</h1><p><a href='/'>Dashboard</a> &middot; <a href='/sd'>MicroSD</a> &middot; <a href='/storage'>Storage</a> &middot; <a href='/logs'>Log eventi</a></p>";
  html += "<div class='gxSection'><div class='gxSectionTitle'>MicroSD <span>file, log e backup leggibili</span></div><div class='gxActions'>";
  html += "<a class='gxAction good' href='/sd-files'><b>File SD</b><span>Sfoglia tutta la microSD</span></a>";
  html += "<a class='gxAction' href='/sd-files?p=/logs'><b>Log CSV</b><span>Cartella /logs</span></a>";
  html += "<a class='gxAction' href='/sd-files?p=/backup_recovery'><b>Recovery</b><span>Backup completi</span></a>";
  html += "<a class='gxAction' href='/sd-files?p=/backup'><b>Backup config</b><span>File JSON impostazioni</span></a>";
  html += "</div></div>";
  html += "<div class='gxSection'><div class='gxSectionTitle'>Log e dati rapidi <span>download e diagnostica</span></div><div class='gxActions'>";
  html += "<a class='gxAction' href='/logs'><b>Log eventi</b><span>Eventi firmware</span></a>";
  html += "<a class='gxAction' href='/logs.json'><b>Log JSON</b><span>Eventi in JSON</span></a>";
  html += "<a class='gxAction' href='/history.csv'><b>History CSV</b><span>Storico interno</span></a>";
  html += "<a class='gxAction' href='/sd-log.csv'><b>CSV oggi</b><span>Log SD del giorno</span></a>";
  html += "</div></div>";
  html += "<div class='gxSection'><div class='gxSectionTitle'>JSON diagnostici <span>file leggibili da browser</span></div><div class='gxActions'>";
  html += "<a class='gxAction' href='/json'><b>Live JSON</b><span>Dati live</span></a>";
  html += "<a class='gxAction' href='/battery.json'><b>BAT JSON</b><span>Batteria ESP</span></a>";
  html += "<a class='gxAction' href='/alerts.json'><b>Alert JSON</b><span>Allarmi</span></a>";
  html += "<a class='gxAction' href='/sd.json'><b>SD JSON</b><span>Stato microSD</span></a>";
  html += "</div></div>";
  html += "<div class='card'><div class='t'>Sicurezza</div><div class='e'>Puoi aprire/scaricare file liberamente. L'eliminazione richiede sempre conferma. La formattazione resta solo nella pagina MicroSD.</div></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSdFilesPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String p = normalizeSdPath(server.hasArg("p") ? server.arg("p") : "/");
  if (!safeSdBrowserPath(p)) { sendActionPage("File SD", "Percorso non valido.", 2, "/files"); return; }
  String html = htmlHeader("File microSD");
  html += "<h1>File microSD</h1><p><a href='/'>Dashboard</a> &middot; <a href='/files'>File & Log</a> &middot; <a href='/sd'>MicroSD</a> &middot; <a href='/storage'>Storage</a></p>";
  if (!sdMounted) {
    html += "<div class='card warn'><div class='t'>SD non montata</div><div class='v'>Smontata</div><div class='e'>" + esc(sdLastStatus) + "</div><p><a class='button' href='/sd?mount=1'>Monta SD</a></p></div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }
  File dir = SD.open(p);
  if (!dir || !dir.isDirectory()) {
    html += "<div class='card warn'><div class='t'>Percorso</div><div class='v'>Non e' una cartella</div><div class='e'>" + esc(p) + "</div><p>" + sdFileBrowserLinks(p, false) + "</p></div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }
  html += "<div class='card blue'><div class='t'>Cartella corrente</div><div class='v' style='font-size:28px'>" + esc(p) + "</div><div class='e'>";
  html += "<a class='button' href='/sd-files?p=/'>Root</a> ";
  if (p != "/") html += "<a class='button' href='/sd-files?p=" + esc(sdParentPath(p)) + "'>Su</a> ";
  html += "<a class='button' href='/sd-files?p=/logs'>Logs</a> <a class='button' href='/sd-files?p=/backup_recovery'>Recovery</a> <a class='button' href='/sd-files?p=/backup'>Backup</a>";
  html += "</div></div>";
  html += "<div class='card'><div class='t'>Contenuto</div><div class='tablewrap'><table class='hist'><tr><th>Nome</th><th>Tipo</th><th>Dimensione</th><th>Azione</th></tr>";
  int count = 0;
  while (true) {
    File e = dir.openNextFile();
    if (!e) break;
    String name = String(e.name());
    if (!name.startsWith("/")) name = (p == "/" ? "/" : p + "/") + name;
    bool isDir = e.isDirectory();
    uint64_t sz = isDir ? 0 : e.size();
    html += "<tr><td style='text-align:left'>" + esc(fileNameOnly(name)) + "</td>";
    html += "<td>" + String(isDir ? "Cartella" : "File") + "</td>";
    html += "<td>" + String(isDir ? "-" : formatBytes64(sz)) + "</td>";
    html += "<td>" + sdFileBrowserLinks(name, isDir) + "</td></tr>";
    count++;
    e.close();
  }
  if (count == 0) html += "<tr><td colspan='4' style='text-align:left;color:#8b949e'>Cartella vuota</td></tr>";
  html += "</table></div></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSdViewFile() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String p = normalizeSdPath(server.hasArg("p") ? server.arg("p") : "");
  if (!sdMounted || !safeSdBrowserPath(p) || p == "/") { sendActionPage("Apri file", "File non valido o SD non montata.", 2, "/files"); return; }
  File f = SD.open(p, FILE_READ);
  if (!f || f.isDirectory()) { sendActionPage("Apri file", "File non trovato.", 2, "/sd-files"); return; }
  String html = htmlHeader("Apri file SD");
  html += "<h1>Apri file</h1><p><a href='/sd-files?p=" + esc(sdParentPath(p)) + "'>Cartella</a> &middot; <a href='/sd-download?p=" + esc(p) + "'>Scarica</a> &middot; <a href='/files'>File & Log</a></p>";
  html += "<div class='card'><div class='t'>File</div><div class='v' style='font-size:24px'>" + esc(p) + "</div><div class='e'>Dimensione: " + formatBytes64(f.size()) + "</div></div>";
  if (!isReadableTextFile(p)) {
    html += "<div class='card warn'><div class='t'>Anteprima</div><div class='e'>Questo file non sembra testuale. Usa Scarica.</div></div>";
  } else if (f.size() > 65536) {
    html += "<div class='card warn'><div class='t'>Anteprima troppo grande</div><div class='e'>File oltre 64 KB. Scaricalo per leggerlo completo.</div></div>";
  } else {
    String content = f.readString();
    html += "<div class='card'><div class='t'>Anteprima</div><pre style='white-space:pre-wrap;overflow:auto;background:#0b1220;border:1px solid #30363d;border-radius:14px;padding:12px'>" + esc(content) + "</pre></div>";
  }
  f.close();
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSdDownloadFile() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String p = normalizeSdPath(server.hasArg("p") ? server.arg("p") : "");
  if (!sdMounted || !safeSdBrowserPath(p) || p == "/") { server.send(404, "text/plain", "File non valido o SD non montata"); return; }
  File f = SD.open(p, FILE_READ);
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "File non trovato"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileNameOnly(p) + "\"");
  server.streamFile(f, fileMimeFromPath(p));
  f.close();
}

void handleSdDeleteFile() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String p = normalizeSdPath(server.hasArg("p") ? server.arg("p") : "");
  if (!sdMounted || !safeSdBrowserPath(p) || p == "/") { sendActionPage("Elimina file", "Percorso non valido o SD non montata.", 2, "/files"); return; }
  if (!server.hasArg("confirm") || server.arg("confirm") != "YES") {
    String html = htmlHeader("Conferma eliminazione");
    html += "<h1>Conferma eliminazione</h1><p><a href='/sd-files?p=" + esc(sdParentPath(p)) + "'>Annulla</a></p>";
    html += "<div class='card danger'><div class='t'>Attenzione</div><div class='v' style='font-size:24px'>" + esc(p) + "</div><div class='e'>Eliminazione definitiva dalla microSD.</div>";
    html += "<p><a class='button danger' href='/sd-delete?p=" + esc(p) + "&confirm=YES'>Conferma elimina</a> <a class='button' href='/sd-files?p=" + esc(sdParentPath(p)) + "'>Annulla</a></p></div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }
  File f = SD.open(p);
  bool ok = false;
  if (f) {
    if (f.isDirectory()) {
      uint32_t files=0, dirs=0;
      ok = sdWipeRecursive(p, files, dirs);
      SD.rmdir(p);
    } else {
      ok = SD.remove(p);
    }
    f.close();
  }
  addEventLog("SD", ok ? ("Eliminato " + p) : ("Errore eliminazione " + p));
  sendActionPage("Elimina SD", ok ? "Elemento eliminato." : "Eliminazione non riuscita.", 2, "/sd-files?p=" + sdParentPath(p));
}

String storageMode() {
  prefs.begin("victron", true);
  String m = prefs.getString("storage_mode", "both");
  prefs.end();
  if (m != "internal" && m != "sd" && m != "both") m = "both";
  return m;
}

bool storageUseSd() {
  String m = storageMode();
  return (m == "sd" || m == "both");
}

bool storageUseInternal() {
  String m = storageMode();
  return (m == "internal" || m == "both");
}

int sdLogIntervalSec() {
  prefs.begin("victron", true);
  int sec = prefs.getInt("sd_log_sec", 60);
  prefs.end();
  if (sec < 30) sec = 30;
  if (sec > 3600) sec = 3600;
  return sec;
}

String isoDateForFile() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 20) && (timeinfo.tm_year + 1900) >= 2024) {
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
    return String(buf);
  }
  return "boot";
}

String sdMonthDir() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 20) && (timeinfo.tm_year + 1900) >= 2024) {
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m", &timeinfo);
    return String(buf);
  }
  return "boot";
}

String sdLogFileNameForDate(const String& date) {
  if (date == "boot") return "/logs/boot.csv";
  String month = date.substring(0, 7);
  return "/logs/" + month + "/" + date + ".csv";
}

String sdLogFileName() { return sdLogFileNameForDate(isoDateForFile()); }
String sdCsvHeader() { return "time,uptime_s,pv_w,pv_v,batt_v,batt_a,batt_w,charge_state,mppt_state,error,yield_today_kwh,esp_batt_v,esp_batt_pct,wifi_rssi,ve_online"; }

String csvEscape(String v) { v.replace("\"", "'"); v.replace(",", " "); return v; }

String sdCsvCurrentLine() {
  String line;
  line += csvEscape(timeText()) + ",";
  line += String(millis() / 1000UL) + ",";
  line += jsNum(panelW, 1) + ",";
  line += jsNum(panelV, 3) + ",";
  line += jsNum(battV, 3) + ",";
  line += jsNum(battA, 3) + ",";
  line += jsNum(battW, 2) + ",";
  line += csvEscape(chargeState) + ",";
  line += csvEscape(mpptState) + ",";
  line += csvEscape(errorState) + ",";
  line += jsNum(yieldTodayKWh, 3) + ",";
  line += jsNum(espBatteryVoltage(), 3) + ",";
  line += jsNum(espBatteryPercent(), 0) + ",";
  line += String(WiFi.RSSI()) + ",";
  line += String(victronOnline() ? 1 : 0);
  return line;
}

bool sdEnsureReadyForWrite() {
  if (!sdMounted) { if (!sdMount(false)) return false; }
  if (!sdMounted) return false;
  sdCreateBaseDirs();
  String mdir = "/logs/" + sdMonthDir();
  if (sdMonthDir() != "boot" && !SD.exists(mdir)) SD.mkdir(mdir);
  return true;
}

bool sdAppendLine(const String& path, const String& line, bool headerIfNew) {
  if (!sdEnsureReadyForWrite()) { lastSdLogStatus = "SD non pronta: " + sdLastStatus; return false; }
  bool needHeader = headerIfNew && !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) { lastSdLogStatus = "Errore apertura file SD: " + path; return false; }
  if (needHeader) f.println(sdCsvHeader());
  f.println(line);
  f.close();
  lastSdLogStatus = "OK: scritto " + path;
  lastSdLogOkMs = millis();
  return true;
}

void sdLoggerLoop() {
  if (!storageUseSd()) return;
  int sec = sdLogIntervalSec();
  if (lastSdLogMs != 0 && millis() - lastSdLogMs < (unsigned long)sec * 1000UL) return;
  lastSdLogMs = millis();
  bool ok = sdAppendLineProtected(sdLogFileName(), sdCsvCurrentLine(), true);
  if (ok) addEventLog("SDLOG", "Campione salvato su " + sdLogFileName());
  else if (storageUseInternal()) addEventLog("SDLOG", "Fallback interno: " + lastSdLogStatus);
}

void handleStoragePage() {
  if (!requireAuth()) return;
  String mode = storageMode();
  int sec = sdLogIntervalSec();
  String html = htmlHeader("Storage");
  html += "<h1>Storage / Logger</h1><p><a href='/'>Dashboard</a> &middot; <a href='/sd'>MicroSD</a> &middot; <a href='/stats-sd'>Statistiche</a> &middot; <a href='/sd-logs'>Log SD</a> &middot; <a href='/sd-log.csv'>CSV oggi</a></p>";
  html += "<div class='card'><div class='t'>Modalita' salvataggio</div><div class='v'>" + mode + "</div><div class='e'>Interna = LittleFS, SD = archivio CSV, Entrambe = cache interna + archivio lungo su SD.</div>";
  html += "<form method='POST' action='/storage-save'><p><label><input type='radio' name='mode' value='internal'" + String(mode=="internal"?" checked":"") + "> Solo interna LittleFS</label></p>";
  html += "<p><label><input type='radio' name='mode' value='sd'" + String(mode=="sd"?" checked":"") + "> Solo MicroSD</label></p>";
  html += "<p><label><input type='radio' name='mode' value='both'" + String(mode=="both"?" checked":"") + "> Entrambe</label></p>";
  html += "<p>Intervallo logging SD: <input name='sec' type='number' min='30' max='3600' value='" + String(sec) + "'> secondi</p>";
  html += "<p><button class='button' type='submit'>Salva storage</button></p></form></div>";
  html += "<div class='grid'>";
  html += card("LittleFS", littleFsReady ? "Attivo" : "Non attivo", littleFsReady ? ("Usati " + String((unsigned long)LittleFS.usedBytes()) + " / " + String((unsigned long)LittleFS.totalBytes())) : "Filesystem interno non pronto");
  html += card("MicroSD", sdMounted ? "Montata" : "Smontata", sdMounted ? ("Usati " + formatBytes64(SD.usedBytes()) + " / " + formatBytes64(SD.totalBytes()) + "<br>File: " + sdLogFileName()) : (esc(sdLastStatus) + "<br><a class='button' href='/sd?mount=1'>Monta SD</a>"));
  html += card("Ultimo log SD", lastSdLogOkMs ? "OK" : "N/D", esc(lastSdLogStatus) + "<br>Intervallo: " + String(sec) + " sec");
  html += card("Azioni", "Snapshot", "<a class='button' href='/sd-snapshot'>Salva snapshot ora</a> <a class='button' href='/sd-log.csv'>Scarica CSV oggi</a> <a class='button' href='/files'>File & Log</a> <a class='button' href='/backup-sd'>Backup config su SD</a> <a class='button' href='/backup-recovery'>Backup/Recovery completo</a>");
  html += "</div><div class='card warn'><div class='t'>Consiglio</div><p>Per affidabilita': lascia <b>Entrambe</b>. LittleFS resta cache rapida; MicroSD diventa archivio lungo e leggibile da PC.</p></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleStorageSave() {
  if (!requireAuth()) return;
  String mode = server.arg("mode");
  if (mode != "internal" && mode != "sd" && mode != "both") mode = "both";
  int sec = server.arg("sec").toInt();
  if (sec < 30) sec = 30;
  if (sec > 3600) sec = 3600;
  prefs.begin("victron", false);
  prefs.putString("storage_mode", mode);
  prefs.putInt("sd_log_sec", sec);
  prefs.end();
  addEventLog("STORAGE", "Modalita' " + mode + ", intervallo " + String(sec) + " sec");
  sendActionPage("Storage", "Impostazioni storage salvate.", 2, "/storage");
}

void handleSdSnapshot() {
  if (!requireAuth()) return;
  bool ok = sdAppendLineProtected(sdLogFileName(), sdCsvCurrentLine(), true);
  addEventLog("SDLOG", ok ? "Snapshot manuale salvato" : ("Snapshot fallito: " + lastSdLogStatus));
  sendActionPage("Snapshot SD", ok ? "Snapshot salvato su microSD." : lastSdLogStatus, 2, "/storage");
}

void handleSdLogCsv() {
  if (!requireAuth()) return;
  if (!sdEnsureReadyForWrite()) { server.send(503, "text/plain", "SD non pronta: " + sdLastStatus); return; }
  String path = sdLogFileName();
  if (!SD.exists(path)) { server.send(404, "text/plain", "File log non ancora creato: " + path); return; }
  File f = SD.open(path, "r");
  if (!f) { server.send(500, "text/plain", "Errore apertura: " + path); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=victron_" + isoDateForFile() + ".csv");
  server.streamFile(f, "text/csv");
  f.close();
}


String dailyLabelFromIndex(int idx) {
  if (idx < 0) return "N/D";
  int rel = (idx - currentDayIndex + 31) % 31;
  if (rel == 0) return "oggi";
  if (rel == 30) return "ieri";
  return String(31 - rel) + " giorni fa";
}

float slotAvgPanel(const HistorySlot& s) { return s.samples ? (s.panelSum / s.samples) : 0; }
float slotAvgBatt(const HistorySlot& s) { return s.samples ? (s.battSum / s.samples) : 0; }
float slotBattMin(const HistorySlot& s) { return (s.battMin == 999) ? 0 : s.battMin; }

int bestDayIndex() {
  int best = -1;
  float maxWh = -1;
  for (int i=0;i<31;i++) {
    if (daily[i].samples > 0 && daily[i].wh > maxWh) { maxWh = daily[i].wh; best = i; }
  }
  return best;
}

int worstDayIndex() {
  int worst = -1;
  float minWh = 999999999.0f;
  for (int i=0;i<31;i++) {
    if (daily[i].samples > 0 && daily[i].wh < minWh) { minWh = daily[i].wh; worst = i; }
  }
  return worst;
}

float monthDailyAverageWh() {
  float sum = 0; int n = 0;
  for (int i=0;i<31;i++) if (daily[i].samples > 0) { sum += daily[i].wh; n++; }
  return n ? sum / n : 0;
}

float lastNDaysWh(int days) {
  if (currentDayIndex < 0) return 0;
  float sum = 0;
  for (int k=0;k<days && k<31;k++) {
    int idx = (currentDayIndex - k + 31) % 31;
    if (daily[idx].samples > 0) sum += daily[idx].wh;
  }
  return sum;
}

void addSdLogRowsForDir(String& html, const String& dirPath) {
  if (!sdMounted) return;
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;
    String name = String(f.name());
    bool isDir = f.isDirectory();
    size_t sz = f.size();
    f.close();
    if (isDir) {
      addSdLogRowsForDir(html, name);
    } else if (name.endsWith(".csv")) {
      html += "<tr><td>" + esc(name) + "</td><td>" + formatBytes64(sz) + "</td><td><a class='button' href='/sd-log-download?file=" + esc(name) + "'>Scarica</a> <a class='button danger' href='/sd-log-delete?file=" + esc(name) + "' onclick=\"return confirm('Eliminare questo log?')\">Elimina</a></td></tr>";
    }
  }
}

void handleSdLogsPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Log SD");
  html += "<h1>Log microSD</h1><p><a href='/'>Dashboard</a> &middot; <a href='/storage'>Storage</a> &middot; <a href='/stats-sd'>Stats</a> &middot; <a href='/sd'>MicroSD</a></p>";
  html += "<div class='card'><div class='t'>Logger intelligente</div><div class='v'>CSV giornalieri</div><p>I nuovi log vengono salvati in cartelle mensili: <b>/logs/YYYY-MM/YYYY-MM-DD.csv</b>. Il file <b>/logs/boot.csv</b> viene usato se data/ora non sono ancora valide.</p>";
  html += "<p><a class='button' href='/sd-snapshot'>Salva snapshot ora</a> <a class='button' href='/sd-log.csv'>CSV oggi</a></p></div>";
  if (!sdMounted) {
    html += "<div class='card warn'><div class='t'>SD non montata</div><p>" + esc(sdLastStatus) + "</p><p><a class='button' href='/sd?mount=1'>Monta SD</a></p></div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }
  html += "<div class='card'><div class='t'>File disponibili</div><table class='dataTable'><tr><th>File</th><th>Dimensione</th><th>Azioni</th></tr>";
  addSdLogRowsForDir(html, "/logs");
  html += "</table></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

bool safeSdPath(const String& path) {
  if (!path.startsWith("/logs/")) return false;
  if (path.indexOf("..") >= 0) return false;
  if (!path.endsWith(".csv")) return false;
  return true;
}

void handleSdLogDownload() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String path = server.arg("file");
  if (!safeSdPath(path)) { server.send(400, "text/plain", "Percorso non valido"); return; }
  if (!SD.exists(path)) { server.send(404, "text/plain", "File non trovato"); return; }
  File f = SD.open(path, "r");
  if (!f) { server.send(500, "text/plain", "Errore apertura file"); return; }
  String fn = path.substring(path.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=" + fn);
  server.streamFile(f, "text/csv");
  f.close();
}

void handleSdLogDelete() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String path = server.arg("file");
  if (!safeSdPath(path)) { sendActionPage("Log SD", "Percorso non valido.", 2, "/sd-logs"); return; }
  bool ok = SD.exists(path) && SD.remove(path);
  addEventLog("SDLOG", ok ? ("Eliminato " + path) : ("Eliminazione fallita " + path));
  sendActionPage("Log SD", ok ? "Log eliminato." : "File non trovato o errore eliminazione.", 2, "/sd-logs");
}

bool sdEnsureDir(const String& path) {
  if (!sdMounted) return false;
  if (path.length() == 0 || path == "/") return true;
  if (SD.exists(path)) return true;

  String partial = "";
  int start = 1;
  while (start < (int)path.length()) {
    int slash = path.indexOf('/', start);
    String part = (slash < 0) ? path.substring(start) : path.substring(start, slash);
    if (part.length()) {
      partial += "/" + part;
      if (!SD.exists(partial)) {
        if (!SD.mkdir(partial)) return false;
      }
    }
    if (slash < 0) break;
    start = slash + 1;
  }
  return true;
}

String cloneFolderName() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 20) && (timeinfo.tm_year + 1900) >= 2024) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &timeinfo);
    return String(buf);
  }
  return "boot_" + String(millis() / 1000UL);
}

bool copyLittleFsFileToSd(const String& src, const String& dst, String& detail) {
  if (!littleFsReady || !LittleFS.exists(src)) {
    detail += "<li>Skip " + src + ": non presente</li>";
    return true;
  }
  fs::File in = LittleFS.open(src, "r");
  if (!in) { detail += "<li>Errore apertura LittleFS " + src + "</li>"; return false; }
  File out = SD.open(dst, FILE_WRITE);
  if (!out) { in.close(); detail += "<li>Errore apertura SD " + dst + "</li>"; return false; }
  uint8_t buf[512];
  size_t total = 0;
  while (in.available()) {
    size_t n = in.read(buf, sizeof(buf));
    if (n == 0) break;
    out.write(buf, n);
    total += n;
    yield();
  }
  out.close();
  in.close();
  detail += "<li>Copiato " + src + " → " + dst + " (" + formatBytes64(total) + ")</li>";
  return true;
}

bool backupRunningFirmwareToSd(const String& dst, String& detail) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) { detail += "<li>Partizione firmware corrente non trovata</li>"; return false; }
  File out = SD.open(dst, FILE_WRITE);
  if (!out) { detail += "<li>Errore apertura firmware.bin su SD</li>"; return false; }

  size_t sketchSize = ESP.getSketchSize();
  if (sketchSize == 0 || sketchSize > running->size) sketchSize = running->size;

  uint8_t buf[1024];
  size_t offset = 0;
  while (offset < sketchSize) {
    size_t n = sketchSize - offset;
    if (n > sizeof(buf)) n = sizeof(buf);
    if (esp_partition_read(running, offset, buf, n) != ESP_OK) {
      out.close();
      detail += "<li>Errore lettura firmware a offset " + String(offset) + "</li>";
      return false;
    }
    out.write(buf, n);
    offset += n;
    if (sketchSize > 0) {
      int p = 35 + (int)((offset * 30UL) / sketchSize);
      setBackupProgress(p, "Copia firmware: " + formatBytes64(offset) + " / " + formatBytes64(sketchSize));
    }
    yield();
  }
  out.close();
  detail += "<li>Firmware corrente salvato: " + dst + " (" + formatBytes64(sketchSize) + ")</li>";
  return true;
}

bool createFullBackupBackup(const String& reason, String& detailOut) {
  detailOut = "";
  setBackupProgress(3, "Controllo microSD...");
  if (!sdEnsureReadyForWrite()) { detailOut = "SD non pronta: " + sdLastStatus; setBackupProgress(100, "Errore: SD non pronta"); return false; }

  String reasonSafe = reason; reasonSafe.replace(" ", "_"); reasonSafe.replace("/", "-");
  String root = "/backup_recovery/recovery_" + cloneFolderName() + "_" + reasonSafe;
  setBackupProgress(8, "Creazione cartella Backup/Recovery...");
  if (!sdEnsureDir("/backup_recovery") || !sdEnsureDir(root)) { detailOut = "Impossibile creare cartella clone su SD"; setBackupProgress(100, "Errore cartella Backup/Recovery"); return false; }

  bool ok = true;
  String details = "<ul>";

  setBackupProgress(14, "Scrittura manifest.json...");
  File manifest = SD.open(root + "/manifest.json", FILE_WRITE);
  if (manifest) {
    manifest.print("{");
    manifest.print("\"firmware\":\"" + String(FW_VERSION) + "\",");
    manifest.print("\"name\":\"" + String(FW_NAME) + "\",");
    manifest.print("\"build\":\"" + buildText() + "\",");
    manifest.print("\"created\":\"" + timeText() + "\",");
    manifest.print("\"ip\":\"" + WiFi.localIP().toString() + "\",");
    manifest.print("\"storage_mode\":\"" + storageMode() + "\",");
    manifest.print("\"reason\":\"" + jsonEsc(reason) + "\",");
    manifest.print("\"note\":\"Backup / Recovery: firmware.bin + config + LittleFS principali. Ripristino firmware via OTA/manual upload.\"");
    manifest.print("}");
    manifest.close();
    details += "<li>Creato manifest.json</li>";
  } else { ok = false; details += "<li>Errore manifest.json</li>"; }

  setBackupProgress(22, "Salvataggio configurazione...");
  File cfg = SD.open(root + "/config.json", FILE_WRITE);
  if (cfg) { cfg.print(settingsBackupJson()); cfg.close(); details += "<li>Config salvata</li>"; }
  else { ok = false; details += "<li>Errore config.json</li>"; }

  setBackupProgress(35, "Copia firmware corrente su SD...");
  ok &= backupRunningFirmwareToSd(root + "/firmware.bin", details);
  setBackupProgress(70, "Copia storico interno LittleFS...");
  ok &= copyLittleFsFileToSd("/history.bin", root + "/littlefs_history.bin", details);
  setBackupProgress(78, "Copia log eventi interni...");
  ok &= copyLittleFsFileToSd("/events.log", root + "/littlefs_events.log", details);

  // Copia anche il CSV del giorno se esiste, utile come snapshot dati.
  setBackupProgress(84, "Copia eventuale CSV del giorno...");
  String todayLog = sdLogFileName();
  if (SD.exists(todayLog)) {
    File in = SD.open(todayLog, "r");
    File out = SD.open(root + "/sd_today_log.csv", FILE_WRITE);
    if (in && out) {
      uint8_t buf[512]; size_t total = 0;
      while (in.available()) { size_t n = in.read(buf, sizeof(buf)); if (!n) break; out.write(buf, n); total += n; yield(); }
      details += "<li>CSV oggi copiato (" + formatBytes64(total) + ")</li>";
    } else details += "<li>CSV oggi non copiato</li>";
    if (in) in.close(); if (out) out.close();
  } else {
    details += "<li>CSV oggi non ancora presente</li>";
  }

  details += "</ul><p><b>Cartella:</b> " + root + "</p>";
  detailOut = details;
  addEventLog("BACKUP", String("Backup/Recovery completo ") + (ok ? "OK " : "con errori ") + root);
  setBackupProgress(92, "Pulizia backup vecchi: mantengo ultimi 5 recovery...");
  pruneBackupBackupsKeepTwo();
  setBackupProgress(100, ok ? "Backup/Recovery completato" : "Backup/Recovery completato con errori");
  return ok;
}

bool createFullBackupBackup(String& detailOut) {
  return createFullBackupBackup("manuale", detailOut);
}

void pruneBackupBackupsKeepTwo() {
  if (!sdMounted) return;
  int keep = prefsGetIntSafe("backup_keep", 5);
  if (keep < 1) keep = 1;
  if (keep > 5) keep = 5;
  String newest[5] = {"", "", "", "", ""};
  File dir = SD.open("/backup_recovery");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }

  File entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = String(entry.name());
      if (!name.startsWith("/")) name = "/backup_recovery/" + name;
      if (name.indexOf("/backup_recovery/recovery_") == 0) {
        for (int i = 0; i < keep; i++) {
          if (name > newest[i]) {
            for (int j = keep - 1; j > i; j--) newest[j] = newest[j - 1];
            newest[i] = name;
            break;
          }
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  dir = SD.open("/backup_recovery");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = String(entry.name());
      if (!name.startsWith("/")) name = "/backup_recovery/" + name;
      bool keepThis = false;
      for (int i = 0; i < keep; i++) if (name == newest[i]) keepThis = true;
      if (name.indexOf("/backup_recovery/recovery_") == 0 && !keepThis) {
        uint32_t files = 0, dirs = 0;
        sdWipeRecursive(name, files, dirs);
        SD.rmdir(name);
        addEventLog("BACKUP", "Rimosso recovery vecchio: " + name);
      }
    }
    entry.close();
    entry = dir.openNextFile();
    yield();
  }
  dir.close();
}

void autoBackupBeforeOta(const String& source) {
  prefs.begin("victron", false);
  prefs.putString("last_pre_ota_source", source);
  prefs.putString("last_pre_ota_time", timeIsValid() ? timeText() : buildText());
  prefs.putString("last_pre_ota_status", "Avvio backup pre-OTA");
  prefs.end();

  if (!sdEnsureReadyForWrite()) {
    String msg = "Auto clone pre-OTA saltato, SD non pronta: " + sdLastStatus;
    addEventLog("BACKUP", msg);
    prefs.begin("victron", false);
    prefs.putString("last_pre_ota_status", msg);
    prefs.putBool("last_pre_ota_ok", false);
    prefs.end();
    return;
  }

  String details;
  bool ok = createFullBackupBackup("pre_" + source, details);
  String msg = String("Auto clone pre-OTA ") + (ok ? "OK" : "ERRORE") + " - " + source;
  addEventLog("BACKUP", msg);

  prefs.begin("victron", false);
  prefs.putBool("last_pre_ota_ok", ok);
  prefs.putString("last_pre_ota_status", msg);
  prefs.putString("last_pre_ota_detail", details);
  prefs.end();
}

void autoBackupAfterOtaIfNeeded() {
  prefs.begin("victron", true);
  bool pending = prefs.getBool("clone_after_ota", false);
  String src = prefs.getString("clone_after_src", "OTA");
  prefs.end();
  if (!pending) return;

  if (!sdEnsureReadyForWrite()) {
    addEventLog("BACKUP", "Auto clone post-OTA rinviato, SD non pronta: " + sdLastStatus);
    return;
  }

  String details;
  bool ok = createFullBackupBackup("post_" + src, details);
  if (ok) {
    prefs.begin("victron", false);
    prefs.putBool("clone_after_ota", false);
    prefs.putString("clone_after_last", String(FW_VERSION));
    prefs.end();
  }
  addEventLog("BACKUP", String("Auto clone post-OTA ") + (ok ? "OK" : "ERRORE") + " - " + src);
}

void handleFullBackupBackupPage() {
  handleBackupRecoveryPage();
}

void handleBackupRecoveryPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Backup / Recovery SD");
  html += "<h1>Backup / Recovery completo</h1><p><a href='/'>Dashboard</a> &middot; <a href='/storage'>Storage</a> &middot; <a href='/backup-sd'>Backup config</a> &middot; <a href='/sd'>MicroSD</a></p>";
  html += "<div class='card'><div class='t'>Cosa salva</div><div class='v'>Firmware + config + dati interni</div>";
  html += "<p>Crea una cartella in <b>/backup_recovery/</b> con: firmware.bin corrente, config.json, manifest.json, storico LittleFS, log eventi e CSV del giorno se presente.</p>";
  html += "<p>Il sistema mantiene automaticamente solo gli <b>ultimi 5 backup recovery</b>. I più vecchi vengono rimossi dalla pulizia retention.</p>";
  html += "<p><b>Ripristino:</b> carichi firmware.bin dalla pagina OTA locale; config.json resta disponibile per recuperare impostazioni.</p>";
  html += "<p><a class='button' href='/backup-recovery-start'>Avvia backup completo</a></p>";
  html += "</div>";
  html += card("Stato SD", sdMounted ? "OK" : "NO", sdMounted ? ("Libera: " + formatBytes64(SD.totalBytes() - SD.usedBytes()) + "<br>Conservazione: ultimi 5 backup recovery") : esc(sdLastStatus));
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void cloneBackupTask(void* param) {
  cloneTaskRunning = true;
  cloneTaskDone = false;
  cloneTaskOk = false;
  cloneTaskDetail = "";
  setBackupProgress(1, "Avvio backup completo...");
  String details;
  bool ok = createFullBackupBackup("manuale", details);
  cloneTaskOk = ok;
  cloneTaskDetail = details;
  cloneTaskDone = true;
  cloneTaskRunning = false;
  cloneTaskHandle = NULL;
  setBackupProgress(100, ok ? "Backup/Recovery completato" : "Backup/Recovery completato con errori");
  vTaskDelete(NULL);
}

void handleBackupProgressJson() {
  if (!requireAuth()) return;
  String j = "{";
  j += "\"running\":" + String(cloneTaskRunning ? "true" : "false") + ",";
  j += "\"done\":" + String(cloneTaskDone ? "true" : "false") + ",";
  j += "\"ok\":" + String(cloneTaskOk ? "true" : "false") + ",";
  j += "\"percent\":" + String(cloneTaskPercent) + ",";
  j += "\"stage\":\"" + esc(cloneTaskStage) + "\"";
  j += "}";
  sendJsonPretty(j);
}

void handleFullBackupBackupStart() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  if (!sdMounted) { sendActionPage("Backup / Recovery", "SD non pronta: " + sdLastStatus, 3, "/backup-recovery"); return; }
  if (!cloneTaskRunning && !cloneTaskDone) {
    cloneTaskOk = false;
    cloneTaskPercent = 0;
    cloneTaskStage = "Preparazione backup completo...";
    cloneTaskDetail = "";
    xTaskCreatePinnedToCore(cloneBackupTask, "clone_bak", 12288, NULL, 1, &cloneTaskHandle, 1);
  }
  String html = htmlHeader("Backup / Recovery Progress");
  html += "<h1>Backup / Recovery</h1>";
  html += "<div class='card'><p><b>Fase:</b> <span id='stage'>Avvio...</span></p>";
  html += "<div style='width:100%;height:24px;background:#1c2430;border-radius:14px;overflow:hidden;border:1px solid rgba(255,255,255,.12)'><div id='bar' style='height:100%;width:0%;background:linear-gradient(90deg,#5ee77d,#83a8ff);transition:width .4s'></div></div>";
  html += "<p style='font-size:30px;font-weight:800'><span id='pct'>0</span>%</p>";
  html += "<p>Non togliere la microSD e non spegnere durante il backup.</p>";
  html += "<p><a class='button' href='/backup-recovery'>Backup / Recovery</a><a class='button' href='/sd'>MicroSD</a><a class='button' href='/'>Dashboard</a></p></div>";
  html += R"rawliteral(
<script>
function pollBackup(){
 fetch('/clone-progress.json',{cache:'no-store'}).then(r=>r.json()).then(j=>{
   document.getElementById('pct').textContent=j.percent||0;
   document.getElementById('bar').style.width=(j.percent||0)+'%';
   document.getElementById('stage').textContent=j.stage||'In corso...';
   if(j.done){
     document.getElementById('stage').textContent=(j.ok?'Completato: ':'Completato con errori: ')+(j.stage||'');
     setTimeout(()=>{ location.href='/backup-recovery?t='+Date.now(); }, 3500);
   } else setTimeout(pollBackup,900);
 }).catch(e=>{ document.getElementById('stage').textContent='Attendo risposta ESP...'; setTimeout(pollBackup,1800); });
}
pollBackup();
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBackupToSd() {
  if (!requireAuth()) return;
  if (!sdEnsureReadyForWrite()) { sendActionPage("Backup SD", "SD non pronta: " + sdLastStatus, 3, "/settings-backup"); return; }
  sdEnsureDir("/backup");
  String path = "/backup/config_" + isoDateForFile() + ".json";
  File f = SD.open(path, FILE_WRITE);
  if (!f) { sendActionPage("Backup SD", "Errore apertura file backup.", 3, "/settings-backup"); return; }
  f.print(settingsBackupJson());
  f.close();
  addEventLog("BACKUP", "Configurazione salvata su SD: " + path);
  sendActionPage("Backup SD", "Configurazione salvata su " + path, 3, "/settings-backup");
}

void handleStatsSdPage() {
  if (!requireAuth()) return;
  HistorySlot *today = (currentDayIndex >= 0) ? &daily[currentDayIndex] : nullptr;
  int yIdx = (currentDayIndex >= 0) ? ((currentDayIndex + 30) % 31) : -1;
  HistorySlot *yday = (yIdx >= 0) ? &daily[yIdx] : nullptr;
  HistorySlot *month = (currentMonthIndex >= 0) ? &monthly[currentMonthIndex] : nullptr;
  int bIdx = bestDayIndex();
  int wIdx = worstDayIndex();
  float todayWh = today ? today->wh : 0;
  float yWh = yday ? yday->wh : 0;
  float monthWh = month ? month->wh : lastNDaysWh(31);
  float avgDay = monthDailyAverageWh();
  float delta = todayWh - yWh;
  String trend = delta >= 0 ? "+" + String(delta, 0) + " Wh vs ieri" : String(delta, 0) + " Wh vs ieri";
  String html = htmlHeader("Statistiche SD");
  html += "<h1>Recap statistiche</h1><p><a href='/'>Dashboard</a> &middot; <a href='/storage'>Storage</a> &middot; <a href='/sd-logs'>Log SD</a> &middot; <a href='/history-gx'>Storico GX</a> &middot; <a href='/sd-log.csv'>CSV oggi</a></p>";
  html += "<div class='gxHero'><div class='gxTop'><div><div class='gxTitle'>Produzione</div><div class='gxSub'>Recap da storico interno + logger SD</div></div><div class='gxPills'><span class='gxPill ok'>" + storageMode() + "</span><span class='gxPill " + String(sdMounted?"ok":"warn") + "'>SD " + String(sdMounted?"OK":"NO") + "</span></div></div>";
  html += "<div class='gxGrid'><div class='gxBig sun'><div class='gxLabel'>Oggi</div><div class='gxValue'>" + String(todayWh/1000.0f, 3) + " kWh</div><div class='gxBar'><div class='gxBarFill' style='width:" + String(min(100.0f, todayWh/15.0f),0) + "%'></div></div><div class='gxDetails'><div class='gxMini'><span>Ieri</span><b>" + String(yWh/1000.0f,3) + " kWh</b></div><div class='gxMini'><span>Trend</span><b>" + trend + "</b></div></div></div>";
  html += "<div class='gxSide'><div class='gxStatus'><div class='gxLabel'>Mese</div><div class='state'>" + String(monthWh/1000.0f, 3) + " kWh</div></div><div class='gxStatus'><div class='gxLabel'>Media giorno</div><div class='state'>" + String(avgDay/1000.0f, 3) + " kWh</div></div><div class='gxStatus'><div class='gxLabel'>7 giorni</div><div class='state'>" + String(lastNDaysWh(7)/1000.0f, 3) + " kWh</div></div></div></div></div>";
  html += "<div class='grid'>";
  html += card("Giorno migliore", bIdx >= 0 ? String(daily[bIdx].wh/1000.0f, 3) + " kWh" : "N/D", bIdx >= 0 ? (dailyLabelFromIndex(bIdx) + "<br>Max PV: " + String(daily[bIdx].maxW,1) + " W") : "Servono dati storici");
  html += card("Giorno peggiore", wIdx >= 0 ? String(daily[wIdx].wh/1000.0f, 3) + " kWh" : "N/D", wIdx >= 0 ? (dailyLabelFromIndex(wIdx) + "<br>Campioni: " + String(daily[wIdx].samples)) : "Servono dati storici");
  html += card("Batteria oggi", today ? (String(slotBattMin(*today),2) + " / " + String(today->battMax,2) + " V") : "N/D", today ? ("Media: " + String(slotAvgBatt(*today),2) + " V") : "N/D");
  html += card("PV oggi", today ? (String(today->maxW,0) + " W max") : "N/D", today ? ("Media: " + String(slotAvgPanel(*today),0) + " W<br>Campioni: " + String(today->samples)) : "N/D");
  html += card("MicroSD", sdMounted ? "OK" : "NO", sdMounted ? ("Libera: " + formatBytes64(SD.totalBytes() - SD.usedBytes()) + "<br>File oggi: " + sdLogFileName() + "<br><a class='button' href='/sd-logs'>Gestisci log</a>") : esc(sdLastStatus));
  html += card("Alert", String(alertCountNow()), "Vai agli alert avanzati<br><a class='button' href='/alerts'>Apri alert</a>");
  html += "</div><div class='card'><div class='t'>Nota</div><p>Il recap migliora con piu' giorni di logger attivo. Lascia storage su <b>Entrambe</b> per avere cache interna e archivio lungo su microSD.</p></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}



String resetReasonText() {
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON: return "Power on / alimentazione";
    case ESP_RST_EXT: return "Reset esterno / tasto EN";
    case ESP_RST_SW: return "Riavvio software";
    case ESP_RST_PANIC: return "Crash / panic";
    case ESP_RST_INT_WDT: return "Watchdog interrupt";
    case ESP_RST_TASK_WDT: return "Watchdog task";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_DEEPSLEEP: return "Risveglio deep sleep";
    case ESP_RST_BROWNOUT: return "Brownout / alimentazione bassa";
    case ESP_RST_SDIO: return "Reset SDIO";
    default: return "Sconosciuto";
  }
}

String powerHealthText() {
  esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_BROWNOUT) return "ATTENZIONE: ultimo reset da brownout. Controlla buck/alimentazione.";
  if (r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_INT_WDT) return "Watchdog rilevato. Controlla loop, SD o rete.";
  if (r == ESP_RST_PANIC) return "Crash/panic rilevato. Verifica log e recovery.";
  return "OK: nessun problema alimentazione evidente dall'ultimo reset.";
}

int sdCountEntries(const String& path, bool recursive) {
  if (!sdMounted) return 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  File f = dir.openNextFile();
  while (f) {
    n++;
    if (recursive && f.isDirectory()) {
      String child = String(f.name());
      if (!child.startsWith("/")) child = path + "/" + child;
      n += sdCountEntries(child, true);
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return n;
}

uint64_t sdDirBytes(const String& path) {
  if (!sdMounted) return 0;
  File dir = SD.open(path);
  if (!dir) return 0;
  if (!dir.isDirectory()) { uint64_t sz = dir.size(); dir.close(); return sz; }
  uint64_t total = 0;
  File f = dir.openNextFile();
  while (f) {
    String child = String(f.name());
    if (!child.startsWith("/")) child = path + "/" + child;
    if (f.isDirectory()) total += sdDirBytes(child);
    else total += f.size();
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return total;
}


float thresholdBattLow() {
  prefs.begin("victron", true);
  float v = prefs.getFloat("thr_batt_low", 12.0f);
  prefs.end();
  return v;
}

float thresholdEspBatLow() {
  prefs.begin("victron", true);
  float v = prefs.getFloat("thr_esp_low", 25.0f);
  prefs.end();
  return v;
}

int thresholdWifiWeak() {
  prefs.begin("victron", true);
  int v = prefs.getInt("thr_wifi", -78);
  prefs.end();
  return v;
}

int thresholdNoVedirectSec() {
  prefs.begin("victron", true);
  int v = prefs.getInt("thr_ved_sec", 120);
  prefs.end();
  return v;
}

int thresholdSdFullPercent() {
  prefs.begin("victron", true);
  int v = prefs.getInt("thr_sd_pct", 90);
  prefs.end();
  return v;
}

String normalizedBatteryAh(String raw) {
  raw.trim();
  raw.replace(",", ".");
  raw.replace("Ah", "");
  raw.replace("AH", "");
  raw.replace("ah", "");
  raw.replace("aH", "");
  raw.trim();
  if (raw.endsWith("H") || raw.endsWith("h")) {
    raw.remove(raw.length() - 1);
    raw.trim();
  }
  return raw;
}

float configuredPanelWatts() {
  prefs.begin("victron", true);
  String panel = prefs.getString("plant_panel_w", "120");
  prefs.end();
  panel.trim();
  float w = panel.toFloat();
  if (w < 1.0f || w > 5000.0f) w = 120.0f;
  return w;
}

String plantInfoSummary() {
  prefs.begin("victron", true);
  String name = prefs.getString("plant_name", "Impianto Victron");
  String panel = prefs.getString("plant_panel_w", "120");
  String battAh = prefs.getString("plant_batt_ah", "N/D");
  String battType = prefs.getString("plant_batt_type", "N/D");
  String sysV = prefs.getString("plant_sys_v", "12");
  prefs.end();

  String battLine = "Batteria: ";
  battLine += esc(sysV) + " V";
  if (battAh.length() > 0 && battAh != "N/D") battLine += " / " + esc(battAh) + " Ah";
  if (battType.length() > 0 && battType != "N/D") battLine += " / " + esc(battType);

  return esc(name) + "<br>Pannello: " + esc(panel) + " W<br>" + battLine;
}

int healthScoreNow() {
  int score = 100;
  if (WiFi.status() != WL_CONNECTED) score -= 25;
  else if (WiFi.RSSI() < thresholdWifiWeak()) score -= 10;
  if (!victronOnline() && (millis() - lastVictronMs) / 1000UL > (unsigned long)thresholdNoVedirectSec()) score -= 20;
  if (errorState != "0" && errorState != "N/D") score -= 25;
  float ep = espBatteryPercent();
  if (!isnan(ep) && ep < thresholdEspBatLow()) score -= 10;
  if (sdMounted && SD.totalBytes() > 0) {
    int usedPct = (int)((SD.usedBytes() * 100ULL) / SD.totalBytes());
    if (usedPct >= thresholdSdFullPercent()) score -= 10;
  } else if (storageUseSd()) score -= 10;
  if (!littleFsReady) score -= 10;
  if (ESP.getFreeHeap() < 45000) score -= 10;
  if (esp_reset_reason() == ESP_RST_BROWNOUT) score -= 15;
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

String healthStatusText() {
  int s = healthScoreNow();
  if (s >= 90) return "Ottimo";
  if (s >= 75) return "Buono";
  if (s >= 55) return "Attenzione";
  return "Critico";
}

void sdCreateBaseDirs() {
  if (!sdMounted) return;
  const char* dirs[] = {"/logs", "/stats", "/backup", "/backup_recovery", "/config", "/exports", "/diagnostic"};
  for (uint8_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
    if (!SD.exists(dirs[i])) SD.mkdir(dirs[i]);
  }
}

void handlePlantInfoPage() {
  if (!requireAuth()) return;

  prefs.begin("victron", true);
  String plantName = prefs.getString("plant_name", "Impianto Victron");
  String plantPanelW = prefs.getString("plant_panel_w", "120");
  String plantBattAh = prefs.getString("plant_batt_ah", "");
  String plantBattType = prefs.getString("plant_batt_type", "Piombo/AGM");
  String plantSysV = prefs.getString("plant_sys_v", "12");
  String plantNotes = prefs.getString("plant_notes", "");
  prefs.end();

  String html = htmlHeader("Info impianto");
  html += "<h1>Info impianto</h1><p><a href='/'>Dashboard</a> &middot; <a href='/energy-today'>Energia</a> &middot; <a href='/stats-sd'>Statistiche</a></p>";
  html += "<form method='POST' action='/plant-info-save'><div class='card'><div class='t'>Dati manuali impianto</div>";
  html += "<p>Nome impianto<br><input name='name' value='" + esc(plantName) + "'></p>";
  html += "<p>Potenza pannello W<br><input name='panelw' type='number' step='1' value='" + esc(plantPanelW) + "'><br><span class='sectionNote'>Questo valore scala automaticamente il grafico live pannello in dashboard.</span></p>";
  html += "<p>Capacita batteria Ah<br><input name='battah' type='number' step='0.1' value='" + esc(plantBattAh) + "'></p>";
  html += "<p>Tipo batteria<br><select name='batttype'>";
  const char* opts[] = {"Piombo/AGM", "GEL", "AGM", "Piombo", "Litio", "Altro"};
  for (auto &o: opts) html += "<option" + String(plantBattType==o?" selected":"") + ">" + String(o) + "</option>";
  html += "</select></p>";
  html += "<p>Tensione sistema<br><select name='sysv'><option" + String(plantSysV == "12" ? " selected" : "") + ">12</option><option" + String(plantSysV == "24" ? " selected" : "") + ">24</option><option" + String(plantSysV == "48" ? " selected" : "") + ">48</option></select> V</p>";
  html += "<p>Note installazione<br><textarea name='notes' rows='4'>" + esc(plantNotes) + "</textarea></p>";
  html += "<p><button class='button' type='submit'>Salva info impianto</button></p></div></form>";

  html += card("Batteria configurata", esc(plantSysV) + " V / " + (plantBattAh.length() ? esc(plantBattAh) : String("N/D")) + " Ah", "Tipo: " + esc(plantBattType));
  html += card("Uso pannello", isnan(panelW)?"N/D":String((int)constrain(panelW / max(1.0f, plantPanelW.toFloat()) * 100.0f, 0.0f, 150.0f)) + "%", "Potenza attuale / potenza pannello impostata: " + esc(plantPanelW) + " W");
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handlePlantInfoSave() {
  if (!requireAuth()) return;

  String name = server.arg("name");
  String panelWSet = server.arg("panelw");
  String battAh = normalizedBatteryAh(server.arg("battah"));
  String battType = server.arg("batttype");
  String sysV = server.arg("sysv");
  String notes = server.arg("notes");

  name.trim();
  panelWSet.trim();
  battType.trim();
  sysV.trim();
  notes.trim();

  if (name.length() == 0) name = "Impianto Victron";
  if (panelWSet.length() == 0) panelWSet = "120";
  if (battType.length() == 0) battType = "Piombo/AGM";
  if (sysV.length() == 0) sysV = "12";

  prefs.begin("victron", false);
  prefs.putString("plant_name", name);
  prefs.putString("plant_panel_w", panelWSet);
  prefs.putString("plant_batt_ah", battAh);
  prefs.putString("plant_batt_type", battType);
  prefs.putString("plant_sys_v", sysV);
  prefs.putString("plant_notes", notes);
  prefs.end();

  addEventLog("CFG", "Info impianto aggiornate: batteria " + sysV + "V " + battAh + "Ah " + battType);
  sendActionPage("Info impianto", "Dati impianto salvati: batteria " + sysV + " V / " + battAh + " Ah / " + battType + ".", 2, "/plant-info");
}

void handleThresholdsPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Soglie alert");
  html += "<h1>Soglie alert</h1><p><a href='/'>Dashboard</a> &middot; <a href='/alerts'>Alert</a> &middot; <a href='/health'>Health</a></p>";
  html += "<form method='POST' action='/thresholds-save'><div class='card'><div class='t'>Soglie configurabili</div>";
  html += "<p>Batteria impianto bassa sotto V<br><input name='battlow' type='number' step='0.01' value='" + String(thresholdBattLow(),2) + "'></p>";
  html += "<p>Batteria ESP bassa sotto %<br><input name='esplow' type='number' step='1' value='" + String(thresholdEspBatLow(),0) + "'></p>";
  html += "<p>WiFi debole sotto dBm<br><input name='wifi' type='number' step='1' value='" + String(thresholdWifiWeak()) + "'></p>";
  html += "<p>No VE.Direct dopo secondi<br><input name='ved' type='number' step='1' value='" + String(thresholdNoVedirectSec()) + "'></p>";
  html += "<p>SD quasi piena sopra %<br><input name='sdpct' type='number' step='1' value='" + String(thresholdSdFullPercent()) + "'></p>";
  html += "<p><button class='button' type='submit'>Salva soglie</button></p></div></form>";
  html += card("Alert attivi ora", String(alertCountNow()), "Health score: " + String(healthScoreNow()) + "/100");
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleThresholdsSave() {
  if (!requireAuth()) return;
  prefs.putFloat("thr_batt_low", server.arg("battlow").toFloat());
  prefs.putFloat("thr_esp_low", server.arg("esplow").toFloat());
  prefs.putInt("thr_wifi", server.arg("wifi").toInt());
  prefs.putInt("thr_ved_sec", server.arg("ved").toInt());
  prefs.putInt("thr_sd_pct", server.arg("sdpct").toInt());
  addEventLog("CFG", "Soglie alert aggiornate");
  sendActionPage("Soglie", "Soglie alert salvate.", 2, "/thresholds");
}

void handleHealthPage() {
  if (!requireAuth()) return;
  int hs = healthScoreNow();
  String html = htmlHeader("Health Score");
  html += "<h1>Health Score</h1><p><a href='/'>Dashboard</a> &middot; <a href='/alerts'>Alert</a> &middot; <a href='/thresholds'>Soglie</a> &middot; <a href='/power'>Power</a></p>";
  html += "<div class='batHero'><div class='t'>Sistema</div><div class='batValue'>" + String(hs) + "/100</div><div class='e'>" + healthStatusText() + "</div><div class='batGauge'><div class='batFill' style='width:" + String(hs) + "%'></div></div></div>";
  html += "<div class='grid'>";
  html += card("WiFi", WiFi.status()==WL_CONNECTED?"OK":"NO", "RSSI " + String(WiFi.RSSI()) + " dBm");
  html += card("VE.Direct", victronOnline()?"OK":"No Data", "Ultimo dato: " + String(victronSeen ? ((millis()-lastVictronMs)/1000UL) : 0) + " s fa");
  html += card("BAT ESP", String(isnan(espBatteryPercent())?0:espBatteryPercent(),0) + "%", espBatteryStatusText());
  html += card("SD", sdMounted?"OK":"NO", sdMounted?("Libera " + formatBytes64(SD.totalBytes()-SD.usedBytes())):esc(sdLastStatus));
  html += card("Heap", String(ESP.getFreeHeap()), ESP.getFreeHeap()<45000?"Attenzione memoria bassa":"OK");
  html += card("Ultimo reboot", resetReasonText(), powerHealthText());
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleDiagnosticSnapshot() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  if (!sdMounted) { sendActionPage("Snapshot diagnostico", "SD non pronta: " + sdLastStatus, 3, "/files"); return; }
  sdCreateBaseDirs();
  String folder = "/diagnostic/snapshot_" + String(millis()/1000UL);
  sdEnsureDir(folder);
  struct Pair { const char* name; String data; } files[] = {
    {"system.json", systemHealthJson()},
    {"alerts.json", String("{\"alert_count\":") + String(alertCountNow()) + "}"},
    {"battery.json", String("{\"voltage\":") + String(isnan(espBatteryVoltage())?0:espBatteryVoltage(),3) + ",\"percent\":" + String(isnan(espBatteryPercent())?0:espBatteryPercent(),0) + "}"},
    {"sd.json", sdInfoJson()},
    {"reboot.txt", resetReasonText() + "\n" + powerHealthText()},
    {"settings.json", settingsBackupJson()}
  };
  for (auto &p : files) { File f = SD.open(folder + "/" + p.name, FILE_WRITE); if (f) { f.print(p.data); f.close(); } }
  addEventLog("SNAP", "Snapshot diagnostico salvato: " + folder);
  sendActionPage("Snapshot diagnostico", "Creato in " + folder, 4, "/sd-files?p=" + folder);
}

void handleBackupRecoveryListPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Ripristino Backup");
  html += "<h1>Ripristino Backup / Recovery</h1><p><a href='/backup-recovery'>Backup / Recovery</a> &middot; <a href='/sd-files?p=/backup_recovery'>File backup</a> &middot; <a class='button danger' href='/sd-retention?apply=1' onclick=\"return confirm('Pulire ora i backup vecchi mantenendo gli ultimi 5 recovery e 5 giornalieri?')\">Pulisci vecchi 5/5</a></p>";
  if (!sdMounted) {
    html += card("MicroSD", "NO", esc(sdLastStatus));
  } else {
    File dir = SD.open("/backup_recovery");
    if (!dir || !dir.isDirectory()) html += card("Backup", "Nessuno", "Cartella /backup_recovery non presente");
    else {
      html += "<div class='card'><div class='t'>Backup disponibili</div><table class='dataTable'><tr><th>Cartella</th><th>Azione</th></tr>";
      File e = dir.openNextFile();
      while (e) {
        if (e.isDirectory()) {
          String p = String(e.name()); if (!p.startsWith("/")) p = "/backup_recovery/" + p;
          html += "<tr><td>" + esc(p) + "</td><td><a class='button' href='/backup-restore?p=" + urlEncode(p) + "'>Apri restore</a></td></tr>";
        }
        e.close(); e = dir.openNextFile(); yield();
      }
      html += "</table></div>";
      dir.close();
    }
  }
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBackupRecoveryRestorePage() {
  if (!requireAuth()) return;
  String p = server.arg("p");
  if (!safeSdBrowserPath(p) || !p.startsWith("/backup_recovery")) { sendActionPage("Restore", "Percorso non valido.", 3, "/backup-list"); return; }
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Restore backup");
  html += "<h1>Restore backup</h1><p><a href='/backup-list'>Lista backup</a> &middot; <a href='/sd-files?p=" + urlEncode(p) + "'>Apri cartella</a></p>";
  html += card("Backup", esc(p), "Il ripristino firmware scrive firmware.bin da SD nella partizione OTA e riavvia.");
  html += card("Config", SD.exists(p + "/config.json") ? "Presente" : "Assente", "Scarica/apri il file dalla cartella backup per verifica.");
  html += card("Firmware", SD.exists(p + "/firmware.bin") ? "Presente" : "Assente", "Usalo solo se devi tornare a una versione precedente funzionante.");
  html += "<div class='card warn'><div class='t'>Conferma richiesta</div><p>Ripristinare firmware da SD e riavviare?</p><p><a class='button danger' href='/backup-restore-start?p=" + urlEncode(p) + "&confirm=YES' onclick=\"return confirm('Ripristinare firmware da questo backup?')\">Ripristina firmware.bin</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBackupRecoveryRestoreStart() {
  if (!requireAuth()) return;
  String p = server.arg("p");
  if (server.arg("confirm") != "YES" || !safeSdBrowserPath(p) || !p.startsWith("/backup_recovery")) { sendActionPage("Restore", "Conferma o percorso non valido.", 3, "/backup-list"); return; }
  if (!sdMounted) sdMount(false);
  String fw = p + "/firmware.bin";
  if (!sdMounted || !SD.exists(fw)) { sendActionPage("Restore", "firmware.bin non trovato nel backup.", 3, "/backup-list"); return; }
  File f = SD.open(fw, FILE_READ);
  if (!f) { sendActionPage("Restore", "Impossibile aprire firmware.bin.", 3, "/backup-list"); return; }
  size_t size = f.size();
  if (!Update.begin(size)) { f.close(); sendActionPage("Restore", "Update.begin fallito: " + String(Update.errorString()), 4, "/backup-list"); return; }
  size_t written = Update.writeStream(f);
  bool ok = (written == size) && Update.end(true);
  f.close();
  if (!ok) { sendActionPage("Restore", "Restore fallito: " + String(Update.errorString()), 5, "/backup-list"); return; }
  addEventLog("RESTORE", "Firmware ripristinato da " + p);
  String html = htmlHeader("Restore completato");
  html += "<meta http-equiv='refresh' content='4;url=/'>";
  html += "<h1>Restore completato</h1><p>Firmware scritto correttamente. Riavvio in corso...</p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(800);
  ESP.restart();
}

void handlePowerPage() {
  if (!requireAuth()) return;
  float ev = espBatteryVoltage();
  float ep = espBatteryPercent();
  String html = htmlHeader("Power / Alimentazione");
  html += "<h1>Power / Alimentazione</h1><p><a href='/'>Dashboard</a> &middot; <a href='/battery'>Batteria ESP</a> &middot; <a href='/alerts'>Alert</a> &middot; <a href='/setup-check'>Setup</a></p>";
  html += "<div class='gxHero'><div class='gxTop'><div><div class='gxTitle'>Stato alimentazione</div><div class='gxSub'>Diagnostica alimentazione, reboot e consumi</div></div><div class='gxPills'><span class='gxPill " + String(esp_reset_reason()==ESP_RST_BROWNOUT?"warn":"ok") + "'>" + esc(resetReasonText()) + "</span><span class='gxPill ok'>LCD auto " + displayAutoOffText() + "</span></div></div>";
  html += "<div class='gxGrid'><div class='gxBig bat'><div class='gxLabel'>BAT ESP / LiPo</div><div class='gxValue'>" + String(isnan(ev)?0:ev,2) + " V</div><div class='gxBar'><div class='gxBarFill' style='width:" + String((int)constrain(isnan(ep)?0:ep,0,100)) + "%'></div></div><div class='gxDetails'><div class='gxMini'><span>Carica</span><b>" + String(isnan(ep)?0:ep,0) + "%</b></div><div class='gxMini'><span>Stato</span><b>" + esc(espBatteryStatusText()) + "</b></div></div></div>";
  html += "<div class='gxSide'><div class='gxStatus'><div class='gxLabel'>Ultimo riavvio</div><div class='state'>" + esc(resetReasonText()) + "</div></div><div class='gxStatus'><div class='gxLabel'>Boot count</div><div class='state'>" + String(bootCounter) + "</div></div><div class='gxStatus'><div class='gxLabel'>Uptime</div><div class='state'>" + uptimeText() + "</div></div></div></div></div>";
  html += "<div class='grid'>";
  html += card("Diagnosi", esp_reset_reason()==ESP_RST_BROWNOUT ? "Brownout" : "OK", powerHealthText());
  html += card("Consiglio alimentazione", "Buck 12V -> 5V", "Non alimentare ESP32+TFT dal 5V VE.Direct. Usa buck 5V 2A/3A e VE.Direct solo dati.");
  html += card("Consumi", "LCD OFF", "Auto spegnimento: " + displayAutoOffText() + "<br><a class='button' href='/settings'>Impostazioni display</a>");
  html += card("VE.Direct", victronOnline()?"OK":"No Data", "Ultimo dato: " + String(victronSeen ? ((millis()-lastVictronMs)/1000UL) : 0) + " s fa");
  html += card("Shutdown software", "Disponibile", "Spegne WiFi, WebUI e display. La scheda resta alimentata.<br><a class='button danger' href='/shutdown'>Apri spegnimento remoto</a>");
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void enterSoftwareShutdown(uint32_t sleepMinutes, bool wakeByTimer, const String& reason) {
  addEventLog("SHUTDOWN", reason + (wakeByTimer ? (" per " + String(sleepMinutes) + " min") : " fino a reset"));
  prefs.begin("victron", false);
  prefs.putString("last_shutdown_reason", reason);
  prefs.putString("last_shutdown_mode", wakeByTimer ? "timer" : "reset");
  prefs.putUInt("last_shutdown_minutes", sleepMinutes);
  prefs.putString("last_shutdown_fw", FW_VERSION);
  prefs.putULong("last_shutdown_uptime_s", millis() / 1000UL);
  prefs.end();

  setBacklight(false);
  delay(150);

  if (sdMounted) {
    SD.end();
    sdMounted = false;
  }

  ArduinoOTA.end();
  server.stop();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(300);

  if (wakeByTimer && sleepMinutes > 0) {
    uint64_t us = (uint64_t)sleepMinutes * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(us);
  }

  esp_deep_sleep_start();
}

void handleShutdownPage() {
  if (!requireAuth()) return;

  prefs.begin("victron", true);
  String lastMode = prefs.getString("last_shutdown_mode", "N/D");
  String lastReason = prefs.getString("last_shutdown_reason", "N/D");
  uint32_t lastMin = prefs.getUInt("last_shutdown_minutes", 0);
  String lastFw = prefs.getString("last_shutdown_fw", "N/D");
  unsigned long lastUp = prefs.getULong("last_shutdown_uptime_s", 0);
  prefs.end();

  String html = htmlHeader("Shutdown software");
  html += "<h1>Shutdown software remoto</h1><p><a href='/power'>Power</a> &middot; <a href='/settings'>Settings</a> &middot; <a href='/'>Dashboard</a></p>";
  html += "<div class='card warn'><div class='t'>Attenzione</div>";
  html += "<p>Questo comando spegne il firmware: WiFi, WebUI, TFT/backlight e lettura VE.Direct vengono fermati e l'ESP32 entra in deep sleep.</p>";
  html += "<p><b>La scheda resta elettricamente alimentata</b> se USB-C, buck o LiPo sono collegati. Per togliere corrente serve staccare alimentazione o usare hardware esterno.</p>";
  html += "</div>";

  html += "<div class='grid'>";
  html += card("Shutdown con timer", "Consigliato", "Scegli i minuti. La CYD torna online da sola dopo il tempo impostato.");
  html += card("Shutdown fino a reset", "Avanzato", "La CYD resta non raggiungibile finche' non premi RESET o togli/rimetti alimentazione.");
  html += card("Ultimo shutdown", esc(lastMode), "Motivo: " + esc(lastReason) + "<br>Timer: " + String(lastMin) + " min<br>Firmware: " + esc(lastFw) + "<br>Uptime precedente: " + String(lastUp) + " s");
  html += "</div>";

  html += "<div class='card'><div class='t'>Spegni con timer</div>";
  html += "<form method='get' action='/shutdown-timed' onsubmit=\"return confirm('Spegnere la CYD per il tempo scelto? La WebUI non sara disponibile fino al risveglio.')\">";
  html += "<p>Minuti: <select name='min'>";
  for (int m = 1; m <= 30; m++) {
    html += "<option value='" + String(m) + "'" + String(m==5 ? " selected" : "") + ">" + String(m) + " min</option>";
  }
  const int extraMins[] = {45,60,90,120,180,240,360,480,720,1440};
  for (size_t i=0; i<sizeof(extraMins)/sizeof(extraMins[0]); i++) {
    int m = extraMins[i];
    String label = (m < 60) ? (String(m) + " min") : (String(m/60) + (m%60==0 ? " h" : " h " + String(m%60) + " min"));
    html += "<option value='" + String(m) + "'>" + label + "</option>";
  }
  html += "</select> <button type='submit'>Spegni con timer</button></p></form>";
  html += "<p class='e'>Puoi usare 1, 2, 3, 4, 5 minuti e valori superiori. Limite massimo: 1440 minuti.</p>";
  html += "</div>";

  html += "<div class='card danger'><div class='t'>Spegni fino a reset</div>";
  html += "<p>Usalo solo se sei sicuro: dopo questo comando non potrai riaccenderla da remoto.</p>";
  html += "<p><a class='button danger' href='/shutdown-reset' onclick=\"return confirm('Confermi shutdown fino a reset? La CYD non sara piu raggiungibile finche non fai reset fisico o togli/rimetti alimentazione.')\">Spegni fino a reset</a></p>";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleShutdownTimed() {
  if (!requireAuth()) return;
  int minutes = server.hasArg("min") ? server.arg("min").toInt() : 5;
  if (minutes < 1) minutes = 1;
  if (minutes > 1440) minutes = 1440;

  String html = htmlHeader("Shutdown con timer");
  html += "<meta http-equiv='refresh' content='" + String((minutes * 60) + 20) + ";url=/'>";
  html += "<h1>Shutdown con timer</h1>";
  html += "<p>La CYD entra in deep sleep per <b>" + String(minutes) + " minuti</b>.</p>";
  html += "<p>La WebUI non sara' raggiungibile fino al risveglio automatico.</p>";
  html += "<p>La scheda resta alimentata: e' uno spegnimento software, non elettrico.</p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(1200);
  enterSoftwareShutdown((uint32_t)minutes, true, "Shutdown remoto con timer");
}

void handleShutdownReset() {
  if (!requireAuth()) return;
  String html = htmlHeader("Shutdown fino a reset");
  html += "<h1>Shutdown fino a reset</h1>";
  html += "<p>La CYD entra in deep sleep senza timer di risveglio.</p>";
  html += "<p><b>Per riaccenderla servira' reset fisico o stacco/riattacco alimentazione.</b></p>";
  html += "<p>La scheda resta alimentata: e' uno spegnimento software, non elettrico.</p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(1200);
  enterSoftwareShutdown(0, false, "Shutdown remoto fino a reset");
}

void handleSdMaintenancePage() {
  if (!requireAuth()) return;
  if (!sdMounted && server.hasArg("mount")) sdMount(false);
  uint64_t logsBytes = sdMounted ? sdDirBytes("/logs") : 0;
  uint64_t backupBytes = sdMounted ? sdDirBytes("/backup_recovery") : 0;
  int logEntries = sdMounted ? sdCountEntries("/logs", true) : 0;
  int backupEntries = sdMounted ? sdCountEntries("/backup_recovery", true) : 0;
  String html = htmlHeader("Manutenzione SD");
  html += "<h1>Manutenzione microSD</h1><p><a href='/sd'>MicroSD</a> &middot; <a href='/storage'>Storage</a> &middot; <a href='/sd-files'>File SD</a> &middot; <a href='/sd-logs'>Log SD</a></p>";
  html += "<div class='grid'>";
  html += card("Stato SD", sdMounted?"Montata":"Smontata", sdMounted ? ("Libera: " + formatBytes64(SD.totalBytes()-SD.usedBytes()) + "<br>Usata: " + formatBytes64(SD.usedBytes())) : (esc(sdLastStatus) + "<br><a class='button' href='/sd-maintenance?mount=1'>Monta SD</a>"));
  html += card("Log", String(logEntries) + " elementi", "Spazio usato: " + formatBytes64(logsBytes) + "<br><a class='button' href='/sd-files?p=/logs'>Apri /logs</a>");
  html += card("Backup Recovery", String(backupEntries) + " elementi", "Spazio usato: " + formatBytes64(backupBytes) + "<br><a class='button' href='/sd-files?p=/backup_recovery'>Apri backup</a>");
  html += card("CSV oggi", sdMounted?"Disponibile":"N/D", "<a class='button' href='/sd-log.csv'>Scarica CSV oggi</a>");
  html += "</div>";
  html += "<div class='card warn'><div class='t'>Pulizia sicura</div><p>Per ora la pulizia automatica elimina solo i log CSV vecchi mantenendo backup e configurazioni.</p>";
  html += "<p><a class='button danger' href='/sd-clean-old-logs' onclick=\"return confirm('Eliminare i log SD vecchi? I backup non saranno toccati.')\">Elimina log vecchi</a> <a class='button' href='/sd-format'>Svuota/Formattazione rapida SD</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSdMaintenanceCleanLogs() {
  if (!requireAuth()) return;
  if (!sdMounted) {
    sendActionPage("Manutenzione SD", "MicroSD non montata. Montala prima da /sd.", 3, "/sd-maintenance");
    return;
  }
  // Pulizia prudente: conserva il file di oggi e rimuove gli altri CSV sotto /logs.
  String keep = sdLogFileName();
  int removed = 0;
  File root = SD.open("/logs");
  if (root && root.isDirectory()) {
    File month = root.openNextFile();
    while (month) {
      if (month.isDirectory()) {
        String mpath = String(month.name());
        File d = SD.open(mpath);
        if (d && d.isDirectory()) {
          File f = d.openNextFile();
          while (f) {
            String fp = String(f.name());
            f.close();
            if (fp.endsWith(".csv") && fp != keep) { if (SD.remove(fp)) removed++; }
            f = d.openNextFile();
          }
          d.close();
        }
      }
      month.close();
      month = root.openNextFile();
    }
    root.close();
  }
  addEventLog("SD", "Pulizia log vecchi: " + String(removed) + " file eliminati");
  sendActionPage("Manutenzione SD", "Eliminati " + String(removed) + " log vecchi. Backup e configurazioni non toccati.", 3, "/sd-maintenance");
}

void handleBatteryPage() {
  if (!requireAuth()) return;
  float v = espBatteryVoltage();
  float pct = espBatteryPercent();
  float pinV = adcPinVoltage(34);
  String st = espBatteryStatusText();
  int w = (int)constrain(isnan(pct) ? 0 : pct, 0, 100);
  String color = "#7ee787";
  String heroClass = "batHero";
  if (st == "Critica") { color = "#ff7b72"; heroClass += " batBad"; }
  else if (st == "Bassa") { color = "#f2cc60"; heroClass += " batWarn"; }

  String html = htmlHeader("Batteria ESP");
  html += "<div class='top'><h1>Batteria ESP / LiPo tampone</h1>";
  html += "<div class='sub'>Monitor della batteria tampone della scheda ESP32/CYD. Lettura GPIO34, moltiplicatore 2.14, PWM luminosita' disattivato.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/battery'>Batteria ESP</a><a href='/bat-scan'>BAT scan</a><a href='/battery.json'>JSON</a><a href='/settings'>Impostazioni</a><a href='/system-pro'>Sistema</a></div></div>";

  html += "<div class='" + heroClass + "'>";
  html += "<div class='t'>Stato batteria tampone</div>";
  html += "<div class='batIcon'><span style='width:" + String(w) + "%;background:" + color + "'></span></div>";
  html += "<div class='batValue' style='color:" + color + "'>" + String(isnan(v) ? 0 : v, 2) + " V</div>";
  html += "<div class='e' style='font-size:18px'>Carica stimata <b>" + String(isnan(pct) ? 0 : pct, 0) + "%</b> &nbsp; Stato: <b style='color:" + color + "'>" + st + "</b></div>";
  html += "<div class='batGauge' style='margin-top:16px'><div class='batFill' style='width:" + String(w) + "%;background:linear-gradient(90deg," + color + ",#7ee787)'></div></div>";
  html += "<div class='batGrid'>";
  html += "<div class='batMini'><div class='t'>GPIO</div><b>34</b></div>";
  html += "<div class='batMini'><div class='t'>ADC pin</div><b>" + String(pinV, 3) + " V</b></div>";
  html += "<div class='batMini'><div class='t'>Moltiplicatore</div><b>" + String(espBatteryMultiplier(), 3) + "x</b></div>";
  html += "<div class='batMini'><div class='t'>LiPo</div><b>" + esc(espBatteryConnectionText()) + "</b></div>";
  html += "</div><p><a class='button' href='/battery-installed?state=on'>Segna LiPo collegata</a> <a class='button' href='/battery-installed?state=off'>Segna non collegata</a></p></div>";

  html += "<div class='grid' style='margin-top:12px'>";
  html += card("Tensione LiPo", String(isnan(v) ? 0 : v, 2) + " V", "Stimata dal partitore collegato a GPIO34.");
  html += card("Percentuale", String(isnan(pct) ? 0 : pct, 0) + "%", "Stima indicativa. Da calibrare con LiPo reale montata.");
  html += card("Stato", st, "Soglie: Piena / OK / Bassa / Critica.<br>LiPo: " + esc(espBatteryConnectionText()));
  html += card("Diagnostica", "BAT scan", "<a class='button' href='/bat-scan'>Apri scansione ADC</a>");
  html += "</div>";

  html += "<div class='card warn'><div class='t'>Sicurezza LiPo</div><div class='e'>Usa solo LiPo 1S 3.7 V, massimo 4.2 V. Verifica sempre polarita' BAT+ e BAT-. Non collegare batterie 2S/7.4 V.</div></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBatteryInstalledToggle() {
  if (!requireAuth()) return;
  String st = server.arg("state");
  prefs.begin("victron", false);
  if (st == "on") {
    prefs.putBool("esp_bat_installed", true);
    addEventLog("BAT", "LiPo segnata come collegata");
    prefs.end();
    sendActionPage("Batteria ESP", "LiPo segnata come collegata manualmente.", 2, "/battery");
    return;
  }
  if (st == "off") {
    prefs.putBool("esp_bat_installed", false);
    addEventLog("BAT", "LiPo segnata come non collegata");
    prefs.end();
    sendActionPage("Batteria ESP", "LiPo segnata come non collegata/non confermata.", 2, "/battery");
    return;
  }
  prefs.end();
  sendActionPage("Batteria ESP", "Parametro state non valido.", 2, "/battery");
}

void handleBatteryJson() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  float v = espBatteryVoltage();
  float pct = espBatteryPercent();
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"source_gpio\":" + String(pubCfg.espBatteryAdcPin) + ",";";
  j += "\"multiplier\":" + String(espBatteryMultiplier(), 4) + ",";
  j += "\"adc_pin_voltage\":" + String(pubCfg.espBatteryAdcPin < 0 ? 0 : adcPinVoltage(pubCfg.espBatteryAdcPin), 3) + ",";
  j += "\"lipo_voltage\":" + String(isnan(v) ? 0 : v, 3) + ",";
  j += "\"percent\":" + String(isnan(pct) ? 0 : pct, 0) + ",";
  j += "\"status\":\"" + espBatteryStatusText() + "\",";
  j += "\"connection\":\"" + espBatteryConnectionText() + "\",";
  j += "\"note\":\"Stima su GPIO34 con moltiplicatore 2.14, da calibrare con LiPo reale\"";
  j += "}";
  sendJsonPretty(j);
}

void handleBatScanPage() {
  if (!requireAuth()) return;
  const int pins[] = {32, 33, 34, 35, 36, 39};
  String html = htmlHeader("BAT Scan ESP");
  html += "<div class='top'><h1>Batteria ESP / BAT Scan</h1>";
  html += "<div class='sub'>Diagnostica per capire se la LiPo tampone della scheda e' leggibile da firmware. Senza batteria installata, cerca un pin stabile compatibile con il nodo BAT/carica.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/battery'>Batteria ESP</a><a href='/settings'>Impostazioni</a><a href='/system-pro'>Sistema</a><a href='/battery.json'>JSON</a></div></div>";
  html += "<div class='card warn'><div class='t'>Come leggere questa pagina</div><div class='e'>Hai misurato circa 4.158 V sul connettore BAT. Se la scheda ha un partitore 1:1, il pin ADC corretto dovrebbe leggere circa 2.08 V nella colonna PIN V e circa 4.16 V nella colonna LiPo x2. Se nessun pin ha senso, la BAT non e' collegata a un ADC e servira' un partitore esterno.</div></div>";
  html += "<div class='card'><div class='t'>Scansione ADC</div><div class='tablewrap'><table class='hist'><tr><th>GPIO</th><th>Raw</th><th>Pin V</th><th>LiPo stimata x2</th><th>% stimata</th><th>Nota</th></tr>";
  for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
    int pin = pins[i];
    uint32_t sumRaw = 0;
    for (int s = 0; s < 24; s++) { sumRaw += analogRead(pin); delay(1); }
    float raw = sumRaw / 24.0f;
    float pinV = (raw / 4095.0f) * 3.30f;
    float lipo2 = pinV * 2.0f;
    float pct2 = lipoPercentFromVoltage(lipo2);
    String note = "";
    if (lipo2 > 3.6f && lipo2 < 4.35f) note = "Possibile BAT con partitore x2";
    else if (pinV < 0.08f) note = "Basso / non collegato";
    else if (pinV > 3.15f) note = "Alto / pull-up / non BAT x2";
    else note = "Da verificare";
    html += "<tr><td>GPIO" + String(pin) + "</td><td>" + String(raw,0) + "</td><td>" + String(pinV,3) + " V</td><td>" + String(lipo2,3) + " V</td><td>" + String(isnan(pct2) ? 0 : pct2,0) + "%</td><td>" + note + "</td></tr>";
  }
  html += "</table></div></div>";
  html += "<div class='card good'><div class='t'>Prossimo test</div><div class='e'>Quando installerai la LiPo, ricarica questa pagina: il GPIO corretto dovra' cambiare lentamente con la tensione batteria. Non collegare batterie 2S/7.4 V. Solo LiPo 1S 3.7 V, max 4.2 V, polarita' verificata.</div></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void sendActionPage(const String& title, const String& message, int refreshSeconds = 6, const String& target = "/") {
  String html;
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='" + String(refreshSeconds) + ";url=" + target + "'>";
  html += "<style>";
  html += "body{font-family:Arial;background:#0d1117;color:#e6edf3;padding:25px}";
  html += ".box{background:#161b22;border:1px solid #30363d;border-radius:18px;padding:20px;max-width:520px}";
  html += ".ok{color:#7ee787;font-size:25px;font-weight:bold}";
  html += "a{color:#79c0ff}";
  html += "</style></head><body>";
  html += "<div class='box'><div class='ok'>" + title + "</div><p>" + message + "</p>";
  html += "<p>Ritorno automatico tra " + String(refreshSeconds) + " secondi...</p>";
  html += "<p><a href='" + target + "'>Torna ora</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}




void handleApiLive() { handleJson(); }
void handleApiHistory() { handleHistory(); }
void handleApiSystem() {
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"name\":\"" + String(FW_NAME) + "\",";
  j += "\"build\":\"" + buildText() + "\",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"uptime\":\"" + uptimeText() + "\",";
  j += "\"littlefs\":" + fsInfoJson() + ",";
  j += "\"ota_partitions\":" + otaPartitionJson() + ",";
  j += "\"rollback\":\"" + esc(prefGet("rollback_status", rollbackStatus)) + "\"";
  j += "}";
  sendJsonPretty(j);
}


String alertLevelClass(const String& level) {
  if (level == "OK") return "ok";
  if (level == "WARN") return "warn";
  return "bad";
}

int alertCountNow() {
  int n = 0;
  if (WiFi.status() != WL_CONNECTED) n++;
  if (WiFi.status() == WL_CONNECTED && WiFi.RSSI() < thresholdWifiWeak()) n++;
  if (!victronOnline() && (millis() - lastVictronMs) / 1000UL > (unsigned long)thresholdNoVedirectSec()) n++;
  if (errorState != "0" && errorState != "N/D") n++;
  float ep = espBatteryPercent();
  if (!isnan(ep) && ep < thresholdEspBatLow()) n++;
  if (!isnan(battV) && battV > 1 && battV < thresholdBattLow()) n++;
  if (sdMounted && SD.totalBytes() > 0) { int usedPct = (int)((SD.usedBytes() * 100ULL) / SD.totalBytes()); if (usedPct >= thresholdSdFullPercent()) n++; }
  if (!littleFsReady) n++;
  if (ESP.getFreeHeap() < 45000) n++;
  return n;
}

String systemHealthJson() {
  String j = "{";
  j += "\"wifi_ok\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  j += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"victron_ok\":" + String(victronOnline() ? "true" : "false") + ",";
  j += "\"mppt_error\":\"" + esc(errorState) + "\",";
  j += "\"esp_battery_voltage\":" + String(isnan(espBatteryVoltage()) ? 0 : espBatteryVoltage(), 3) + ",";
  j += "\"esp_battery_percent\":" + String(isnan(espBatteryPercent()) ? 0 : espBatteryPercent(), 0) + ",";
  j += "\"littlefs_ok\":" + String(littleFsReady ? "true" : "false") + ",";
  j += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"alert_count\":" + String(alertCountNow()) + ",";
  j += "\"health_score\":" + String(healthScoreNow()) + ",";
  j += "\"health_status\":\"" + healthStatusText() + "\"";
  j += "}";
  return j;
}

String alertRow(const String& name, const String& state, const String& detail) {
  String cls = alertLevelClass(state);
  String label = state == "OK" ? "OK" : (state == "WARN" ? "Attenzione" : "Critico");
  String h;
  h += "<tr><td>" + name + "</td><td><span class='status " + cls + "'>" + label + "</span></td><td>" + detail + "</td></tr>";
  return h;
}

void handleAlertsJson() {
  if (!requireAuth()) return;
  String j = "{";
  j += "\"alert_count\":" + String(alertCountNow()) + ",";
  j += "\"wifi_ok\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  j += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"victron_ok\":" + String(victronOnline() ? "true" : "false") + ",";
  j += "\"last_victron_seconds\":" + String((millis() - lastVictronMs) / 1000UL) + ",";
  j += "\"mppt_error\":\"" + esc(errorState) + "\",";
  j += "\"esp_battery_percent\":" + String(isnan(espBatteryPercent()) ? 0 : espBatteryPercent(), 0) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap());
  j += "}";
  sendJsonPretty(j);
}

void handleAlertsPage() {
  if (!requireAuth()) return;
  float ep = espBatteryPercent();
  String html = htmlHeader("Alert sistema");
  html += "<div class='top'><h1>Alert & Stato sistema</h1><div class='sub'>Controllo rapido di VE.Direct, WiFi, batteria ESP, filesystem e stabilita'.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/setup-check'>Setup guidato</a><a href='/thresholds'>Soglie</a><a href='/health'>Health</a><a href='/battery'>Batteria ESP</a><a href='/alerts.json'>JSON Alert</a></div></div>";
  html += "<div class='grid'>";
  html += card("Alert attivi", String(alertCountNow()), alertCountNow() == 0 ? "Tutto regolare" : "Controlla la tabella sotto");
  html += card("VE.Direct", victronOnline() ? "OK" : "No Data", "Ultimo dato: " + String((millis() - lastVictronMs) / 1000UL) + " s fa");
  html += card("WiFi", WiFi.status() == WL_CONNECTED ? "OK" : "Offline", "RSSI: " + String(WiFi.RSSI()) + " dBm");
  html += card("BAT ESP", String(isnan(ep) ? 0 : ep, 0) + "%", "Stato: " + espBatteryStatusText() + "<br>LiPo: " + espBatteryConnectionText());
  html += card("MicroSD", sdMounted ? "OK" : "NO", sdMounted ? ("Libera: " + formatBytes64(SD.totalBytes() - SD.usedBytes())) : esc(sdLastStatus));
  html += "</div>";
  html += "<div class='card'><div class='t'>Dettaglio alert</div><table class='dataTable'>";
  html += alertRow("WiFi", WiFi.status() == WL_CONNECTED ? (WiFi.RSSI() < thresholdWifiWeak() ? "WARN" : "OK") : "BAD", WiFi.status() == WL_CONNECTED ? ("RSSI " + String(WiFi.RSSI()) + " dBm") : "Non connesso");
  html += alertRow("VE.Direct", victronOnline() ? "OK" : "WARN", victronOnline() ? "Dati ricevuti" : "Nessun dato recente dal Victron");
  html += alertRow("Errore MPPT", (errorState == "0" || errorState == "N/D") ? "OK" : "BAD", "ERR=" + esc(errorState));
  html += alertRow("Batteria ESP", (!isnan(ep) && ep < thresholdEspBatLow()) ? "WARN" : "OK", String(isnan(espBatteryVoltage()) ? 0 : espBatteryVoltage(), 2) + " V / " + String(isnan(ep) ? 0 : ep,0) + "%");
  html += alertRow("LittleFS", littleFsReady ? "OK" : "WARN", littleFsReady ? "Filesystem pronto" : "Storico persistente non disponibile");
  html += alertRow("Memoria", ESP.getFreeHeap() < 45000 ? "WARN" : "OK", "Heap libero " + String(ESP.getFreeHeap()) + " byte");
  html += alertRow("MicroSD", sdMounted ? "OK" : (storageUseSd()?"WARN":"OK"), sdMounted ? ("Montata, libera " + formatBytes64(SD.totalBytes() - SD.usedBytes())) : (storageUseSd()?esc(sdLastStatus):"Storage SD non richiesto"));
  html += alertRow("LiPo ESP", espBatteryConnectionText().indexOf("collegata") >= 0 ? "OK" : "WARN", espBatteryConnectionText());
  html += alertRow("Logger SD", (storageUseSd() && lastSdLogOkMs == 0) ? "WARN" : "OK", esc(lastSdLogStatus));
  html += "</table></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleEnergyTodayPage() {
  if (!requireAuth()) return;
  HistorySlot *cur = (currentDayIndex >= 0) ? &daily[currentDayIndex] : nullptr;
  float histWh = cur ? cur->wh : 0;
  float yWh = isnan(yieldTodayKWh) ? 0 : yieldTodayKWh * 1000.0f;
  float todayWh = histWh > 0 ? histWh : yWh;
  float maxW = cur ? cur->maxW : (isnan(maxPowerToday) ? 0 : maxPowerToday);
  float avgW = (cur && cur->samples) ? (cur->panelSum / cur->samples) : 0;
  float battMinSafe = (cur && cur->battMin != 999) ? cur->battMin : 0;
  float battMaxSafe = cur ? cur->battMax : 0;
  float chargeMin = cur ? (cur->samples * 10.0f / 60.0f) : 0;
  String html = htmlHeader("Energia oggi");
  html += "<div class='top'><h1>Energia oggi</h1><div class='sub'>Produzione giornaliera, potenza massima, media e stato prevalente.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/history-gx'>Storico GX</a><a href='/victron-data'>Dati Victron</a><a href='/data-center'>Dati & Storico</a></div></div>";
  html += "<div class='gxHero'><div class='gxTop'><div><div class='gxTitle'>Produzione oggi</div><div class='gxSub'>Aggiornata dallo storico locale</div></div><div class='gxPills'><span class='gxPill " + String(victronOnline()?"ok":"warn") + "'>VE.Direct " + String(victronOnline()?"OK":"No Data") + "</span></div></div>";
  html += "<div class='gxGrid'><div class='gxBig sun'><div class='gxLabel'>Energia stimata</div><div class='gxValue'>" + String(todayWh/1000.0f, 3) + " kWh</div><div class='gxBar'><div class='gxBarFill' style='width:" + String(min(100.0f, todayWh / 12.0f),0) + "%'></div></div><div class='gxDetails'><div class='gxMini'><span>Wh</span><b>" + String(todayWh,0) + "</b></div><div class='gxMini'><span>Stato</span><b>" + esc(chargeState) + "</b></div></div></div>";
  html += "<div class='gxSide'><div class='gxStatus'><div class='gxLabel'>PV Max</div><div class='state'>" + String(maxW,0) + " W</div></div><div class='gxStatus'><div class='gxLabel'>PV Media</div><div class='state'>" + String(avgW,0) + " W</div></div><div class='gxStatus'><div class='gxLabel'>Tempo campionato</div><div class='state'>" + String(chargeMin,0) + " min</div></div></div></div></div>";
  html += "<div class='grid'>";
  html += card("Batteria min/max", String(battMinSafe,2) + " / " + String(battMaxSafe,2) + " V", "Range rilevato nello storico di oggi");
  html += card("MPPT", esc(mpptState), "Errore: " + esc(errorState));
  html += card("PV istantaneo", fmt(panelW,0," W"), "PV " + fmt(panelV,2," V"));
  html += card("Rendimento VE.Direct", fmt(yieldTodayKWh,3," kWh"), "Totale: " + fmt(yieldTotalKWh,2," kWh"));
  html += card("Impianto", prefs.getString("plant_name", "Impianto Victron"), plantInfoSummary());
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBatteryCalPage() {
  if (!requireAuth()) return;
  String msg = "";
  float pinV = adcPinVoltage(34);
  if (server.hasArg("real")) {
    float real = server.arg("real").toFloat();
    if (real > 3.0f && real < 4.35f && pinV > 0.10f) {
      float m = real / pinV;
      saveEspBatteryMultiplier(m);
      msg = "Calibrazione salvata: moltiplicatore " + String(m, 4);
    } else {
      msg = "Valore non valido. Inserisci una tensione LiPo reale tra 3.0 e 4.35 V.";
    }
  }
  float v = espBatteryVoltage();
  float pct = espBatteryPercent();
  String html = htmlHeader("Calibrazione batteria ESP");
  html += "<div class='top'><h1>Calibrazione Batteria ESP</h1><div class='sub'>Inserisci la tensione misurata col tester sui poli BAT. Il firmware calcola il moltiplicatore GPIO34 automaticamente.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/battery'>Batteria ESP</a><a href='/bat-scan'>BAT scan</a></div></div>";
  if (msg.length()) html += "<div class='banner'>" + msg + "</div>";
  html += "<div class='grid'>";
  html += card("Firmware legge", String(isnan(v)?0:v,3) + " V", "Percentuale: " + String(isnan(pct)?0:pct,0) + "%<br>Moltiplicatore: " + String(espBatteryMultiplier(),4));
  html += card("ADC GPIO" + String(pubCfg.espBatteryAdcPin), String(pinV,3) + " V", "Tensione letta sul pin ADC");
  html += "</div>";
  html += "<div class='card'><div class='t'>Nuova calibrazione</div><form method='get' action='/battery-cal'><p>Misura col tester la LiPo/BAT e inserisci il valore reale:</p><input name='real' placeholder='es. 4.158' inputmode='decimal'> <button type='submit'>Calibra</button></form><div class='e'>Solo LiPo 1S 3.7 V, max 4.2 V. Non usare 2S/7.4 V.</div></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleHistoryGxPage() {
  if (!requireAuth()) return;
  String h24 = historyJsonArray(hourly, 24, currentHourIndex < 0 ? 0 : currentHourIndex, "H");
  String d31 = historyJsonArray(daily, 31, currentDayIndex < 0 ? 0 : currentDayIndex, "G");
  String html = htmlHeader("Storico GX");
  html += "<div class='top'><h1>Storico GX</h1><div class='sub'>Grafici web leggeri per produzione PV ed energia.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/energy-today'>Energia oggi</a><a href='/history-compact?type=daily'>Storico classico</a><a href='/api/history?type=daily'>JSON</a></div></div>";
  html += "<div class='grid'><div class='card blue'><div class='t'>PV ultime 24 letture</div><div class='canvasHint'>Barre gialle = P max in W per periodo. Scala pannello: 0-" + String(configuredPanelWatts(),0) + " W.</div><canvas id='c24' width='700' height='260'></canvas></div><div class='card good'><div class='t'>Energia ultimi 31 giorni</div><div class='canvasHint'>Barre verdi = energia giornaliera in Wh. Scala automatica sul periodo.</div><canvas id='c31' width='700' height='260'></canvas></div></div>";
  html += "<div class='card'><div class='t'>Riepilogo</div><div id='gxTip' style='display:none;background:#0b1220;border:1px solid #58a6ff;border-radius:14px;padding:10px;margin-bottom:10px'></div><div id='sumHist' class='e'>Caricamento...</div></div>";
  html += "<script>const h24=" + h24 + ";const d31=" + d31 + ";const configuredPanelW=" + String(configuredPanelWatts(),0) + ";</script>";
  html += R"rawliteral(
<script>
function drawBars(id,data,key,color,scale,label,unit){
 const c=document.getElementById(id),ctx=c.getContext('2d'),w=c.width,h=c.height;
 ctx.clearRect(0,0,w,h); ctx.fillStyle='#0b1220'; ctx.fillRect(0,0,w,h);
 ctx.strokeStyle='#30363d'; ctx.beginPath(); ctx.moveTo(42,10); ctx.lineTo(42,h-30); ctx.lineTo(w-8,h-30); ctx.stroke();
 const vals=data.map(x=>Number(x[key]||0)); const max=Math.max(1,Number(scale)||0,...(scale?[]:vals)); const bw=(w-58)/Math.max(1,data.length);
 ctx.strokeStyle='#1f2937'; ctx.font='12px Arial'; ctx.fillStyle='#8b949e';
 ctx.fillText(label+' 0-'+Math.round(max)+' '+unit,6,20); ctx.fillText(Math.round(max/2)+' '+unit,6,Math.round((h-30+10)/2)); ctx.fillText('0 '+unit,12,h-34);
 data.forEach((x,i)=>{let v=Number(x[key]||0);let bh=Math.max(0,Math.min(1,v/max))*(h-48);let x0=48+i*bw;let y=h-31-bh;ctx.fillStyle=color;ctx.fillRect(x0,y,Math.max(2,bw-3),bh);});
}
function fmtDur(sec){sec=Math.max(0,Number(sec)||0);const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60);if(h>0)return h+'h '+String(m).padStart(2,'0')+'m';return m+'m';}
function pointText(x){const wh=Number(x.wh)||0,mw=Number(x.maxw)||0,mpv=Number(x.maxpv)||0,bmin=Number(x.battmin)||0,bmax=Number(x.battmax)||0,total=Number(x.charge_total_sec)||0;if(wh<=0&&mw<=0&&mpv<=0&&bmin<=0&&bmax<=0&&total<=0)return '<b>'+x.label+'</b><br><span class=\"noData\">Nessun dato registrato.</span>';const batt=(bmin>0&&bmax>0)?(bmin.toFixed(2)+'-'+bmax.toFixed(2)+' V'):'N/D';return '<b>'+x.label+'</b><br>Produzione '+Math.round(wh)+' Wh · P max '+Math.round(mw)+' W<br>PV max '+mpv.toFixed(2)+' V · Batt '+batt+'<br>Bulk '+fmtDur(x.bulk_sec)+' · Ass '+fmtDur(x.absorption_sec)+' · Float '+fmtDur(x.float_sec)+' · Off '+fmtDur(x.off_sec);}
function bindCanvasTip(id,data){const c=document.getElementById(id);if(!c)return;c.onclick=(ev)=>{const r=c.getBoundingClientRect();const x=ev.clientX-r.left;const idx=Math.max(0,Math.min(data.length-1,Math.floor((x/(r.width||1))*data.length)));const tip=document.getElementById('gxTip');tip.innerHTML=pointText(data[idx]||{});tip.style.display='block';};}
function avg(a,k){let vals=a.map(x=>Number(x[k]||0)).filter(x=>x>0);return vals.length?vals.reduce((p,c)=>p+c,0)/vals.length:0;}
drawBars('c24',h24,'maxw','#f2cc60',Math.max(1,Number(configuredPanelW)||120),'Scala pannello','W'); drawBars('c31',d31,'wh','#7ee787',0,'Max periodo','Wh'); bindCanvasTip('c24',h24); bindCanvasTip('c31',d31);
const today=d31[d31.length-1]||{};document.getElementById('sumHist').innerHTML='Tocca una barra per il popup dettagli. Oggi: <b>'+Number(today.wh||0).toFixed(0)+' Wh</b> &nbsp; Max PV: <b>'+Number(today.maxw||0).toFixed(0)+' W</b> &nbsp; Media 31g: <b>'+avg(d31,'wh').toFixed(0)+' Wh</b>';
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSetupCheckPage() {
  if (!requireAuth()) return;
  float ep = espBatteryPercent();
  String html = htmlHeader("Setup guidato");
  html += "<div class='top'><h1>Setup guidato</h1><div class='sub'>Checklist rapida per cablaggio, rete, OTA, display e dati Victron.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/alerts'>Alert</a><a href='/system-pro'>Sistema</a><a href='/diag'>Diagnostica</a><a href='/quick-check'>Check rapido</a></div></div>";
  html += "<div class='grid'>";
  html += card("WiFi", WiFi.status() == WL_CONNECTED ? "OK" : "NO", "IP: " + WiFi.localIP().toString() + "<br>RSSI: " + String(WiFi.RSSI()) + " dBm");
  html += card("VE.Direct", victronOnline() ? "OK" : "No Data", "Usa GND comune + TX Victron verso RX ESP. Non alimentare da VE.Direct 5V.");
  html += card("Display TFT", "OK", "CYD ILI9341, no PWM, boot progress stabile");
  html += card("Touch", touchReady ? "OK" : "OFF/Guard", "Safe guard attivo. <a class='button' href='/touch-reset'>Riattiva touch</a>");
  html += card("BAT ESP", String(isnan(ep)?0:ep,0) + "%", "GPIO" + String(pubCfg.espBatteryAdcPin) + ", moltiplicatore " + String(espBatteryMultiplier(),3) + "<br><a class='button' href='/battery-cal'>Calibra</a>");
  html += card("OTA", "OK", "Firmware: " + String(FW_VERSION) + "<br><a class='button' href='/updates'>Aggiornamenti</a>");
  html += card("LittleFS", littleFsReady ? "OK" : "NO", "Storico persistente " + String(littleFsReady ? "attivo" : "non disponibile"));
  html += card("Alimentazione", "Consigliata", "12V batteria -> buck 5V 3A -> USB-C ESP. VE.Direct solo dati.");
  html += "</div>";
  html += "<div class='card'><div class='t'>JSON sistema</div><pre style='white-space:pre-wrap;color:#8b949e'>" + esc(systemHealthJson()) + "</pre></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void addEventLog(const String& type, const String& msg) {
  if (!littleFsReady) return;
  fs::File f = LittleFS.open("/events.log", "a");
  if (!f) return;
  String line = String(millis() / 1000UL) + "s;" + type + ";" + msg + "\n";
  f.print(line);
  f.close();
  if (LittleFS.exists("/events.log")) {
    fs::File r = LittleFS.open("/events.log", "r");
    if (r && r.size() > 14000) {
      String tail = "";
      r.seek(r.size() > 9000 ? r.size() - 9000 : 0);
      while (r.available()) tail += (char)r.read();
      r.close();
      fs::File w = LittleFS.open("/events.log", "w");
      if (w) { w.print(tail); w.close(); }
    } else if (r) r.close();
  }
}

String eventsLogText() {
  if (!littleFsReady || !LittleFS.exists("/events.log")) return "Nessun evento salvato.";
  fs::File f = LittleFS.open("/events.log", "r");
  if (!f) return "Log non leggibile.";
  String out = "";
  while (f.available()) out += (char)f.read();
  f.close();
  return out;
}

void handleLogsPage() {
  if (!requireAuth()) return;
  String log = eventsLogText();
  String html = htmlHeader("Log eventi");
  html += "<div class='card'><div class='t'>Ultimi eventi</div><p>Boot, WiFi, OTA, touch, alert e diagnostica base.</p>";
  html += "<p><a class='button' href='/logs.json'>JSON</a> <a class='button' href='/logs-clear' onclick='return confirm(\"Cancellare log eventi?\")'>Cancella log</a></p>";
  html += "<pre style='white-space:pre-wrap;background:#060b14;border:1px solid #1f6feb;border-radius:14px;padding:14px;max-height:520px;overflow:auto'>" + esc(log) + "</pre></div>";
  html += "<p><a class='button' href='/'>Home</a></p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleLogsJson() {
  if (!requireAuth()) return;
  String log = eventsLogText();
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"log\":\"" + jsonEsc(log) + "\"";
  j += "}";
  sendJsonPretty(j);
}

void handleLogsClear() {
  if (!requireAuth()) return;
  if (littleFsReady && LittleFS.exists("/events.log")) LittleFS.remove("/events.log");
  addEventLog("LOG", "Log cancellato");
  sendActionPage("Log cancellato", "Il file eventi e' stato svuotato.", 2, "/logs");
}

String settingsBackupJson() {
  prefs.begin("victron", true);
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"github_version_url\":\"" + jsonEsc(prefs.getString("gh_ver_url", getGithubVersionUrl())) + "\",";
  j += "\"github_bin_url\":\"" + jsonEsc(prefs.getString("gh_bin_url", getGithubBinUrl())) + "\",";
  j += "\"github_sha_url\":\"" + jsonEsc(prefs.getString("gh_sha_url", getGithubShaUrl())) + "\",";
  j += "\"github_log_url\":\"" + jsonEsc(prefs.getString("gh_log_url", getGithubChangelogUrl())) + "\",";
  j += "\"esp_bat_mult\":" + String(prefs.getFloat("esp_bat_mult", espBatteryMultiplier()), 5) + ",";
  j += "\"touch_left\":" + String(prefs.getInt("touch_left", 1500)) + ",";
  j += "\"touch_right\":" + String(prefs.getInt("touch_right", 2600)) + ",";
  j += "\"touch_x_left\":" + String(prefs.getInt("touch_x_left", -1)) + ",";
  j += "\"touch_x_right\":" + String(prefs.getInt("touch_x_right", -1)) + ",";
  j += "\"gh_weekly\":" + String(prefs.getBool("gh_weekly", true) ? "true" : "false") + ",";
  j += "\"gh_auto_day\":" + String(prefs.getInt("gh_auto_day", 0)) + ",";
  j += "\"gh_auto_hour\":" + String(prefs.getInt("gh_auto_hour", 9)) + ",";
  j += "\"gh_auto_min\":" + String(prefs.getInt("gh_auto_min", 0)) + ",";
  j += "\"lcd_auto_sec\":" + String(displayAutoOffSeconds()) + ",";
  j += "\"tft_rotation\":" + String(displayRotation) + ",";
  j += "\"esp_bat_installed\":" + String(prefs.getBool("esp_bat_installed", false) ? "true" : "false") + ",";
  j += "\"touch_disabled\":" + String(prefs.getBool("touch_disabled", false) ? "true" : "false") + ",";
  j += "\"backlight_on\":" + String(backlightOn ? "true" : "false");
  j += "}";
  prefs.end();
  return j;
}

void handleSettingsExport() {
  if (!requireAuth()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=victron_settings_backup.json");
  sendJsonPretty(settingsBackupJson());
}

String extractJsonStringValue(const String& src, const String& key, const String& fallback) {
  String token = "\"" + key + "\"";
  int p = src.indexOf(token);
  if (p < 0) return fallback;
  p = src.indexOf(':', p);
  if (p < 0) return fallback;
  p = src.indexOf('"', p);
  if (p < 0) return fallback;
  int e = src.indexOf('"', p + 1);
  if (e < 0) return fallback;
  return src.substring(p + 1, e);
}

float extractJsonFloatValue(const String& src, const String& key, float fallback) {
  String token = "\"" + key + "\"";
  int p = src.indexOf(token);
  if (p < 0) return fallback;
  p = src.indexOf(':', p);
  if (p < 0) return fallback;
  int e = p + 1;
  while (e < (int)src.length() && (src[e] == ' ' || src[e] == '\t')) e++;
  int s0 = e;
  while (e < (int)src.length() && ((src[e] >= '0' && src[e] <= '9') || src[e] == '.' || src[e] == '-')) e++;
  if (e <= s0) return fallback;
  return src.substring(s0, e).toFloat();
}

int extractJsonIntValue(const String& src, const String& key, int fallback) {
  return (int)extractJsonFloatValue(src, key, fallback);
}

void handleSettingsBackupPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Backup impostazioni");
  html += "<div class='grid'>";
  html += card("Esporta", "JSON", "Scarica una copia delle impostazioni critiche.<br><a class='button' href='/settings-export'>Scarica backup</a>");
  html += card("Backup/Recovery", "Completo", "Salva firmware.bin, config e dati interni su microSD.<br><a class='button' href='/backup-recovery'>Crea backup completo</a>");
  html += card("Importa", "Manuale", "Incolla qui il JSON esportato per ripristinare URL GitHub, calibrazione batteria e soglie touch.");
  html += "</div><div class='card'><form method='POST' action='/settings-restore'>";
  html += "<textarea name='json' rows='12' style='width:100%;border-radius:14px;background:#060b14;color:#e6edf3;border:1px solid #30363d;padding:12px'>" + esc(settingsBackupJson()) + "</textarea>";
  html += "<p><button class='button' type='submit'>Importa JSON</button> <a class='button' href='/settings'>Settings</a></p>";
  html += "</form></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSettingsRestore() {
  if (!requireAuth()) return;
  String body = server.arg("json");
  if (body.length() < 10) { sendActionPage("Import fallito", "JSON vuoto o non valido.", 3, "/settings-backup"); return; }
  prefs.begin("victron", false);
  String v;
  v = extractJsonStringValue(body, "github_version_url", ""); if (v.length()) prefs.putString("gh_ver_url", v);
  v = extractJsonStringValue(body, "github_bin_url", ""); if (v.length()) prefs.putString("gh_bin_url", v);
  v = extractJsonStringValue(body, "github_sha_url", ""); if (v.length()) prefs.putString("gh_sha_url", v);
  v = extractJsonStringValue(body, "github_log_url", ""); if (v.length()) prefs.putString("gh_log_url", v);
  float mult = extractJsonFloatValue(body, "esp_bat_mult", -1);
  if (mult > 1.0 && mult < 4.0) prefs.putFloat("esp_bat_mult", mult);
  int tl = extractJsonIntValue(body, "touch_left", -1); if (tl > 100 && tl < 3800) prefs.putInt("touch_left", tl);
  int tr = extractJsonIntValue(body, "touch_right", -1); if (tr > 200 && tr < 4095) prefs.putInt("touch_right", tr);
  int txl = extractJsonIntValue(body, "touch_x_left", -1); if (txl > 100 && txl < 4095) prefs.putInt("touch_x_left", txl);
  int txr = extractJsonIntValue(body, "touch_x_right", -1); if (txr > 100 && txr < 4095) prefs.putInt("touch_x_right", txr);
  int gd = extractJsonIntValue(body, "gh_auto_day", -1); if (gd >= 0 && gd <= 6) prefs.putInt("gh_auto_day", gd);
  int gh = extractJsonIntValue(body, "gh_auto_hour", -1); if (gh >= 0 && gh <= 23) prefs.putInt("gh_auto_hour", gh);
  int gm = extractJsonIntValue(body, "gh_auto_min", -1); if (gm >= 0 && gm <= 59) prefs.putInt("gh_auto_min", gm);
  int lcds = extractJsonIntValue(body, "lcd_auto_sec", -1); if (lcds >= 0 && lcds <= 86400) prefs.putInt("lcd_auto_sec", lcds);
  int lcdm = extractJsonIntValue(body, "lcd_auto_min", -1); if (lcds < 0 && lcdm >= 0 && lcdm <= 240) prefs.putInt("lcd_auto_sec", lcdm * 60);
  int rotr = extractJsonIntValue(body, "tft_rotation", -1); if (rotr == 1 || rotr == 3) prefs.putInt("tft_rotation", rotr);
  int tnm = extractJsonIntValue(body, "touch_nav_mode", -1); if (tnm == 0 || tnm == 1) prefs.putInt("touch_nav_mode", tnm);
  prefs.end();
  addEventLog("SETTINGS", "Backup impostazioni importato");
  sendActionPage("Backup importato", "Impostazioni ripristinate. Alcuni valori saranno effettivi al prossimo riavvio.", 4, "/settings-backup");
}

void handleTouchRaw() {
  if (!requireAuth()) return;
  int x = lastTouchRawX;
  int y = lastTouchRawY;
  bool down = false;
  if (touchReady) {
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    delayMicroseconds(20);
    down = ts.touched();
    if (down) {
      TS_Point p = ts.getPoint();
      x = p.x; y = p.y;
      lastTouchRawX = x; lastTouchRawY = y; lastTouchRawMs = millis();
    }
  }
  String j = "{";
  j += "\"ready\":" + String(touchReady ? "true" : "false") + ",";
  j += "\"down\":" + String(down ? "true" : "false") + ",";
  j += "\"x\":" + String(x) + ",";
  j += "\"y\":" + String(y) + ",";
  j += "\"left\":" + String(prefsGetIntSafe("touch_left", 1500)) + ",";
  j += "\"right\":" + String(prefsGetIntSafe("touch_right", 2600)) + ",";
  j += "\"sample_left\":" + String(prefsGetTouchCal("touch_x_left", -1)) + ",";
  j += "\"sample_right\":" + String(prefsGetTouchCal("touch_x_right", -1));
  j += "}";
  sendJsonPretty(j);
}

void handleTouchCalPage() {
  if (!requireAuth()) return;
  int sl = prefsGetTouchCal("touch_x_left", -1);
  int sr = prefsGetTouchCal("touch_x_right", -1);
  int l = prefsGetIntSafe("touch_left", 1500);
  int r = prefsGetIntSafe("touch_right", 2600);
  bool prec = touchPrecisionAvailable();

  String html = htmlHeader("Touch calibration");
  html += "<div class='top'><h1>Calibrazione touch</h1>";
  html += "<div class='sub'>Metodo consigliato: usa la calibrazione precisa a 5 punti direttamente dal TFT: <b>TOOLS &gt; TOUCH CAL</b>. La calibrazione viene salvata separatamente per rotazione normale e 180 gradi.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/settings'>Settings</a><a href='/touch-raw'>Raw JSON</a><a href='/touch-reset'>Riattiva touch</a><a href='/touch-clear'>Reset calibrazione</a></div></div>";

  html += "<div class='grid'>";
  html += "<div class='card'><div class='t'>Stato calibrazione precisa</div>";
  html += "<div class='gxGrid'><div class='gxBig'><div class='gxLabel'>Rotazione</div><div class='gxValue'>" + displayRotationText() + "</div></div>";
  html += "<div class='gxBig'><div class='gxLabel'>Precisione</div><div class='gxValue'>" + String(prec ? "OK" : "Da fare") + "</div></div>";
  html += "<div class='gxBig'><div class='gxLabel'>Navigazione</div><div class='gxValue'>" + touchNavigationModeText() + "</div></div></div>";
  html += "<p class='e'>Per calibrare: sul display vai alla pagina <b>TOOLS</b>, premi <b>TOUCH CAL</b> e tocca le 5 croci nell'ordine richiesto.</p></div>";

  html += "<div class='card'><div class='t'>Lettura live touch</div>";
  html += "<div class='gxGrid'><div class='gxBig'><div class='gxLabel'>Raw X</div><div class='gxValue' id='tx'>-</div></div>";
  html += "<div class='gxBig'><div class='gxLabel'>Raw Y</div><div class='gxValue' id='ty'>-</div></div></div>";
  html += "<p id='touchState' class='e'>Tocca lo schermo del display per vedere i valori grezzi.</p></div>";

  html += "<div class='card'><div class='t'>Fallback vecchio a zone</div>";
  html += "<p>Questi valori restano solo come emergenza se la calibrazione precisa non e' disponibile.</p>";
  html += "<p>Sinistra salvata: <b id='leftSaved'>" + String(sl) + "</b><br>Destra salvata: <b id='rightSaved'>" + String(sr) + "</b></p>";
  html += "<details><summary>Mostra calibrazione web semplificata</summary>";
  html += "<form method='POST' action='/touch-save-points' onsubmit='return fillTouchForm()'><input type='hidden' name='left_raw' id='leftRaw'><input type='hidden' name='right_raw' id='rightRaw'><p><button class='button' type='button' onclick='markLeft()'>Memorizza sinistra</button> <button class='button' type='button' onclick='markRight()'>Memorizza destra</button> <button class='button' type='submit'>Salva fallback</button></p></form>";
  html += "<form method='POST' action='/touch-save'><p>Limite sinistro <input name='left' type='number' value='" + String(l) + "' style='width:110px'> Limite destro <input name='right' type='number' value='" + String(r) + "' style='width:110px'> <button class='button' type='submit'>Salva manuale</button></p></form>";
  html += "</details></div></div>";

  html += R"rawliteral(
<script>
let curX=-1,curY=-1,leftVal=null,rightVal=null;
async function u(){try{let r=await fetch('/touch-raw?nc='+Date.now());let j=await r.json();curX=Number(j.x);curY=Number(j.y);document.getElementById('tx').textContent=curX;document.getElementById('ty').textContent=curY;document.getElementById('touchState').textContent=j.ready?(j.down?'Touch premuto':'Touch pronto'):'Touch non ancora pronto / guard attivo'; if(leftVal===null && j.sample_left>0){leftVal=Number(j.sample_left);document.getElementById('leftSaved').textContent=leftVal;} if(rightVal===null && j.sample_right>0){rightVal=Number(j.sample_right);document.getElementById('rightSaved').textContent=rightVal;}}catch(e){}}
function markLeft(){ if(curX<0){alert('Tocca prima il display');return;} leftVal=curX; document.getElementById('leftSaved').textContent=leftVal; }
function markRight(){ if(curX<0){alert('Tocca prima il display');return;} rightVal=curX; document.getElementById('rightSaved').textContent=rightVal; }
function fillTouchForm(){ if(leftVal===null || rightVal===null || Math.abs(rightVal-leftVal)<250){alert('Servono due punti validi e distanti: sinistra e destra.'); return false;} document.getElementById('leftRaw').value=leftVal; document.getElementById('rightRaw').value=rightVal; return true; }
setInterval(u,400);u();
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleTouchSavePoints() {
  if (!requireAuth()) return;
  int l = server.arg("left_raw").toInt();
  int r = server.arg("right_raw").toInt();
  if (l < 100 || l > 4095 || r < 100 || r > 4095 || abs(r - l) < 250) {
    sendActionPage("Calibrazione touch non valida", "Tocca un punto a sinistra e uno a destra: devono essere abbastanza distanti.", 4, "/touch-cal");
    return;
  }
  prefsPutIntSafe("touch_x_left", l);
  prefsPutIntSafe("touch_x_right", r);
  addEventLog("TOUCH", "Calibrazione guidata salvata left=" + String(l) + " right=" + String(r));
  sendActionPage("Touch calibrato", "Calibrazione guidata salvata. Sinistra=" + String(l) + ", Destra=" + String(r) + ".", 2, "/touch-cal");
}

void handleTouchSave() {
  if (!requireAuth()) return;
  int l = server.arg("left").toInt();
  int r = server.arg("right").toInt();
  if (l < 100 || r > 4095 || r <= l + 200) { sendActionPage("Soglie non valide", "Controlla che destra sia maggiore di sinistra.", 3, "/touch-cal"); return; }
  prefsPutIntSafe("touch_left", l);
  prefsPutIntSafe("touch_right", r);
  addEventLog("TOUCH", "Calibrazione salvata L=" + String(l) + " R=" + String(r));
  sendActionPage("Touch salvato", "Nuove soglie: sinistra " + String(l) + ", destra " + String(r), 2, "/touch-cal");
}

void handleHistoryCsv() {
  if (!requireAuth()) return;
  String csv = "type,label,wh,kwh,maxw,maxpv,battmax,battmin,battavg,panelavg,loadwh,errors,samples\n";
  auto addRows = [&](const char* type, HistorySlot *arr, int count, int currentIndex, const char* pref) {
    for (int i = 0; i < count; i++) {
      int idx = (currentIndex + 1 + i) % count;
      HistorySlot &s = arr[idx];
      float battAvg = s.samples ? s.battSum / s.samples : 0;
      float panelAvg = s.samples ? s.panelSum / s.samples : 0;
      float battMinSafe = (s.battMin == 999) ? 0 : s.battMin;
      String label = String(pref) + String(i - count + 1);
      csv += String(type) + "," + label + "," + String(s.wh,2) + "," + String(s.wh/1000.0,3) + "," + String(s.maxW,1) + "," + String(s.maxPanelV,2) + "," + String(s.battMax,2) + "," + String(battMinSafe,2) + "," + String(battAvg,2) + "," + String(panelAvg,2) + "," + String(s.loadWh,2) + "," + String(s.errors) + "," + String(s.samples) + "\n";
    }
  };
  addRows("hourly", hourly, 24, currentHourIndex, "H");
  addRows("daily", daily, 31, currentDayIndex, "D");
  addRows("monthly", monthly, 12, currentMonthIndex, "M");
  server.sendHeader("Content-Disposition", "attachment; filename=victron_history.csv");
  server.send(200, "text/csv", csv);
}

void handleOtaCenterPage() {
  if (!requireAuth()) return;
  checkGithubUpdate(true);
  String otaStatus = prefGet("ota_status", "N/D");
  String otaDetail = prefGet("ota_detail", "N/D");

  prefs.begin("victron", true);
  bool en = prefs.getBool("gh_weekly", true);
  int day = prefs.getInt("gh_auto_day", 0);
  int hour = prefs.getInt("gh_auto_hour", 9);
  int minute = prefs.getInt("gh_auto_min", 0);
  String lastRun = prefs.getString("gh_weekly_last", "N/D");
  String lastStatus = prefs.getString("gh_weekly_status", lastScheduledUpdateStatus);
  prefs.end();

  const char* dayNames[] = {"Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato"};
  if (day < 0 || day > 6) day = 0;
  if (hour < 0 || hour > 23) hour = 9;
  if (minute < 0 || minute > 59) minute = 0;

  String html = htmlHeader("Centro aggiornamenti");
  html += "<div class='top'><h1>Centro aggiornamenti</h1><div class='sub'>Gestione OTA locale, GitHub OTA e controllo automatico programmabile.</div><div class='nav'><a href='/'>Dashboard</a><a href='/github-update'>GitHub OTA</a><a href='/update'>OTA locale</a><a href='/ota-status'>OTA Pro</a></div></div>";
  html += "<div class='grid'>";
  html += card("Installato", String(FW_VERSION), String(FW_NAME) + "<br>Build: " + buildText());
  html += card("GitHub", esc(githubRemoteVersion), "Disponibile: " + String(githubUpdateAvailable ? "SI" : "NO") + "<br>Status: " + esc(githubLastStatus) + "<br>Ultimo controllo: " + esc(prefGet("gh_last_check_time", "N/D")));
  html += card("Ultimo OTA", esc(otaStatus), esc(otaDetail));
  html += card("Partizioni", fmtBytes(ESP.getSketchSize()), "Spazio update: " + fmtBytes(ESP.getFreeSketchSpace()));
  html += "</div>";
  html += "<div class='card'><div class='t'>Controllo automatico</div>";
  html += "<p>Stato: <b>" + String(en ? "ATTIVO" : "DISATTIVO") + "</b><br>Quando: <b>" + String(dayNames[day]) + " alle " + String(hour) + ":" + (minute < 10 ? "0" : "") + String(minute) + "</b><br>Ultimo controllo: " + esc(lastRun) + "<br>Esito: " + esc(lastStatus) + "</p>";
  html += "<form method='POST' action='/ota-schedule-save'><p><label><input type='checkbox' name='enabled' value='1' " + String(en ? "checked" : "") + "> Abilita controllo automatico</label></p>";
  html += "<p>Giorno <select name='day'>";
  for (int i=0;i<7;i++){ html += "<option value='" + String(i) + "'" + String(i==day?" selected":"") + ">" + String(dayNames[i]) + "</option>"; }
  html += "</select> Ora <input name='hour' type='number' min='0' max='23' value='" + String(hour) + "' style='width:70px'> Minuti <input name='minute' type='number' min='0' max='59' value='" + String(minute) + "' style='width:70px'> <button class='button' type='submit'>Salva programma</button></p></form>";
  html += "<div class='e'>Il controllo usa NTP. Ha una finestra di circa 5 minuti per evitare problemi se il Wi-Fi arriva in ritardo.</div></div>";
  html += "<div class='card'><div class='t'>Azioni</div><p><a class='button' href='/github-update'>GitHub OTA</a> <a class='button' href='/update'>OTA locale</a> <a class='button' href='/ota-status'>OTA Pro</a> <a class='button' href='/recovery'>Recovery</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleOtaScheduleSave() {
  if (!requireAuth()) return;
  bool en = server.hasArg("enabled");
  int day = server.arg("day").toInt();
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  if (day < 0) day = 0; if (day > 6) day = 6;
  if (hour < 0) hour = 0; if (hour > 23) hour = 23;
  if (minute < 0) minute = 0; if (minute > 59) minute = 59;
  prefs.begin("victron", false);
  prefs.putBool("gh_weekly", en);
  prefs.putInt("gh_auto_day", day);
  prefs.putInt("gh_auto_hour", hour);
  prefs.putInt("gh_auto_min", minute);
  prefs.end();
  addEventLog("OTA", "Programma automatico salvato day=" + String(day) + " " + String(hour) + ":" + String(minute));
  sendActionPage("Programma OTA salvato", "Controllo automatico aggiornato.", 2, "/ota-center");
}

void handleUpdatesHub() {
  if (!requireAuth()) return;
  checkGithubUpdate(true);
  String html = htmlHeader("Aggiornamenti e Sicurezza");
  html += "<div class='top'><h1>Aggiornamenti & Sicurezza</h1>";
  html += "<div class='sub'>Tutte le funzioni OTA, GitHub, rollback, recovery e verifica firmware in un solo posto.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/github-update'>GitHub Update</a><a href='/update'>OTA locale</a><a href='/ota-status'>Stato OTA</a><a href='/recovery'>Recovery</a><a href='/system-pro'>Sistema</a></div></div>";
  html += "<div class='grid'>";
  html += card("GitHub Update", githubRemoteVersion.length() ? esc(githubRemoteVersion) : "N/D", "Controllo automatico eseguito all apertura.<br>Status: " + esc(githubLastStatus) + "<br>Ultimo controllo: " + esc(prefGet("gh_last_check_time", "N/D")) + "<br><a class='button' href='/github-update'>Apri GitHub Update</a>");
  html += card("OTA locale", String(FW_VERSION), "Carica manualmente un file .bin da PC o telefono.<br><a class='button' href='/update'>Apri firmware OTA</a>");
  html += card("Stato OTA", esc(prefGet("ota_status", "N/D")), "Partizioni, spazio disponibile, ultimo upload, checksum e log.<br><a class='button' href='/ota-status'>Apri OTA Pro</a>");
  html += card("Recovery", recoveryMode ? "Attiva" : "Pronta", "Modalita emergenza con accesso rapido a OTA e diagnostica.<br><a class='button' href='/recovery'>Apri Recovery</a>");
  html += card("Rollback", esc(prefGet("rollback_status", rollbackStatus)), "Conferma boot stabile e protegge dagli update falliti.");
  html += card("Aggiornamento automatico", "Domenica 09:00", "Controllo settimanale GitHub quando WiFi e NTP sono disponibili.");
  html += "</div>";
  html += "<div class='card'><div class='t'>Ordine consigliato</div><p>1) Controlla GitHub Update. 2) Se disponibile, aggiorna da GitHub. 3) Dopo il riavvio apri Firmware o OTA Pro per confermare versione e data.</p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void handleVictronDataPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Dati Victron");
  html += "<div class='top'><h1>Dati Victron / VE.Direct</h1>";
  html += "<div class='sub'>Tabella tecnica live con i valori letti dal regolatore. Utile per debug, cablaggio VE.Direct e controllo produzione.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/data-center'>Dati & Storico</a><a href='/history-compact?type=daily'>Storico</a><a href='/api/live'>JSON Live</a><a href='/system-pro'>Sistema</a></div></div>";

  html += "<div class='grid'>";
  html += card("VE.Direct", victronOnline() ? "Online" : "Offline", "Ultimo dato: " + String((millis() - lastVictronMs) / 1000UL) + " s fa<br>Ultima riga: " + esc(lastRawLine));
  html += card("Solare", fmt(panelW,0," W"), "PV: " + fmt(panelV,2," V") + "<br>Oggi: " + fmt(yieldTodayKWh,2," kWh"));
  html += card("Batteria impianto", fmt(battV,2," V"), "Corrente: " + fmt(battA,2," A") + "<br>Potenza: " + fmt(battW,1," W"));
  html += card("Stato carica", esc(chargeState), "MPPT: " + esc(mpptState) + "<br>Errore: " + esc(errorState));
  html += "</div>";

  html += "<div class='card'><div class='t'>Valori VE.Direct</div><table class='dataTable'>";
  html += "<tr><td>PV Voltage</td><td id='d_pv'>" + fmt(panelV,2," V") + "</td></tr>";
  html += "<tr><td>PV Power</td><td id='d_pw'>" + fmt(panelW,0," W") + "</td></tr>";
  html += "<tr><td>Battery Voltage</td><td id='d_bv'>" + fmt(battV,2," V") + "</td></tr>";
  html += "<tr><td>Charge Current</td><td id='d_ba'>" + fmt(battA,2," A") + "</td></tr>";
  html += "<tr><td>Battery Power</td><td id='d_bw'>" + fmt(battW,1," W") + "</td></tr>";
  html += "<tr><td>Yield today</td><td id='d_yt'>" + fmt(yieldTodayKWh,3," kWh") + "</td></tr>";
  html += "<tr><td>Yield total</td><td id='d_ytt'>" + fmt(yieldTotalKWh,2," kWh") + "</td></tr>";
  html += "<tr><td>Charge state</td><td id='d_cs'>" + esc(chargeState) + "</td></tr>";
  html += "<tr><td>Error</td><td id='d_er'>" + esc(errorState) + "</td></tr>";
  html += "<tr><td>WiFi RSSI</td><td id='d_rssi'>" + String(WiFi.RSSI()) + " dBm</td></tr>";
  html += "<tr><td>ESP Battery</td><td id='d_esp'>" + String(espBatteryVoltage(),2) + " V / " + String(espBatteryPercent(),0) + "%</td></tr>";
  html += "</table><div class='liveSmall'>Aggiornamento automatico ogni 3 secondi da /json.</div></div>";

  html += R"rawliteral(
<script>
function set(id,v){const e=document.getElementById(id); if(e)e.innerText=v;}
async function live(){try{const r=await fetch('/json?_='+Date.now(),{cache:'no-store'}); const j=await r.json();
 set('d_pv',Number(j.panel_voltage).toFixed(2)+' V');
 set('d_pw',Number(j.panel_power).toFixed(0)+' W');
 set('d_bv',Number(j.battery_voltage).toFixed(2)+' V');
 set('d_ba',Number(j.battery_current).toFixed(2)+' A');
 set('d_bw',Number(j.battery_power).toFixed(1)+' W');
 set('d_yt',Number(j.yield_today_kwh).toFixed(3)+' kWh');
 set('d_ytt',Number(j.yield_total_kwh).toFixed(2)+' kWh');
 set('d_cs',j.charge_state||'N/D');
 set('d_er',j.error||'0');
 set('d_rssi',j.wifi_rssi+' dBm');
 set('d_esp',Number(j.esp_battery_voltage).toFixed(2)+' V / '+Number(j.esp_battery_percent).toFixed(0)+'%');
}catch(e){}}
setInterval(live,3000); live();
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleDataCenter() {
  if (!requireAuth()) return;
  String html = htmlHeader("Dati e Storico");
  html += "<div class='top'><h1>Dati & Storico</h1>";
  html += "<div class='sub'>Live, grafici, storico persistente e API in una pagina ordinata.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/live-status'>Live leggibile</a><a href='/victron-data'>Dati Victron</a><a href='/history-compact?type=daily'>Storico</a><a href='/api/live'>API Live</a><a href='/api/history?type=daily'>API History</a><a href='/api/system'>API Sistema</a></div></div>";
  html += "<div class='grid'>";
  html += card("Live leggibile", fmt(panelW,0," W"), "Pagina umana con valori aggiornati, non JSON grezzo.<br><a class='button' href='/live-status'>Apri Live</a>");
  html += card("Dati Victron", victronOnline() ? "Online" : "Offline", "Tabella tecnica VE.Direct con potenze, tensioni, correnti, stato, errore e rese.<br><a class='button' href='/victron-data'>Apri Dati Victron</a>");
  html += card("Storico compatto", fmt(yieldTodayKWh,2," kWh"), "Grafica storico professionale con 7 giorni, 12 ore, 31 giorni e 12 mesi.<br><a class='button' href='/history-compact?type=daily'>Apri Storico</a>");
  html += card("API Live", "JSON", "Per Home Assistant, Node-RED, Grafana o debug.<br><a class='button' href='/api/live'>Apri JSON live</a>");
  html += card("API Storico", "JSON", "Dati storici persistenti LittleFS.<br><a class='button' href='/api/history?type=daily'>Apri JSON storico</a> <a class='button' href='/history.csv'>CSV</a>");
  html += card("API Sistema", "JSON", "Stato ESP32, WiFi, memoria, firmware.<br><a class='button' href='/api/system'>Apri JSON sistema</a>");
  html += card("JSON originale", "Compatibilita", "Endpoint vecchio mantenuto per integrazioni gia fatte.<br><a class='button' href='/json'>Apri /json</a>");
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSystemPro() {
  if (!requireAuth()) return;
  String html = htmlHeader("Sistema Pro");
  html += "<h1>Sistema Pro</h1><p><a href='/'>Dashboard</a> &middot; <a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/data-center'>Dati & Storico</a></p>";
  html += "<div class='grid'>";
  html += card("Firmware", String(FW_VERSION), String(FW_NAME) + "<br>Build: " + buildText());
  html += card("Profilo hardware", "diymore IO27", "VE.Direct RX: IO27<br>VE.Direct TX: disabilitato<br>ESP Battery: GPIO34<br>OTA repo: victron-esp32-monitor-ota");
  html += card("ESP32", String(ESP.getFreeHeap()) + " heap", "Uptime: " + uptimeText() + "<br>Boot: " + String(bootCounter));
  html += card("Batteria ESP", "BAT scan", "Tensione e percentuale LiPo tampone ESP.<br><a class='button' href='/battery'>Apri Batteria ESP</a> <a class='button' href='/bat-scan'>BAT scan</a>");
  html += card("WiFi", WiFi.localIP().toString(), "RSSI: " + String(WiFi.RSSI()) + " dBm<br>Hostname: " + String(HOSTNAME));
  html += card("LittleFS", littleFsReady ? "Attivo" : "Non attivo", littleFsReady ? ("Usati " + String((unsigned long)LittleFS.usedBytes()) + " / " + String((unsigned long)LittleFS.totalBytes())) : "Storico persistente non disponibile");
  html += card("MicroSD", sdMounted ? "Montata" : "Smontata", sdMounted ? ("Usati " + formatBytes64(SD.usedBytes()) + " / " + formatBytes64(SD.totalBytes()) + "<br>Storage: " + storageMode() + "<br><a class='button' href='/sd'>Gestisci SD</a> <a class='button' href='/storage'>Storage</a>") : (esc(sdLastStatus) + "<br><a class='button' href='/sd'>Gestisci SD</a> <a class='button' href='/storage'>Storage</a>"));
  html += card("Rollback", esc(prefGet("rollback_status", rollbackStatus)), otaPartitionJson());
  html += card("Safe OTA V9", esc(prefGet("safe_boot_status", "N/D")), "Recovery: <a href='/recovery'>apri</a><br>Ultimo SHA256: " + esc(prefGet("last_sha256", "N/D")));
  html += card("VE.Direct", victronOnline() ? "Online" : "Offline", "Ultima linea: " + esc(lastRawLine));
  html += "</div>";
  html += "<div class='card'><div class='t'>API</div><p><a class='button' href='/live-status'>Live</a><a class='button' href='/api/live'>JSON live</a><a class='button' href='/api/history?type=daily'>JSON history</a><a class='button' href='/api/system'>JSON system</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleGithubUpdatePage() {
  if (!requireAuth()) return;
  checkGithubUpdate(true);
  String html = htmlHeader("GitHub Update");
  html += "<h1>GitHub Auto Update</h1><p><a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/update'>OTA locale</a> &middot; <a href='/system-pro'>Sistema</a></p>";
  html += "<div class='card'>";
  html += "<div class='t'>Stato</div>";
  String verUrl = getGithubVersionUrl();
  String binUrl = getGithubBinUrl();
  html += "<p><b>Firmware locale:</b> " + String(FW_VERSION) + "</p>";
  html += "<p><b>Versione remota:</b> " + esc(githubRemoteVersion) + "</p>";
  html += "<p><b>Controllo settimanale:</b> domenica ore 09:00</p>";
  html += "<p><b>Esito controllo:</b> " + esc(githubLastStatus) + "</p>";
  html += "<p><b>Ultimo controllo:</b> " + esc(prefGet("gh_last_check_time", "N/D")) + "</p>";
  html += "<div class='sectionNote'>La pagina controlla automaticamente la repo OTA quando viene aperta. Il pulsante serve solo per ricontrollare manualmente.</div>";
  html += "<p><b>Version URL:</b> " + esc(verUrl.length() ? verUrl : "Non configurato") + "</p>";
  html += "<p><b>BIN URL:</b> " + esc(binUrl.length() ? binUrl : "Non configurato") + "</p>";
  html += "<p><a class='button' href='/github-update?check=1'>Ricontrolla ora</a></p>";
  if (binUrl.length()) html += "<form method='POST' action='/github-update-start'><button type='submit'>Aggiorna da GitHub</button></form>";
  else html += "<div class='banner'>Inserisci gli URL qui sotto e premi Salva. Non serve piu ricompilare il firmware per cambiare link.</div>";
  html += "</div>";
  html += "<div class='card'><div class='t'>Configura URL GitHub OTA</div>";
  html += "<form method='POST' action='/github-save'>";
  html += "<p>URL versione <small>(raw version.txt)</small></p>";
  html += "<input name='version_url' style='width:100%;padding:12px;border-radius:12px' value='" + esc(verUrl) + "'>";
  html += "<p>URL firmware .bin <small>(raw latest.bin)</small></p>";
  html += "<input name='bin_url' style='width:100%;padding:12px;border-radius:12px' value='" + esc(binUrl) + "'>";
  html += "<p>URL SHA256 opzionale <small>(raw sha256.txt)</small></p>";
  html += "<input name='sha' style='width:100%;padding:12px;border-radius:12px' value='" + esc(getGithubShaUrl()) + "'>";
  html += "<p>URL changelog opzionale <small>(raw changelog.txt)</small></p>";
  html += "<input name='log' style='width:100%;padding:12px;border-radius:12px' value='" + esc(getGithubChangelogUrl()) + "'>";
  prefs.begin("victron", true);
  bool weeklyEnabled = prefs.getBool("gh_weekly", true);
  String weeklyStatus = prefs.getString("gh_weekly_status", "N/D");
  prefs.end();
  html += "<p><label><input type='checkbox' name='weekly' value='1' " + String(weeklyEnabled ? "checked" : "") + "> Controllo automatico ogni domenica alle 09:00</label></p>";
  html += "<p><b>Ultimo controllo automatico:</b> " + esc(weeklyStatus) + "</p>";
  html += "<p><button type='submit'>Salva URL GitHub</button></p>";
  html += "</form>";
  html += "<p><small>URL consigliato firmware: https://raw.githubusercontent.com/alessioquartarone-ui/victron-esp32-monitor-ota/main/firmware/latest.bin</small></p>";
  html += "</div>";
  String changelog = getGithubChangelogUrl().length() ? httpGetString(getGithubChangelogUrl(), 6000) : "";
  if (changelog.length()) html += "<div class='card'><div class='t'>Changelog remoto</div><pre style='white-space:pre-wrap'>" + esc(changelog) + "</pre></div>";
  html += "<div class='card'><div class='t'>Nota sicurezza</div><p>V9 supporta SHA256 opzionale. HTTPS usa setInsecure per compatibilita ESP32; per sicurezza massima si puo aggiungere CA pinning in una versione futura.</p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleGithubSave() {
  if (!requireAuth()) return;
  String versionUrl = server.arg("version_url");
  String binUrl = server.arg("bin_url");
  versionUrl.trim();
  binUrl.trim();
  saveGithubUrls(versionUrl, binUrl);
  githubLastStatus = "URL GitHub salvati";
  githubRemoteVersion = "N/D";
  String html = htmlHeader("GitHub URL salvati");
  html += "<h1>URL GitHub salvati</h1>";
  html += "<div class='card'><p>Configurazione salvata nella memoria dell'ESP32.</p>";
  html += "<p><a class='button' href='/github-update?check=1'>Ricontrolla ora</a><a class='button' href='/github-update'>Torna</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void githubOtaTask(void* param) {
  githubOtaRunning = true;
  githubOtaDone = false;
  githubOtaOk = false;
  githubOtaPercent = 0;
  githubOtaWritten = 0;
  githubOtaTotal = 0;
  githubOtaMessage = "Avvio download firmware da GitHub...";

  String result;
  bool ok = performGithubBinUpdate(result);
  githubOtaOk = ok;
  githubOtaDone = true;
  githubOtaRunning = false;
  githubOtaMessage = result;
  githubOtaPercent = ok ? 100 : githubOtaPercent;
  githubOtaTaskHandle = NULL;
  vTaskDelete(NULL);
}

void handleGithubProgressJson() {
  if (!requireAuth()) return;
  String j = "{";
  j += "\"running\":" + String(githubOtaRunning ? "true" : "false") + ",";
  j += "\"done\":" + String(githubOtaDone ? "true" : "false") + ",";
  j += "\"ok\":" + String(githubOtaOk ? "true" : "false") + ",";
  j += "\"percent\":" + String(githubOtaPercent) + ",";
  j += "\"written\":" + String((unsigned long)githubOtaWritten) + ",";
  j += "\"total\":" + String((unsigned long)githubOtaTotal) + ",";
  j += "\"message\":\"" + esc(githubOtaMessage) + "\"";
  j += "}";
  sendJsonPretty(j);
}

void handleGithubUpdateStart() {
  if (!requireAuth()) return;
  if (githubOtaRunning) {
    server.sendHeader("Location", "/github-progress-page");
    server.send(303);
    return;
  }
  githubOtaDone = false;
  githubOtaOk = false;
  githubOtaPercent = 0;
  githubOtaWritten = 0;
  githubOtaTotal = 0;
  githubOtaMessage = "Preparazione OTA GitHub...";
  xTaskCreatePinnedToCore(githubOtaTask, "gh_ota", 12288, NULL, 1, &githubOtaTaskHandle, 1);
  server.sendHeader("Location", "/github-progress-page");
  server.send(303);
}

void handleGithubProgressPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("GitHub OTA Progress");
  html += "<h1>GitHub OTA</h1>";
  html += "<div class='card'>";
  html += "<p><b>Stato:</b> <span id='msg'>Avvio...</span></p>";
  html += "<div style='width:100%;height:24px;background:#1c2430;border-radius:14px;overflow:hidden;border:1px solid rgba(255,255,255,.12)'><div id='bar' style='height:100%;width:0%;background:linear-gradient(90deg,#5ee77d,#83a8ff);transition:width .4s'></div></div>";
  html += "<p style='font-size:30px;font-weight:800'><span id='pct'>0</span>%</p>";
  html += "<p><span id='bytes'>0 / 0</span></p>";
  html += "<p>Al termine l'ESP32 si riavvia automaticamente. Se la pagina non torna da sola, usa i pulsanti sotto.</p>";
  html += "<p><a class='button' href='/firmware'>Firmware</a><a class='button' href='/github-update'>GitHub Update</a><a class='button' href='/'>Dashboard</a></p>";
  html += "</div>";
  html += R"rawliteral(
<script>
let waitingReboot=false;
let rebootTries=0;
function mb(b){ b=Number(b||0); return (b/1048576).toFixed(2)+' MB ('+b+' byte)'; }
function setMsg(t){ document.getElementById('msg').textContent=t; }
function setProgress(j){
  document.getElementById('pct').textContent=j.percent || 0;
  document.getElementById('bar').style.width=(j.percent || 0)+'%';
  document.getElementById('bytes').textContent=mb(j.written)+' / '+mb(j.total);
  setMsg(j.message || 'In corso...');
}
async function waitEspBack(){
  rebootTries++;
  setMsg('Firmware scritto. ESP32 in riavvio/WiFi... tentativo '+rebootTries+'. Non chiudere questa pagina.');
  try{
    const r = await fetch('/json?wait='+Date.now(), {cache:'no-store'});
    if(r.ok){
      setMsg('ESP32 riconnesso. Apro pagina Firmware...');
      setTimeout(()=>{ location.href='/firmware?ota=ok&t='+Date.now(); }, 1200);
      return;
    }
  }catch(e){}
  setTimeout(waitEspBack, 4000);
}
function poll(){
  if(waitingReboot) return;
  fetch('/github-progress',{cache:'no-store'})
    .then(r=>r.json())
    .then(j=>{
      setProgress(j);
      if(j.done && j.ok){
        waitingReboot=true;
        document.getElementById('pct').textContent='100';
        document.getElementById('bar').style.width='100%';
        setMsg('Firmware scaricato e scritto. Attendo riavvio completo e WiFi...');
        setTimeout(waitEspBack, 18000);
      } else if(j.done && !j.ok){
        setMsg('ERRORE: '+(j.message || 'OTA fallito'));
      } else {
        setTimeout(poll,1000);
      }
    })
    .catch(e=>{
      if(!waitingReboot){ setMsg('Connessione temporanea persa, riprovo...'); setTimeout(poll,2500); }
    });
}
poll();
</script>
)rawliteral";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleBacklight() {
  if (!requireAuth()) return;

  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") setBacklight(true);
    else if (state == "off") setBacklight(false);
    lastUserActivityMs = millis();
    sendActionPage("Retroilluminazione aggiornata", String("Display ") + (backlightOn ? "acceso" : "spento"), 2, "/settings");
    return;
  }

  String html = htmlHeader("Retroilluminazione");
  html += "<h1>Retroilluminazione</h1>";
  html += "<p><a href='/settings'>Settings</a> &middot; <a href='/'>Dashboard</a></p>";
  html += "<div class='card'><div class='t'>Backlight ON/OFF</div>";
  html += "<p><b>Stato attuale:</b> ";
  html += backlightOn ? "ON" : "OFF";
  html += "</p>";
  html += "<p><a class='button' href='/backlight?state=on'>Accendi</a> ";
  html += "<a class='button' href='/backlight?state=off'>Spegni</a></p>";
  html += "<p><small>Retroilluminazione solo ON/OFF: PWM disattivato perché non compatibile con questa scheda.</small></p>";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


void handleTouchClear() {
  if (!requireAuth()) return;
  prefs.begin("victron", false);
  const char* bases[] = {"valid","x0","y0","x1","y1","x2","y2","x3","y3","x4","y4"};
  for (int r = 1; r <= 3; r += 2) {
    for (size_t i = 0; i < sizeof(bases)/sizeof(bases[0]); i++) {
      String k = String("tp_") + bases[i] + "_r" + String(r);
      prefs.remove(k.c_str());
    }
  }
  prefs.remove("touch_x_left");
  prefs.remove("touch_x_right");
  prefs.remove("touch_x_home");
  prefs.remove("touch_y_home");
  prefs.remove("touch_cal_rotation");
  prefs.end();
  addEventLog("TOUCH", "Calibrazione touch cancellata");
  sendActionPage("Calibrazione touch", "Calibrazione cancellata. Usa TOOLS > TOUCH CAL per rifarla.", 3, "/touch-cal");
}

void handleTouchReset() {
  if (!requireAuth()) return;

  prefs.begin("victron", false);
  prefs.putBool("touch_pending", false);
  prefs.putBool("touch_disabled", false);
  prefs.end();

  touchReady = false;
  touchInitTried = false;
  touchWasDown = false;
  touchInitAfterMs = millis() + 5000UL;

  sendActionPage("Touch riattivato", "Protezione touch azzerata. Il touch verra' reinizializzato tra pochi secondi senza riflashare.", 3, "/settings");
}


void handleSettings() {
  if (!requireAuth()) return;

  if (server.hasArg("backlight")) {
    String b = server.arg("backlight");
    if (b == "on") setBacklight(true);
    else if (b == "off") setBacklight(false);
    lastUserActivityMs = millis();
    sendActionPage("Retroilluminazione aggiornata", String("Display ") + (backlightOn ? "acceso" : "spento"), 2, "/settings");
    return;
  }

  if (server.hasArg("lcd_auto_sec") || server.hasArg("lcd_auto")) {
    int sec = 0;
    if (server.hasArg("lcd_auto_sec")) sec = server.arg("lcd_auto_sec").toInt();
    else sec = server.arg("lcd_auto").toInt() * 60;
    if (sec < 0) sec = 0;
    if (sec > 86400) sec = 86400;
    prefs.begin("victron", false);
    prefs.putInt("lcd_auto_sec", sec);
    prefs.end();
    lastUserActivityMs = millis();
    addEventLog("LCD", "Auto OFF impostato: " + displayAutoOffText());
    sendActionPage("Auto spegnimento LCD", "Tempo salvato: " + displayAutoOffText(), 2, "/settings");
    return;
  }

  if (server.hasArg("tft_rotation")) {
    int r = server.arg("tft_rotation").toInt();
    displayRotation = (r == 3) ? 3 : 1;
    prefs.begin("victron", false);
    prefs.putInt("tft_rotation", displayRotation);
    prefs.end();
    lastUserActivityMs = millis();
    addEventLog("LCD", "Rotazione TFT impostata: " + displayRotationText());
    robustTftInit();
    drawDashboard();
    setBacklight(true);
    sendActionPage("Rotazione display", "Rotazione salvata: " + displayRotationText() + ". Se il touch non coincide, usa TOOLS > TOUCH CAL.", 3, "/settings");
    return;
  }

  if (server.hasArg("touch_mode")) {
    String tm = server.arg("touch_mode");
    int mode = (tm == "zones") ? 1 : 0;
    prefsPutIntSafe("touch_nav_mode", mode);
    addEventLog("TOUCH", "Modalita navigazione: " + touchNavigationModeText());
    sendActionPage("Modalita touch", "Modalita salvata: " + touchNavigationModeText(), 2, "/settings");
    return;
  }

  if (server.hasArg("resetwifi")) {
    server.send(200, "text/html; charset=utf-8",
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='12;url=http://192.168.4.1'>"
      "<style>body{font-family:Arial;background:#0d1117;color:#e6edf3;padding:25px}.ok{color:#7ee787;font-size:25px;font-weight:bold}</style>"
      "</head><body><div class='ok'>Reset WiFi</div>"
      "<p>Credenziali cancellate. Tra pochi secondi cerca la rete <b>Victron-ESP32-Setup</b>.</p>"
      "<p>Password: <b>12345678</b></p>"
      "<p>Apri: <b>http://192.168.4.1</b></p>"
      "</body></html>");
    delay(1500);
    wifiManager.resetSettings();
    cleanRestartNow("reset_wifi");
    return;
  }

  if (server.hasArg("reboot")) {
    sendActionPage("Riavvio ESP32", "Riavvio in corso. La pagina tornera' automaticamente alla dashboard.", 8, "/");
    delay(1200);
    cleanRestartNow("web_reboot");
    return;
  }

  String html = htmlHeader("Settings");
  html += "<h1>Impostazioni</h1><p><a href='/'>Dashboard</a> &middot; <a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/data-center'>Dati & Storico</a> &middot; <a href='/system-pro'>Sistema</a> &middot; <a href='/network'>Rete/IP</a></p>";

  html += "<div class='card'><div class='t'>Stato</div>";
  html += "<p><b>Firmware:</b> " + String(FW_VERSION) + "</p>";
  html += "<p><b>WiFi:</b> " + String(WiFi.SSID()) + " - IP " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Retroilluminazione:</b> " + String(backlightOn ? "ON" : "OFF") + "</p>";
  prefs.begin("victron", true);
  bool touchDisabledUi = prefs.getBool("touch_disabled", false);
  bool touchPendingUi = prefs.getBool("touch_pending", false);
  prefs.end();
  html += "<p><b>Touch:</b> " + String(touchReady ? "ATTIVO" : (touchDisabledUi ? "DISABILITATO DA PROTEZIONE" : (touchPendingUi ? "INIT IN CORSO" : "IN ATTESA/INATTIVO"))) + "</p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Retroilluminazione display</div>";
  html += "<p>Retroilluminazione: solo ON/OFF. PWM e auto-dim restano disattivati per stabilita' hardware.</p>";
  html += "<p><b>Auto spegnimento:</b> " + displayAutoOffText() + "</p>";
  html += "<form method='get' action='/settings'><p>Dopo quanto tempo spegnere il display: <select name='lcd_auto_sec'>";
  int curAutoSec = displayAutoOffSeconds();
  const int optsSec[] = {0,10,15,30,45,60,90,120,180,300,600,900,1800,3600,7200};
  for (size_t i=0;i<sizeof(optsSec)/sizeof(optsSec[0]);i++) {
    String optText;
    if (optsSec[i] == 0) optText = "Mai";
    else if (optsSec[i] < 60) optText = String(optsSec[i]) + " sec";
    else if (optsSec[i] % 60 == 0) optText = String(optsSec[i] / 60) + " min";
    else optText = String(optsSec[i]) + " sec";
    html += "<option value='" + String(optsSec[i]) + "'" + String(curAutoSec==optsSec[i]?" selected":"") + ">" + optText + "</option>";
  }
  html += "</select> <button type='submit'>Salva auto OFF</button></p></form>";
  html += "<form method='get' action='/settings' style='display:inline'><input type='hidden' name='backlight' value='on'><button type='submit'>Accendi display</button></form> ";
  html += "<form method='get' action='/settings' style='display:inline'><input type='hidden' name='backlight' value='off'><button type='submit'>Spegni display</button></form> ";
  html += "<p><a class='button' href='/backlight'>Pagina ON/OFF</a></p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Rotazione schermo</div>";
  html += "<p><b>Rotazione attuale:</b> " + displayRotationText() + "</p>";
  html += "<p>La UI e' ottimizzata per landscape. Puoi scegliere normale o ruotata di 180 gradi.</p>";
  html += "<form method='get' action='/settings'><select name='tft_rotation'>";
  html += "<option value='1" + String(displayRotation==1?"' selected":"'") + ">Normale</option>";
  html += "<option value='3" + String(displayRotation==3?"' selected":"'") + ">Ruotato 180 gradi</option>";
  html += "</select> <button type='submit'>Salva rotazione</button></form>";
  html += "<p class='e'>Dopo la rotazione, se il touch e' invertito, rifai TOOLS &gt; TOUCH CAL.</p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Touch TFT</div>";
  html += "<p><b>Modalita navigazione:</b> " + touchNavigationModeText() + "</p>";
  html += "<p>Consigliato: <b>Solo pulsanti</b>, cosi' cambiano pagina solo i tasti visibili &lt; HOME &gt;. Zone laterali riattiva il comportamento vecchio.</p>";
  html += "<form method='get' action='/settings'><select name='touch_mode'>";
  html += "<option value='buttons" + String(!touchZonesEnabled()?"' selected":"'") + ">Solo pulsanti</option>";
  html += "<option value='zones" + String(touchZonesEnabled()?"' selected":"'") + ">Zone laterali</option>";
  html += "</select> <button type='submit'>Salva modalita touch</button></form>";
  html += "<p>Se il touch viene disabilitato dalla protezione crash-guard, puoi riattivarlo senza riflashare.</p>";
  html += "<p><a class='button' href='/touch-reset'>Riattiva touch</a> <a class='button' href='/touch-cal'>Calibrazione touch</a> <a class='button' href='/touch-clear' onclick=\"return confirm('Cancellare la calibrazione touch precisa?')\">Reset calibrazione</a></p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>WiFi / Rete</div>";
  html += "<p>Se cambia l'IP dopo riavvio router, usa la prenotazione DHCP sul router usando il MAC della CYD.</p>";
  html += "<p><b>IP attuale:</b> " + WiFi.localIP().toString() + "<br><b>MAC:</b> " + WiFi.macAddress() + "<br><b>Hostname:</b> " + String(HOSTNAME) + "<br><b>RSSI:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><a class='button' href='/network'>Apri pagina rete</a> <a class='button' href='/network.json'>network.json</a></p>";
  html += "<p><b>AP setup:</b> Victron-ESP32-Setup / password 12345678</p>";
  html += "<form method='get' action='/settings' onsubmit=\"return confirm('Cancellare il WiFi salvato e riavviare in setup?')\">";
  html += "<input type='hidden' name='resetwifi' value='1'><button type='submit'>Reset WiFi e avvia configurazione</button></form>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Shutdown software remoto</div>";
  html += "<p>Spegni la CYD via software con timer di risveglio oppure fino a reset fisico. La scheda resta alimentata.</p>";
  html += "<p><a class='button danger' href='/shutdown'>Apri shutdown remoto</a></p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Sistema</div>";
  html += "<form method='get' action='/settings' onsubmit=\"return confirm('Riavviare ESP32?')\">";
  html += "<input type='hidden' name='reboot' value='1'><button type='submit'>Riavvia ESP32</button></form>";
  html += "</div>";

  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleUpdatePage() {
  if (!requireAuth()) return;

  String otaStatus = prefGet("ota_status", "N/D");
  String otaTime = prefGet("ota_time", "");
  if (otaTime == "" || otaTime == "N/D") otaTime = "In attesa NTP";
  String otaDetail = prefGet("ota_detail", "N/D");
  String installedTime = prefGet("installed_time", "");
  if (installedTime == "" || installedTime == "N/D") installedTime = "In attesa NTP";
  String installedBuild = prefGet("installed_build", "");
  if (installedBuild == "" || installedBuild == "N/D") installedBuild = buildText();

  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t freeSketch = ESP.getFreeSketchSpace();
  uint32_t maxOtaSize = (freeSketch > 0x1000) ? ((freeSketch - 0x1000) & 0xFFFFF000) : 0;

  String html = htmlHeader("Firmware / OTA");
  html += "<h1>OTA locale / Firmware</h1>";
  html += "<p><a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/github-update'>GitHub Update</a> &middot; <a href='/ota-status'>Stato OTA</a> &middot; <a href='/'>Dashboard</a></p>";

  if (server.hasArg("ota") && server.arg("ota") == "ok") {
    html += "<div class='banner'>ESP32 riconnesso. Firmware attuale: " + String(FW_VERSION) + "</div>";
  }

  html += "<div class='card blue'><div class='t'>Firmware attuale installato</div>";
  html += "<p><b>Nome:</b> " + String(FW_NAME) + "</p>";
  html += "<p><b>Versione:</b> " + String(FW_VERSION) + "</p>";
  html += "<p><b>Data compilazione:</b> " + buildDateIt() + "</p>";
  html += "<p><b>Ora compilazione:</b> " + String(FW_BUILD_TIME) + "</p>";
  html += "<p><b>Ora ESP/NTP:</b> " + timeText() + "</p>";
  html += "<p><b>Installato/rilevato il:</b> " + installedTime + "</p>";
  html += "<p><b>Build firmware installata:</b> " + installedBuild + "</p>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Diagnostica OTA / partizioni</div>";
  html += "<p><b>Sketch attuale:</b> " + fmtBytes(sketchSize) + "</p>";
  html += "<p><b>Spazio OTA libero:</b> " + fmtBytes(freeSketch) + "</p>";
  html += "<p><b>Massimo firmware caricabile stimato:</b> " + fmtBytes(maxOtaSize) + "</p>";
  if (maxOtaSize < 500000) {
    html += "<div class='banner' style='background:#3a2300;border-color:#d29922;color:#ffd58a'>ATTENZIONE: spazio OTA molto basso. Se l'update fallisce, serve flash USB con partition scheme OTA, non 'No OTA'.</div>";
  }
  html += "</div>";

  prefs.begin("victron", true);
  bool otaStillPending = prefs.getBool("ota_pending", false) || prefs.getBool("ota_needs_ntp_fix", false);
  String lastBytes = prefs.getString("ota_last_bytes", "0");
  String lastSize = prefs.getString("ota_last_size", "N/D");
  String lastFree = prefs.getString("ota_free_space", "N/D");
  prefs.end();
  if (otaStillPending) {
    html += "<div class='banner'>OTA rilevato: sto aspettando NTP per salvare data/ora reali. Ricarica questa pagina tra 10-30 secondi.</div>";
  }

  html += "<div class='card'><div class='t'>Ultimo aggiornamento OTA</div>";
  html += "<p><b>Esito:</b> " + otaStatus + "</p>";
  html += "<p><b>Data/ora:</b> " + otaTime + "</p>";
  html += "<p><b>Dettaglio:</b> " + esc(otaDetail) + "</p>";
  html += "<p><b>Firmware scritto ultimo upload:</b> " + fmtBytesStr(lastBytes) + " / " + fmtBytesStr(lastSize) + "</p>";
  html += "<p><b>Spazio OTA rilevato ultimo upload:</b> " + fmtBytesStr(lastFree) + "</p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Carica nuovo firmware .bin</div>";
  html += "<p>Seleziona il file <b>.bin</b> del firmware e avvia l'aggiornamento OTA.</p>";
  html += "<form id='otaForm' enctype='multipart/form-data'>";
  html += "<input id='fwFile' type='file' name='firmware' accept='.bin' required><br><br>";
  html += "<div style='height:22px;background:#202630;border:1px solid #3d4654;border-radius:12px;overflow:hidden;margin:10px 0'><div id='bar' style='height:100%;width:0%;background:#238636;text-align:center;font-size:13px;line-height:22px'>0%</div></div>";
  html += "<button id='otaBtn' type='submit'>Carica firmware</button>";
  html += "</form><p id='otaMsg' class='e'></p>";
  html += "<p><a class='button' href='/firmware?ota=ok'>Apri Firmware</a> <a class='button' href='/'>Dashboard</a> <a class='button' href='/settings'>Settings</a> <a class='button' href='/ota-status'>OTA Pro</a><a class='button' href='/ota-status?json=1'>OTA JSON</a></p>";
  html += "</div>";

  html += R"rawliteral(
<script>
let polling=false, tries=0;
function el(id){return document.getElementById(id)}
function msg(t){el('otaMsg').innerHTML=t;}
function bar(p){p=Math.max(0,Math.min(100,p|0)); el('bar').style.width=p+'%'; el('bar').innerHTML=p+'%';}
async function poll(){
  if(!polling)return;
  tries++;
  msg('Attendo riavvio e WiFi... tentativo '+tries+'<br>Se non cambia pagina, usa il pulsante Apri Firmware dopo che il display e tornato acceso.');
  try{
    const r=await fetch('/json?ping='+Date.now(),{cache:'no-store',credentials:'same-origin'});
    if(r.ok){ msg('ESP32 online. Apro Firmware...'); setTimeout(()=>{location.href='/firmware?ota=ok&t='+Date.now();},1500); return; }
  }catch(e){}
  setTimeout(poll,4000);
}
const f=el('otaForm');
if(f){f.addEventListener('submit',(ev)=>{
 ev.preventDefault();
 const btn=el('otaBtn');
 const file=el('fwFile').files[0];
 if(!file){msg('Seleziona prima un file .bin');return;}
 btn.disabled=true; bar(0);
 msg('Upload in corso. Non chiudere questa pagina. Dimensione: '+file.size+' byte');
 const fd=new FormData(); fd.append('firmware',file,file.name);
 const xhr=new XMLHttpRequest();
 xhr.open('POST','/update',true);
 xhr.responseType='text';
 xhr.upload.onprogress=(e)=>{ if(e.lengthComputable){ bar(Math.round((e.loaded/e.total)*100)); } };
 xhr.onload=()=>{
   bar(100);
   msg('Risposta ESP32:<br><pre style="white-space:pre-wrap">'+(xhr.responseText||'Nessuna risposta')+'</pre><br>Ora attendo riavvio e riconnessione.');
   polling=true; setTimeout(poll,15000);
 };
 xhr.onerror=()=>{
   msg('Connessione caduta durante/fine upload. Se il firmware era gia scritto puo essere normale durante reboot. Attendo ESP32 online...');
   polling=true; setTimeout(poll,15000);
 };
 xhr.send(fd);
});}
</script>
)rawliteral";
  html += "</body></html>";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", html);
}


void saveOtaUploadStats(const String& statusText) {
  prefs.begin("victron", false);
  prefs.putString("ota_last_upload_status", statusText);
  prefs.putString("ota_last_bytes", String((unsigned long)otaWrittenBytes));
  prefs.putString("ota_last_size", otaExpectedSize ? String((unsigned long)otaExpectedSize) : "N/D");
  prefs.putString("ota_free_space", String((unsigned long)otaFreeAtStart));
  prefs.putString("ota_filename", otaFileName);
  prefs.end();
}

void handleLiveStatusPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Live dati");
  html += "<h1>Live dati Victron</h1>";
  html += "<p><a href='/'>Dashboard</a> &middot; <a href='/api/live'>JSON live</a></p>";
  html += "<div class='grid'>";
  html += card("Batteria", fmt(battV, 2, " V"), "Corrente: " + fmt(battA, 2, " A") + "<br>Potenza: " + fmt(battW, 1, " W"));
  html += card("Pannello", fmt(panelW, 0, " W"), "Tensione: " + fmt(panelV, 2, " V"));
  html += card("Produzione", fmt(yieldTodayKWh, 2, " kWh"), "Totale: " + fmt(yieldTotalKWh, 2, " kWh"));
  html += card("Sistema", WiFi.status()==WL_CONNECTED ? "ONLINE" : "OFFLINE", "RSSI: " + String(WiFi.RSSI()) + " dBm<br>VE.Direct: " + String(victronOnline() ? "collegato" : "non collegato"));
  html += "</div>";
  html += "<div class='card'><div class='t'>Aggiornamento automatico</div><pre id='j'>Caricamento...</pre></div>";
  html += "<script>async function u(){try{let r=await fetch('/api/live?nc='+Date.now());let j=await r.json();document.getElementById('j').textContent=JSON.stringify(j,null,2);}catch(e){document.getElementById('j').textContent='Errore lettura live';}}setInterval(u,2000);u();</script>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleOtaStatusPage() {
  if (!requireAuth()) return;
  prefs.begin("victron", true);
  String otaStatus = prefs.getString("ota_status", "N/D");
  String otaTime = prefs.getString("ota_time", "N/D");
  String otaDetail = prefs.getString("ota_detail", "N/D");
  String lastBytes = prefs.getString("ota_last_bytes", "0");
  String lastSize = prefs.getString("ota_last_size", "N/D");
  bool pending = prefs.getBool("ota_pending", false);
  int bootUnconf = prefs.getInt("boot_unconf", 0);
  bool rec = prefs.getBool("recovery_active", false) || prefs.getBool("force_recovery", false) || prefs.getBool("recovery_recommended", false);
  String safe = prefs.getString("safe_boot_status", "N/D");
  String stableFw = prefs.getString("stable_fw", "N/D");
  String stableTime = prefs.getString("stable_time", "N/D");
  String lastPre = prefs.getString("last_pre_ota_status", "N/D");
  String lastPreDetail = prefs.getString("last_pre_ota_detail", "N/D");
  prefs.end();

  String html = htmlHeader("OTA Status / Recovery");
  html += "<h1>OTA Status / Recovery</h1>";
  html += "<p><a href='/updates'>Aggiornamenti & Sicurezza</a> &middot; <a href='/firmware'>Firmware</a> &middot; <a href='/github-update'>GitHub Update</a> &middot; <a href='/recovery'>Recovery</a> &middot; <a href='/ota-status?json=1'>JSON</a></p>";

  html += "<div class='grid'>";
  html += card("Firmware attuale", String(FW_VERSION), String(FW_NAME) + "<br>Build: " + buildText());
  html += card("Firmware stabile", esc(stableFw), "Confermato: " + esc(stableTime));
  html += card("Boot safety", String(bootUnconf) + " / " + String(SAFE_BOOT_MAX_UNCONFIRMED), "Recovery: " + String(rec ? "ATTIVA/CONSIGLIATA" : "NO") + "<br>" + esc(safe));
  html += card("Partizioni OTA", fmtBytes(ESP.getFreeSketchSpace()), "Sketch attuale: " + fmtBytes(ESP.getSketchSize()) + "<br>Max caricabile: " + fmtBytes((ESP.getFreeSketchSpace()>0x1000)?((ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000):0));
  html += card("Ultimo OTA", otaStatus, "Data/ora: " + esc(otaTime) + "<br>Pending: " + String(pending ? "SI" : "NO"));
  html += card("Ultimo upload", fmtBytes((uint32_t)lastBytes.toInt()), "Dimensione dichiarata: " + esc(lastSize));
  html += card("SD / backup", sdMounted ? "SD montata" : "SD non montata", "Pre-OTA: " + esc(lastPre));
  html += card("GitHub remoto", esc(githubRemoteVersion), "Ultimo controllo: " + esc(githubLastStatus));
  html += "</div>";

  html += "<div class='card'><div class='t'>URL OTA attivi</div>";
  html += "<p><b>version.txt:</b><br><small>" + esc(getGithubVersionUrl()) + "</small></p>";
  html += "<p><b>latest.bin:</b><br><small>" + esc(getGithubBinUrl()) + "</small></p>";
  html += "<p><b>latest.sha256:</b><br><small>" + esc(getGithubShaUrl()) + "</small></p>";
  html += "</div>";

  html += "<div class='card'><div class='t'>Dettaglio ultimo OTA</div><p>" + esc(otaDetail) + "</p></div>";
  html += "<div class='card'><div class='t'>Dettaglio backup pre-OTA</div><p>" + esc(lastPreDetail) + "</p></div>";
  html += "<div class='card'><div class='t'>Azioni sicure</div><p><a class='button' href='/github-update?check=1'>Controlla update</a><a class='button' href='/update'>OTA locale</a><a class='button' href='/recovery'>Recovery</a><a class='button' href='/reboot-recovery' onclick=\"return confirm('Riavviare ora in Recovery Mode?')\">Riavvia in Recovery</a></p></div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleOtaStatusJson();

void handleOtaStatus() {
  if (server.hasArg("json")) { handleOtaStatusJson(); return; }
  handleOtaStatusPage();
}

void handleOtaStatusJson() {
  if (!requireAuth()) return;
  prefs.begin("victron", true);
  int bootUnconf = prefs.getInt("boot_unconf", 0);
  bool rec = prefs.getBool("recovery_active", false) || prefs.getBool("force_recovery", false) || prefs.getBool("recovery_recommended", false);
  String j = "{";
  j += "\"fw\":\"" + String(FW_VERSION) + "\",";
  j += "\"fw_name\":\"" + String(FW_NAME) + "\",";
  j += "\"build\":\"" + buildText() + "\",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"uptime_ms\":" + String((unsigned long)millis()) + ",";
  j += "\"sketch_size\":" + String((unsigned long)ESP.getSketchSize()) + ",";
  j += "\"free_sketch_space\":" + String((unsigned long)ESP.getFreeSketchSpace()) + ",";
  j += "\"stable_fw\":\"" + esc(prefs.getString("stable_fw", "N/D")) + "\",";
  j += "\"stable_time\":\"" + esc(prefs.getString("stable_time", "N/D")) + "\",";
  j += "\"boot_unconfirmed\":" + String(bootUnconf) + ",";
  j += "\"boot_unconfirmed_limit\":" + String(SAFE_BOOT_MAX_UNCONFIRMED) + ",";
  j += "\"safe_boot_confirm_seconds\":" + String(SAFE_BOOT_CONFIRM_MS / 1000UL) + ",";
  j += "\"recovery_active\":" + String(rec ? "true" : "false") + ",";
  j += "\"safe_boot_status\":\"" + esc(prefs.getString("safe_boot_status", "N/D")) + "\",";
  j += "\"rollback_status\":\"" + esc(prefs.getString("rollback_status", "N/D")) + "\",";
  j += "\"ota_status\":\"" + esc(prefs.getString("ota_status", "N/D")) + "\",";
  j += "\"ota_time\":\"" + esc(prefs.getString("ota_time", "N/D")) + "\",";
  j += "\"ota_detail\":\"" + esc(prefs.getString("ota_detail", "N/D")) + "\",";
  j += "\"last_upload_status\":\"" + esc(prefs.getString("ota_last_upload_status", "N/D")) + "\",";
  j += "\"last_bytes\":\"" + esc(prefs.getString("ota_last_bytes", "0")) + "\",";
  j += "\"last_size\":\"" + esc(prefs.getString("ota_last_size", "N/D")) + "\",";
  j += "\"ota_pending\":" + String(prefs.getBool("ota_pending", false) ? "true" : "false") + ",";
  j += "\"sd_mounted\":" + String(sdMounted ? "true" : "false") + ",";
  j += "\"last_pre_ota_status\":\"" + esc(prefs.getString("last_pre_ota_status", "N/D")) + "\",";
  j += "\"last_pre_ota_ok\":" + String(prefs.getBool("last_pre_ota_ok", false) ? "true" : "false") + ",";
  j += "\"github_remote_version\":\"" + esc(githubRemoteVersion) + "\",";
  j += "\"github_last_status\":\"" + esc(githubLastStatus) + "\",";
  j += "\"version_url\":\"" + esc(getGithubVersionUrl()) + "\",";
  j += "\"bin_url\":\"" + esc(getGithubBinUrl()) + "\",";
  j += "\"sha256_url\":\"" + esc(getGithubShaUrl()) + "\"";
  j += "}";
  prefs.end();
  server.sendHeader("Cache-Control", "no-store");
  sendJsonPretty(j);
}

void handleUpdateUpload() {
  if (!server.authenticate(WEB_USER, WEB_PASS)) return;

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadAllowed = true;
    uploadError = "";
    otaWrittenBytes = 0;
    otaExpectedSize = upload.totalSize;
    otaFreeAtStart = ESP.getFreeSketchSpace();
    otaFileName = upload.filename;

    String filename = upload.filename;
    filename.toLowerCase();

    if (!filename.endsWith(".bin")) {
      uploadAllowed = false;
      uploadError = "ERRORE: devi caricare un firmware .bin compilato, non un file .ino";
      setUpdateMessage(uploadError);
      saveOtaUploadStats(uploadError);
      return;
    }

    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() > 0x1000) ? ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000) : 0;
    if (maxSketchSpace < 500000) {
      uploadAllowed = false;
      uploadError = "ERRORE: spazio OTA insufficiente o partizione OTA assente. Serve flash USB una volta con partition scheme OTA.";
      setUpdateMessage(uploadError);
      saveOtaUploadStats(uploadError);
      return;
    }

    autoBackupBeforeOta("OTA locale");

    if (!Update.begin(maxSketchSpace, U_FLASH)) {
      uploadAllowed = false;
      uploadError = "ERRORE: impossibile inizializzare update. Probabile schema partizioni senza OTA.";
      setUpdateMessage(uploadError);
      saveOtaUploadStats(uploadError);
      return;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadAllowed) return;

    size_t written = Update.write(upload.buf, upload.currentSize);
    otaWrittenBytes += written;
    if (written != upload.currentSize) {
      uploadAllowed = false;
      uploadError = "ERRORE: scrittura firmware fallita. Scritti " + String((unsigned long)written) + " di " + String((unsigned long)upload.currentSize) + " byte nel blocco corrente.";
      setUpdateMessage(uploadError);
      saveOtaUploadStats(uploadError);
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!uploadAllowed) return;

    if (!Update.end(true)) {
      uploadAllowed = false;
      uploadError = "ERRORE: finalizzazione Update.end(true) fallita. Il firmware NON e' stato installato.";
      setUpdateMessage(uploadError);
      saveOtaUploadStats(uploadError);
      return;
    }
    saveOtaUploadStats("FLASH SCRITTA OK: Update.end(true) completato. Riavvio in attesa.");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadAllowed = false;
    uploadError = "ERRORE: upload abortito dal browser/client";
    Update.abort();
    setUpdateMessage(uploadError);
    saveOtaUploadStats(uploadError);
  }
}

void handleUpdateDone() {
  if (!requireAuth()) return;

  if (!uploadAllowed) {
    saveOtaResult(false, uploadError);
    server.send(200, "text/plain", "OTA ERRORE: " + uploadError + "\nByte scritti: " + String((unsigned long)otaWrittenBytes) + "\nSpazio OTA: " + String((unsigned long)otaFreeAtStart));
    return;
  }

  if (Update.hasError()) {
    String msg = "ERRORE: update fallito durante scrittura/finalizzazione. Il firmware NON e' stato installato.";
    setUpdateMessage(msg);
    saveOtaResult(false, msg);
    saveOtaUploadStats(msg);
    server.send(200, "text/plain", "OTA ERRORE: " + msg);
    return;
  }

  String ok = "OTA FLASH OK: firmware .bin ricevuto, scritto e finalizzato con Update.end(true). Riavvio ESP32 tra circa 10 secondi.";
  setUpdateMessage(ok);
  saveOtaResult(true, ok);
  saveOtaUploadStats(ok);
  prefs.begin("victron", false);
  prefs.putBool("clone_after_ota", true);
  prefs.putString("clone_after_src", "OTA locale");
  prefs.end();

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok + "\nByte scritti: " + String((unsigned long)otaWrittenBytes) + "\nSpazio OTA: " + String((unsigned long)otaFreeAtStart) + "\nLascia aperta la pagina: il polling automatico riprendera quando il WiFi torna online.");

  // V10.4.25: niente secondo reboot automatico post-OTA.
  // Causava: progress 90% -> nero/lampeggi -> ripartenza da 0 al primo avvio.
  otaRestartPending = true;
  otaRestartAtMs = millis() + 10000UL;
}


void handleDiag() {
  if (!requireAuth()) return;

  String html = htmlHeader("Diagnostica");
  html += "<h1>Diagnostica</h1>";
  html += "<p><a href='/'>Dashboard</a> &middot; <a href='/diag.json'>diag.json</a></p>";

  html += "<div class='card'>";
  html += "<p><span class='pill'>Firmware: " + String(FW_VERSION) + "</span></p>";
  html += "<p><b>Ora:</b> " + timeText() + "</p>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>WiFi:</b> ";
  html += WiFi.status() == WL_CONNECTED ? "<span class='diagok'>OK</span>" : "<span class='diagbad'>NO</span>";
  html += " &nbsp; RSSI " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><b>VE.Direct:</b> ";
  html += victronOnline() ? "<span class='diagok'>OK</span>" : "<span class='diagwarn'>non collegato</span>";
  html += "</p>";
  html += "<p><b>Boot count:</b> " + String(bootCounter) + "</p>";
  html += "<p><b>Uptime:</b> " + uptimeText() + "</p>";
  html += "<p><b>Heap libero:</b> " + String(ESP.getFreeHeap()) + " byte</p>";
  html += "<p><b>Ultima riga VE.Direct:</b> " + esc(lastRawLine) + "</p>";
  html += "</div>";

  html += "<div class='card'><h3>Verifica aggiornamento OTA</h3>";
  html += "<p>Questa pagina serve a verificare dopo ogni update se il firmware è realmente cambiato.</p>";
  html += "<p>Versione installata: <b>" + String(FW_VERSION) + "</b></p>";
  html += "</div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleDiagJson() {
  if (!requireAuth()) return;
  sendJsonPretty(diagnosticsJson());
}


void handleInfo() {
  if (!requireAuth()) return;

  String html = htmlHeader("Info");
  html += "<h1>Info Victron Monitor</h1>";
  html += "<p><a href='/'>Dashboard</a></p>";
  html += "<div class='card'>";
  html += "<p><b>Firmware:</b> " + String(FW_NAME) + "</p>";
  html += "<p><b>Versione:</b> " + String(FW_VERSION) + "</p>";
  html += "<p><b>Data compilazione:</b> " + buildDateIt() + "</p>";
  html += "<p><b>Ora compilazione:</b> " + String(FW_BUILD_TIME) + "</p>";
  html += "<p><b>Ultimo OTA:</b> " + prefGet("ota_status", "N/D") + " - " + prefGet("ota_time", "N/D") + "</p>";
  html += "<p><b>Dettaglio OTA:</b> " + esc(prefGet("ota_detail", "N/D")) + "</p>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Hostname:</b> " + String(HOSTNAME) + "</p>";
  html += "<p><b>WiFi setup AP:</b> " + String(WIFI_SETUP_AP) + "</p>";
  html += "<p><b>mDNS:</b> http://" + String(HOSTNAME) + ".local</p>";
  html += "<p><b>RSSI:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><b>VE.Direct:</b> " + String(victronOnline() ? "online" : "non collegato") + "</p>";
  html += "</div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleNotFound() {
  server.send(404, "text/plain", "404");
}

void ensureWiFi() {
  // Public build: do not reconnect using hardcoded credentials.
  // WiFiManager stores credentials in NVS; WiFi.reconnect() reuses those safely.
  if (millis() - lastWiFiCheckMs < 30000UL) return;
  lastWiFiCheckMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(runtimeHostname().c_str());
    WiFi.reconnect();
  }
}



void drawBootProgress(const String& msg, int pct) {
  // V10.4.24: schermata boot senza lampeggi.
  // Prima la V10.4.23 faceva fillScreen() ad ogni step: il pannello sembrava
  // tornare a zero / lampeggiare bianco-nero. Ora disegna la cornice una sola volta
  // e aggiorna solo testo + barra.
  static bool bootScreenDrawn = false;
  static int lastPct = -1;

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  tft.setRotation(displayRotation);

  if (!bootScreenDrawn || pct == 0 || pct < lastPct) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 34, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);
    tft.setCursor(8, 8);
    tft.print("Victron Monitor");

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(12, 58);
    tft.print("Avvio sistema");

    tft.drawRect(12, 128, 296, 22, TFT_DARKGREY);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(12, 220);
    tft.print(FW_VERSION);

    bootScreenDrawn = true;
    lastPct = -1;
  }

  // pulisci solo la riga messaggio
  tft.fillRect(12, 88, 296, 24, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(12, 92);
  tft.print(msg);

  // aggiorna solo interno barra
  int x = 12;
  int y = 128;
  int w = 296;
  int h = 22;
  tft.fillRect(x + 2, y + 2, w - 4, h - 4, TFT_BLACK);
  int fillW = map(pct, 0, 100, 0, w - 4);
  if (fillW > 0) tft.fillRect(x + 2, y + 2, fillW, h - 4, TFT_GREEN);

  tft.fillRect(118, 158, 90, 28, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(132, 162);
  tft.print(String(pct) + "%");

  lastPct = pct;
}

void drawWiFiSetupScreen(const String& msg) {
  tft.setRotation(displayRotation);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 36, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(8, 9);
  tft.print("WiFi Setup");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(12, 55);
  tft.print(msg);

  tft.setTextSize(2);
  tft.setCursor(12, 95);
  tft.print("Rete AP:");

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(12, 120);
  tft.print(WIFI_SETUP_AP);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(12, 155);
  tft.print("Pass:");

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(12, 180);
  tft.print(WIFI_SETUP_PASS);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(12, 225);
  tft.print("Apri 192.168.4.1");
}

bool connectHardcodedWiFi(unsigned long timeoutMs) {
  // Public build: no hardcoded WiFi credentials.
  // Try only previously saved WiFiManager/ESP32 credentials.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(runtimeHostname().c_str());

  drawBootProgress("Connessione WiFi salvata", 55);
  WiFi.begin();

  unsigned long start = millis();
  int lastDots = -1;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    ArduinoOTA.handle();
    delay(250);

    int dots = ((millis() - start) / 1000) % 4;
    if (dots != lastDots) {
      lastDots = dots;
      String m = "Connessione WiFi salvata";
      for (int i = 0; i < dots; i++) m += ".";
      int pct = 55 + (int)((millis() - start) * 12UL / timeoutMs);
      if (pct > 67) pct = 67;
      drawBootProgress(m, pct);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    addEventLog("WIFI", "Connesso IP " + WiFi.localIP().toString() + " RSSI " + String(WiFi.RSSI()));
    drawBootProgress("WiFi OK: " + WiFi.localIP().toString(), 70);
    delay(300);
    return true;
  }
  return false;
}

void startWiFiManager(bool forcePortal) {
  // Public build: generic WiFiManager, no private SSID/password.
  String host = runtimeHostname();
  String apSsid = runtimeSetupApSsid();
  String apPass = runtimeSetupApPass();

  wifiManager.setHostname(host.c_str());
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(30);

  if (!forcePortal) {
    if (connectHardcodedWiFi(30000UL)) return;
  }

  if (forcePortal) drawWiFiSetupScreen("Portale configurazione");
  else drawWiFiSetupScreen("Setup WiFi AP");

  bool ok = wifiManager.startConfigPortal(apSsid.c_str(), apPass.c_str());
  if (!ok && WiFi.status() != WL_CONNECTED) {
    drawWiFiSetupScreen("Offline: credenziali mancanti");
    delay(1200);
    WiFi.mode(WIFI_STA);
  }
}



void robustTftInit() {
  // V10.4.15 CYD DELAYED FINAL INIT
  // Obiettivo: init stabile anche dopo power-cycle/reset manuale.
  // Il TFT resta alimentato durante il reset ESP32, quindi il bus va messo subito
  // in stato sicuro e il controller ILI9341 va forzato a rientrare in init pulita.
  // Nessuna reinit nel loop.

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);     // backlight spento durante init

#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);    // TFT non selezionato
#endif
#ifdef TFT_DC
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_DC, HIGH);
#endif
#ifdef TFT_MOSI
  pinMode(TFT_MOSI, OUTPUT);
  digitalWrite(TFT_MOSI, LOW);
#endif
#ifdef TFT_SCLK
  pinMode(TFT_SCLK, OUTPUT);
  digitalWrite(TFT_SCLK, LOW);
#endif
#ifdef TFT_MISO
  pinMode(TFT_MISO, INPUT);
#endif

  delay(2500);                   // cold boot: lascia stabilizzare TFT e alimentazione

  // Prima init solo per riagganciare SPI e poter mandare SWRESET.
  tft.begin();
  delay(80);
  tft.writecommand(0x01);        // ILI9341 SWRESET
  delay(500);
  tft.writecommand(0x11);        // SLPOUT
  delay(180);
  tft.writecommand(0x29);        // DISPON
  delay(180);

  // Init reale dopo SWRESET.
  tft.begin();
  delay(180);
  tft.setRotation(displayRotation);
  delay(50);
  tft.fillScreen(TFT_BLACK);
  delay(120);

  // V10.4.23: non accendere qui la retroilluminazione.
  // La accendiamo dopo aver disegnato la schermata di caricamento.
  delay(120);
}

void drawTftTestPattern() {
  robustTftInit();
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 80, 80, TFT_RED);
  tft.fillRect(80, 0, 80, 80, TFT_GREEN);
  tft.fillRect(160, 0, 80, 80, TFT_BLUE);
  tft.fillRect(240, 0, 80, 80, TFT_YELLOW);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 105);
  tft.print("TFT TEST OK");
  tft.setTextSize(1);
  tft.setCursor(12, 135);
  tft.print(FW_VERSION);
  tft.setCursor(12, 155);
  tft.print("Se vedi colori, display OK.");
  setBacklight(true);
}

void handleTftTestPage() {
  if (!requireAuth()) return;
  drawTftTestPattern();
  String html = htmlHeader("Test Display TFT");
  html += "<h1>Test Display TFT</h1>";
  html += "<div class='card'>";
  html += "<p>Ho reinizializzato il display con profilo CYD ILI9341/User_Setup GitHub e disegnato un pattern colori.</p>";
  html += "<p>Se il display resta bianco anche ora, il problema e' quasi certamente configurazione TFT_eSPI/driver ILI9341 o pinout, non la dashboard.</p>";
  html += "<a class='button' href='/tft-test'>Riprova test TFT</a> ";
  html += "<a class='button' href='/'>Dashboard</a> ";
  html += "<a class='button' href='/updates'>Aggiornamenti</a>";
  html += "</div>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}


// ========================= V10.4.63 - Recovery / alert history / retention / theme =========================
String uiTheme() {
  prefs.begin("victron", true);
  String t = prefs.getString("web_theme", "dark");
  prefs.end();
  if (t != "dark" && t != "light" && t != "victron" && t != "compact") t = "dark";
  return t;
}

String readSmallSdFile(const String& path, size_t maxBytes = 12000) {
  if (!sdMounted) sdMount(false);
  if (!sdMounted || !SD.exists(path)) return "";
  File f = SD.open(path, FILE_READ);
  if (!f) return "";
  String out;
  while (f.available() && out.length() < maxBytes) out += (char)f.read();
  f.close();
  return out;
}

String backupManifestSummary(const String& folder) {
  String m = readSmallSdFile(folder + "/manifest.json", 3000);
  if (m.length() == 0) return "Manifest non presente";
  String fw = extractJsonStringValue(m, "firmware", "N/D");
  String reason = extractJsonStringValue(m, "reason", "N/D");
  return "Firmware: " + esc(fw) + "<br>Tipo: " + esc(reason);
}

void handleBackupRecoveryProPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Backup / Recovery Pro");
  html += "<h1>Backup / Recovery Pro</h1><p><a href='/'>Dashboard</a> &middot; <a href='/backup-recovery'>Backup</a> &middot; <a href='/daily-backups'>Backup giornalieri</a></p>";
  html += card("Modalita sicura", "Attiva", "Da qui puoi ripristinare firmware completo oppure solo configurazione da un backup SD.");
  if (!sdMounted) {
    html += card("MicroSD", "NO", esc(sdLastStatus));
  } else {
    File dir = SD.open("/backup_recovery");
    if (!dir || !dir.isDirectory()) {
      html += card("Backup completi", "Nessuno", "Cartella /backup_recovery assente");
    } else {
      html += "<div class='card'><div class='t'>Backup completi disponibili</div><table class='dataTable'><tr><td>Backup</td><td>Azioni</td></tr>";
      File e = dir.openNextFile();
      bool any = false;
      while (e) {
        if (e.isDirectory()) {
          any = true;
          String p = String(e.name());
          if (!p.startsWith("/")) p = "/backup_recovery/" + p;
          html += "<tr><td style='text-align:left'><b>" + esc(fileNameOnly(p)) + "</b><br>" + backupManifestSummary(p) + "</td><td>";
          html += "<a class='button' href='/backup-restore?p=" + urlEncode(p) + "'>Firmware</a> ";
          html += "<a class='button' href='/backup-config-restore?p=" + urlEncode(p) + "'>Solo config</a> ";
          html += "<a class='button' href='/sd-files?p=" + urlEncode(p) + "'>File</a></td></tr>";
        }
        e.close(); e = dir.openNextFile(); yield();
      }
      if (!any) html += "<tr><td>Nessun backup</td><td>-</td></tr>";
      html += "</table></div>";
      dir.close();
    }
  }
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void applySettingsJsonBody(const String& body) {
  prefs.begin("victron", false);
  String v;
  v = extractJsonStringValue(body, "github_version_url", ""); if (v.length()) prefs.putString("gh_ver_url", v);
  v = extractJsonStringValue(body, "github_bin_url", ""); if (v.length()) prefs.putString("gh_bin_url", v);
  v = extractJsonStringValue(body, "github_sha_url", ""); if (v.length()) prefs.putString("gh_sha_url", v);
  v = extractJsonStringValue(body, "github_log_url", ""); if (v.length()) prefs.putString("gh_log_url", v);
  float mult = extractJsonFloatValue(body, "esp_bat_mult", -1); if (mult > 1.0 && mult < 4.0) prefs.putFloat("esp_bat_mult", mult);
  int gd = extractJsonIntValue(body, "gh_auto_day", -1); if (gd >= 0 && gd <= 6) prefs.putInt("gh_auto_day", gd);
  int gh = extractJsonIntValue(body, "gh_auto_hour", -1); if (gh >= 0 && gh <= 23) prefs.putInt("gh_auto_hour", gh);
  int gm = extractJsonIntValue(body, "gh_auto_min", -1); if (gm >= 0 && gm <= 59) prefs.putInt("gh_auto_min", gm);
  int lcds = extractJsonIntValue(body, "lcd_auto_sec", -1); if (lcds >= 0 && lcds <= 86400) prefs.putInt("lcd_auto_sec", lcds);
  int rotr = extractJsonIntValue(body, "tft_rotation", -1); if (rotr == 1 || rotr == 3) prefs.putInt("tft_rotation", rotr);
  int tnm = extractJsonIntValue(body, "touch_nav_mode", -1); if (tnm == 0 || tnm == 1) prefs.putInt("touch_nav_mode", tnm);
  prefs.end();
}

void handleBackupConfigRestoreFromSd() {
  if (!requireAuth()) return;
  String p = server.arg("p");
  if (!safeSdBrowserPath(p) || !p.startsWith("/backup_recovery")) { sendActionPage("Restore config", "Percorso non valido.", 3, "/backup-recovery-pro"); return; }
  if (!sdMounted) sdMount(false);
  String cfg = p + "/config.json";
  if (!sdMounted || !SD.exists(cfg)) { sendActionPage("Restore config", "config.json non trovato nel backup.", 3, "/backup-recovery-pro"); return; }
  if (server.arg("confirm") != "YES") {
    String html = htmlHeader("Restore solo config");
    html += "<h1>Restore solo configurazione</h1><p><a href='/backup-recovery-pro'>Indietro</a></p>";
    html += card("Backup", esc(p), "Ripristina solo impostazioni: URL OTA, calibrazione batteria, rotazione, auto LCD, touch e scheduler. Non scrive firmware.");
    html += "<div class='card warn'><p><a class='button danger' href='/backup-config-restore?p=" + urlEncode(p) + "&confirm=YES' onclick=\"return confirm('Ripristinare solo configurazione da SD?')\">Conferma restore config</a></p></div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }
  String body = readSmallSdFile(cfg, 20000);
  if (body.length() < 10) { sendActionPage("Restore config", "config.json vuoto o non leggibile.", 3, "/backup-recovery-pro"); return; }
  applySettingsJsonBody(body);
  addEventLog("RESTORE", "Config ripristinata da " + p);
  sendActionPage("Restore config", "Configurazione ripristinata. Riavvia per applicare tutti i valori.", 4, "/settings");
}

void pruneDailyConfigBackups(int keep) {
  if (!sdMounted) return;
  if (keep < 1) keep = 1; if (keep > 14) keep = 14;
  sdEnsureDir("/backup_auto");
  String newest[14];
  File dir = SD.open("/backup_auto");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  File e = dir.openNextFile();
  while (e) {
    if (!e.isDirectory()) {
      String n = String(e.name()); if (!n.startsWith("/")) n = "/backup_auto/" + n;
      if (n.endsWith(".json")) {
        for (int i = 0; i < keep; i++) if (n > newest[i]) { for (int j = keep - 1; j > i; j--) newest[j] = newest[j-1]; newest[i] = n; break; }
      }
    }
    e.close(); e = dir.openNextFile(); yield();
  }
  dir.close();
  dir = SD.open("/backup_auto"); if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  e = dir.openNextFile();
  while (e) {
    if (!e.isDirectory()) {
      String n = String(e.name()); if (!n.startsWith("/")) n = "/backup_auto/" + n;
      bool keepThis = false; for (int i = 0; i < keep; i++) if (n == newest[i]) keepThis = true;
      if (n.endsWith(".json") && !keepThis) SD.remove(n);
    }
    e.close(); e = dir.openNextFile(); yield();
  }
  dir.close();
}

bool createDailyConfigBackupNow(String& pathOut) {
  if (!sdEnsureReadyForWrite()) return false;
  sdEnsureDir("/backup_auto");
  String date = isoDateForFile();
  pathOut = "/backup_auto/config_" + date + ".json";
  File f = SD.open(pathOut, FILE_WRITE);
  if (!f) return false;
  f.print(settingsBackupJson());
  f.close();
  pruneDailyConfigBackups(prefsGetIntSafe("daily_backup_keep", 5));
  return true;
}

void dailyConfigBackupLoop() {
  if (millis() - lastDailyConfigBackupCheckMs < 60000UL) return;
  lastDailyConfigBackupCheckMs = millis();
  if (!timeIsValid()) return;
  String today = isoDateForFile();
  prefs.begin("victron", false);
  String last = prefs.getString("daily_cfg_bak", "");
  prefs.end();
  if (last == today) return;
  String p;
  if (createDailyConfigBackupNow(p)) {
    prefs.begin("victron", false); prefs.putString("daily_cfg_bak", today); prefs.end();
    addEventLog("BACKUP", "Backup config giornaliero: " + p);
  }
}

void handleDailyBackupsPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String html = htmlHeader("Backup config giornalieri");
  html += "<h1>Backup config giornalieri</h1><p><a href='/'>Dashboard</a> &middot; <a href='/backup-recovery-pro'>Recovery Pro</a></p>";
  html += card("Automatico", "1 volta/giorno", "Mantiene gli ultimi 5 backup config in /backup_auto.<br><a class='button' href='/daily-backup-now'>Crea ora</a>");
  if (!sdMounted) html += card("MicroSD", "NO", esc(sdLastStatus));
  else {
    sdEnsureDir("/backup_auto");
    File dir = SD.open("/backup_auto");
    html += "<div class='card'><div class='t'>File disponibili</div><table class='dataTable'><tr><td>File</td><td>Azione</td></tr>";
    if (dir && dir.isDirectory()) {
      File e = dir.openNextFile(); bool any=false;
      while (e) { if (!e.isDirectory()) { any=true; String n=String(e.name()); if(!n.startsWith("/")) n="/backup_auto/"+n; html += "<tr><td style='text-align:left'>"+esc(fileNameOnly(n))+"</td><td><a class='button' href='/sd-view?p="+urlEncode(n)+"'>Apri</a> <a class='button' href='/sd-download?p="+urlEncode(n)+"'>Scarica</a></td></tr>"; } e.close(); e=dir.openNextFile(); }
      if (!any) html += "<tr><td>Nessun file</td><td>-</td></tr>";
      dir.close();
    }
    html += "</table></div>";
  }
  html += "</body></html>"; server.send(200, "text/html; charset=utf-8", html);
}

void handleDailyBackupNow() {
  if (!requireAuth()) return;
  String p;
  bool ok = createDailyConfigBackupNow(p);
  sendActionPage("Backup config", ok ? ("Creato: " + p) : ("Errore: " + sdLastStatus), 3, "/daily-backups");
}

String alertSignatureText() {
  String sig;
  if (WiFi.status() != WL_CONNECTED) sig += "WiFi offline;";
  else if (WiFi.RSSI() < thresholdWifiWeak()) sig += "WiFi debole " + String(WiFi.RSSI()) + "dBm;";
  if (!victronOnline()) sig += "VE.Direct no data;";
  if (!isnan(battV) && battV > 1 && battV < thresholdBattLow()) sig += "Batteria impianto bassa " + String(battV,2) + "V;";
  if (!isnan(espBatteryPercent()) && espBatteryPercent() < thresholdEspBatLow()) sig += "Batteria ESP bassa " + String(espBatteryPercent(),0) + "%;";
  if (errorState != "0" && errorState != "N/D") sig += "Errore MPPT " + errorState + ";";
  if (!sdMounted && storageUseSd()) sig += "SD non montata;";
  if (sdMounted && SD.totalBytes() > 0) { int used = (int)((SD.usedBytes()*100ULL)/SD.totalBytes()); if (used >= thresholdSdFullPercent()) sig += "SD quasi piena " + String(used) + "%;"; }
  if (sig.length() == 0) sig = "OK";
  return sig;
}

void alertHistoryLoop() {
  if (millis() - lastAlertHistoryCheckMs < 45000UL) return;
  lastAlertHistoryCheckMs = millis();
  String sig = alertSignatureText();
  if (sig == lastAlertSignatureStored) return;
  lastAlertSignatureStored = sig;
  addEventLog("ALERT", sig);
  if (sdEnsureReadyForWrite()) {
    sdEnsureDir("/logs");
    String path = "/logs/alerts_" + sdMonthDir() + ".csv";
    bool isNew = !SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (f) { if (isNew) f.println("time,alert"); f.println(csvEscape(timeText()) + "," + csvEscape(sig)); f.close(); }
  }
}

void handleAlertsHistoryPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  String path = "/logs/alerts_" + sdMonthDir() + ".csv";
  String html = htmlHeader("Storico alert");
  html += "<h1>Storico alert</h1><p><a href='/alerts'>Alert</a> &middot; <a href='/logs'>Log eventi</a> &middot; <a href='/sd-files?p=/logs'>Cartella logs</a></p>";
  html += card("Stato attuale", alertCountNow() ? String(alertCountNow()) + " alert" : "OK", esc(alertSignatureText()));
  if (!sdMounted || !SD.exists(path)) html += card("Storico SD", "N/D", "Nessun file alert mensile ancora creato.");
  else html += "<div class='card'><div class='t'>File mensile</div><p><a class='button' href='/sd-view?p=" + urlEncode(path) + "'>Apri CSV</a> <a class='button' href='/sd-download?p=" + urlEncode(path) + "'>Scarica</a></p><pre style='white-space:pre-wrap;overflow:auto;background:#0b1220;border:1px solid #30363d;border-radius:14px;padding:12px'>" + esc(readSmallSdFile(path, 12000)) + "</pre></div>";
  html += "</body></html>"; server.send(200, "text/html; charset=utf-8", html);
}

void handleSdRetentionPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Retention SD");
  html += "<h1>Retention / Pulizia automatica</h1><p><a href='/sd-maintenance'>Manutenzione SD</a> &middot; <a href='/storage'>Storage</a></p>";
  int logKeep = prefsGetIntSafe("log_keep_days", 90);
  int backupKeep = prefsGetIntSafe("backup_keep", 5);
  int dailyKeep = prefsGetIntSafe("daily_backup_keep", 5);
  html += "<form method='POST' action='/sd-retention-save'><div class='card'><div class='t'>Regole conservazione</div>";
  html += "<p>Mantieni log giornalieri recenti<br><select name='logkeep'>";
  int vals[] = {7,30,90,180,365,9999};
  const char* labs[] = {"7 giorni","30 giorni","90 giorni","180 giorni","365 giorni","Sempre"};
  for (int i=0;i<6;i++) html += "<option value='" + String(vals[i]) + "'" + String(logKeep==vals[i]?" selected":"") + ">" + labs[i] + "</option>";
  html += "</select></p>";
  html += "<p>Mantieni backup recovery<br><select name='bkeep'>";
  for (int i=2;i<=5;i++) html += "<option value='" + String(i) + "'" + String(backupKeep==i?" selected":"") + ">Ultimi " + String(i) + "</option>";
  html += "</select></p>";
  html += "<p>Mantieni backup configurazione giornalieri<br><select name='dkeep'>";
  for (int i=2;i<=5;i++) html += "<option value='" + String(i) + "'" + String(dailyKeep==i?" selected":"") + ">Ultimi " + String(i) + "</option>";
  html += "</select></p><p><button class='button' type='submit'>Salva regole</button> <a class='button danger' href='/sd-retention?apply=1' onclick=\"return confirm('Applicare ora pulizia automatica?')\">Applica ora</a></p></div></form>";
  if (server.hasArg("apply")) { applySdRetentionPolicy(); html += "<div class='banner'>Pulizia retention applicata.</div>"; }
  html += "</body></html>"; server.send(200, "text/html; charset=utf-8", html);
}

void handleSdRetentionSave() {
  if (!requireAuth()) return;
  prefsPutIntSafe("log_keep_days", server.arg("logkeep").toInt());
  prefsPutIntSafe("backup_keep", server.arg("bkeep").toInt());
  if (server.hasArg("dkeep")) prefsPutIntSafe("daily_backup_keep", server.arg("dkeep").toInt());
  pruneBackupBackupsKeepTwo();
  pruneDailyConfigBackups(prefsGetIntSafe("daily_backup_keep", 5));
  sendActionPage("Retention SD", "Regole salvate.", 2, "/sd-retention");
}

void applySdRetentionPolicy() {
  if (!sdMounted) sdMount(false);
  if (!sdMounted) return;
  int keepDays = prefsGetIntSafe("log_keep_days", 90);
  if (keepDays >= 9999) { pruneBackupBackupsKeepTwo(); pruneDailyConfigBackups(prefsGetIntSafe("daily_backup_keep", 5)); return; }
  // Politica prudente: conserva almeno il file di oggi e rimuove solo log giornalieri più vecchi del numero massimo di file.
  String newest[90]; int maxKeep = keepDays; if (maxKeep > 90) maxKeep = 90;
  File root = SD.open("/logs");
  if (root && root.isDirectory()) {
    File month = root.openNextFile();
    while (month) {
      if (month.isDirectory()) {
        File f = month.openNextFile();
        while (f) { if (!f.isDirectory()) { String p=String(f.name()); if(!p.startsWith("/")) p=String(month.name())+"/"+p; if(p.endsWith(".csv") && p.indexOf("alerts_")<0) { for(int i=0;i<maxKeep;i++) if(p>newest[i]) { for(int j=maxKeep-1;j>i;j--) newest[j]=newest[j-1]; newest[i]=p; break; } } } f.close(); f=month.openNextFile(); yield(); }
      }
      month.close(); month = root.openNextFile(); yield();
    }
    root.close();
    root = SD.open("/logs");
    month = root.openNextFile();
    while (month) {
      if (month.isDirectory()) {
        File f = month.openNextFile();
        while (f) { if (!f.isDirectory()) { String p=String(f.name()); if(!p.startsWith("/")) p=String(month.name())+"/"+p; bool keep=false; for(int i=0;i<maxKeep;i++) if(p==newest[i]) keep=true; if(p.endsWith(".csv") && p.indexOf("alerts_")<0 && !keep) SD.remove(p); } f.close(); f=month.openNextFile(); yield(); }
      }
      month.close(); month = root.openNextFile(); yield();
    }
    root.close();
  }
  pruneBackupBackupsKeepTwo();
  pruneDailyConfigBackups(prefsGetIntSafe("daily_backup_keep", 5));
}

void handleDiagnosticRunPage() {
  if (!requireAuth()) return;
  if (!sdMounted) sdMount(false);
  int score = 100;
  String rows;
  auto addRow = [&](const String& name, bool ok, const String& detail, int penalty) {
    if (!ok) score -= penalty;
    rows += "<tr><td>" + esc(name) + "</td><td>" + String(ok?"OK":"ATTENZIONE") + "</td><td>" + esc(detail) + "</td></tr>";
  };
  addRow("WiFi", WiFi.status()==WL_CONNECTED, String(WiFi.RSSI()) + " dBm", 15);
  addRow("VE.Direct", victronOnline(), victronOnline()?"Dati recenti":"Nessun dato recente", 20);
  addRow("MicroSD", sdMounted, sdMounted?formatBytes64(SD.totalBytes()-SD.usedBytes())+" liberi":sdLastStatus, 15);
  addRow("LittleFS", littleFsReady, littleFsReady?"OK":"Non pronto", 10);
  addRow("BAT ESP", isnan(espBatteryPercent())?false:(espBatteryPercent()>=thresholdEspBatLow()), String(isnan(espBatteryPercent())?0:espBatteryPercent(),0)+"%", 10);
  addRow("Heap", ESP.getFreeHeap()>45000, String(ESP.getFreeHeap()) + " byte", 10);
  addRow("OTA URL", getGithubVersionUrl().length()>5 && getGithubBinUrl().length()>5, "Version/BIN configurati", 10);
  addRow("Backup recovery", sdMounted && SD.exists("/backup_recovery"), "Cartella backup_recovery", 10);
  if (score < 0) score = 0;
  String html = htmlHeader("Diagnostica rapida");
  html += "<h1>Diagnostica rapida</h1><p><a href='/'>Dashboard</a> &middot; <a href='/diag-snapshot'>Crea snapshot</a> &middot; <a href='/health'>Health</a></p>";
  html += "<div class='batHero'><div class='t'>Risultato</div><div class='batValue'>" + String(score) + "/100</div><div class='batGauge'><div class='batFill' style='width:" + String(score) + "%'></div></div></div>";
  html += "<div class='card'><table class='dataTable'>" + rows + "</table></div>";
  html += "</body></html>"; server.send(200, "text/html; charset=utf-8", html);
}

void handleThemePage() {
  if (!requireAuth()) return;
  String cur = uiTheme();
  String html = htmlHeader("Tema Web UI");
  html += "<h1>Tema Web UI</h1><p><a href='/'>Dashboard</a> &middot; <a href='/settings'>Settings</a></p>";
  html += "<form method='POST' action='/theme-save'><div class='card'><div class='t'>Aspetto</div><p><select name='theme'>";
  const char* vals[] = {"dark","victron","light","compact"};
  const char* labs[] = {"Scuro GX","Blu Victron","Chiaro","Compatto telefono"};
  for (int i=0;i<4;i++) html += "<option value='" + String(vals[i]) + "'" + String(cur==vals[i]?" selected":"") + ">" + labs[i] + "</option>";
  html += "</select></p><p><button class='button' type='submit'>Salva tema</button></p></div></form>";
  html += "</body></html>"; server.send(200, "text/html; charset=utf-8", html);
}

void handleThemeSave() {
  if (!requireAuth()) return;
  String t = server.arg("theme");
  if (t != "dark" && t != "light" && t != "victron" && t != "compact") t = "dark";
  prefs.begin("victron", false); prefs.putString("web_theme", t); prefs.end();
  sendActionPage("Tema Web UI", "Tema salvato: " + t, 2, "/theme");
}



// ========================= V10.4.64 TECH PRO SAFE ADDITIONS =========================

String latestRecoveryFolder() {
  if (!sdMounted && !sdMount(false)) return "";
  File root = SD.open("/backup_recovery");
  if (!root || !root.isDirectory()) return "";
  String best = "";
  File f = root.openNextFile();
  while (f) {
    if (f.isDirectory()) {
      String name = String(f.name());
      if (!name.startsWith("/")) name = "/backup_recovery/" + name;
      if (name > best) best = name;
    }
    f = root.openNextFile();
  }
  return best;
}

String findFirmwareInRecoveryFolder(const String& folder) {
  if (folder.length() == 0) return "";
  String f1 = folder + "/firmware.bin";
  if (SD.exists(f1)) return f1;
  String f2 = folder + "/latest.bin";
  if (SD.exists(f2)) return f2;
  return "";
}

bool restoreFirmwareFromSdFile(const String& filePath, String& detailOut) {
  if (!sdMounted && !sdMount(false)) { detailOut = "microSD non montata"; return false; }
  String p = normalizeSdPath(filePath);
  if (!safeSdBrowserPath(p) || !p.endsWith(".bin")) { detailOut = "Percorso firmware non valido"; return false; }
  if (!SD.exists(p)) { detailOut = "File firmware non trovato: " + p; return false; }
  File f = SD.open(p, FILE_READ);
  if (!f) { detailOut = "Impossibile aprire firmware"; return false; }
  size_t size = f.size();
  if (size < 200000 || size > ESP.getFreeSketchSpace()) {
    detailOut = "Dimensione firmware non valida: " + String((unsigned long)size) + " bytes";
    f.close();
    return false;
  }
  if (!Update.begin(size)) {
    detailOut = "Update.begin fallito: " + String(Update.errorString());
    f.close();
    return false;
  }
  size_t written = Update.writeStream(f);
  f.close();
  if (written != size) {
    detailOut = "Scrittura incompleta: " + String((unsigned long)written) + "/" + String((unsigned long)size);
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    detailOut = "Update.end fallito: " + String(Update.errorString());
    return false;
  }
  addEventLog("RECOVERY", "Restore firmware da SD: " + p);
  detailOut = "Firmware ripristinato da " + p + ". Riavvio in corso.";
  otaRestartPending = true;
  otaRestartAtMs = millis() + 1500;
  return true;
}

void handleRecoveryRestoreProPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Restore firmware da SD");
  html += "<div class='top'><h1>Restore firmware da Backup / Recovery</h1><div class='sub'>Ripristino OTA da firmware.bin salvato sulla microSD, con controlli base di sicurezza.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/backup-recovery'>Backup / Recovery</a><a href='/backup-list'>Lista backup</a><a href='/sd-files?p=/backup_recovery'>Apri cartella SD</a></div></div>";
  if (!sdMounted && !sdMount(false)) {
    html += "<div class='card warn'><div class='t'>MicroSD non pronta</div><div class='v'>" + esc(sdLastStatus) + "</div><p>Monta la microSD prima di usare il restore firmware.</p></div>";
    html += String("</body></html>"); server.send(200, "text/html", html); return;
  }
  String latest = latestRecoveryFolder();
  String fw = findFirmwareInRecoveryFolder(latest);
  html += "<div class='grid'>";
  html += card("Backup piu' recente", latest.length()?latest:"N/D", "Cartella recovery rilevata automaticamente");
  html += card("Firmware trovato", fw.length()?fw:"N/D", fw.length()?"Pronto per verifica e restore":"Nessun firmware.bin/latest.bin trovato");
  html += card("Spazio OTA libero", formatBytes64(ESP.getFreeSketchSpace()), "Dimensione massima sketch scrivibile");
  html += "</div>";
  if (fw.length()) {
    html += "<div class='card danger'><div class='t'>Azione delicata</div><div class='v'>Ripristina firmware</div><p>Questa operazione scrive una nuova partizione OTA e riavvia l'ESP32. Usala solo se devi tornare a un backup funzionante.</p>";
    html += "<p><a class='button danger' href='/recovery-restore-firmware?file=" + urlEncode(fw) + "' onclick=\"return confirm('Ripristinare il firmware da microSD e riavviare?')\">Ripristina questo firmware</a></p></div>";
  }
  html += "<div class='card'><div class='t'>Restore manuale</div><p>Puoi anche passare un file specifico con <code>/recovery-restore-firmware?file=/backup_recovery/.../firmware.bin</code>.</p></div>";
  html += String("</body></html>");
  server.send(200, "text/html", html);
}

void handleRecoveryRestoreFirmwareStart() {
  if (!requireAuth()) return;
  String file = server.hasArg("file") ? server.arg("file") : "";
  if (file.length() == 0) {
    String latest = latestRecoveryFolder();
    file = findFirmwareInRecoveryFolder(latest);
  }
  String detail;
  bool ok = restoreFirmwareFromSdFile(file, detail);
  sendActionPage("Restore firmware", detail, ok ? 2 : 4, ok ? "/" : "/recovery-restore-pro");
}

void recordRebootHistory() {
  if (!littleFsReady) return;
  File f = LittleFS.open("/reboot_history.log", FILE_APPEND);
  if (!f) return;
  f.print(timeText()); f.print(",");
  f.print(resetReasonText()); f.print(",boot="); f.print(bootCounter);
  f.print(",fw="); f.println(FW_VERSION);
  f.close();
}

void handleRebootHistoryPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Storico reboot");
  html += "<div class='top'><h1>Storico reboot / brownout</h1><div class='sub'>Ultimi riavvii registrati in LittleFS.</div><div class='nav'><a href='/'>Dashboard</a><a href='/power'>Power</a><a href='/reboot-history.json'>JSON</a></div></div>";
  html += "<div class='card'><div class='t'>Ultimo reboot</div><div class='v'>" + esc(resetReasonText()) + "</div><p>Boot count: " + String(bootCounter) + "</p></div>";
  html += "<div class='card'><pre style='white-space:pre-wrap;max-height:420px;overflow:auto'>";
  if (LittleFS.exists("/reboot_history.log")) {
    File f = LittleFS.open("/reboot_history.log", FILE_READ);
    if (f) { while (f.available()) html += esc(f.readStringUntil('\n')) + "\n"; f.close(); }
  } else html += "Nessuno storico reboot ancora disponibile.";
  html += "</pre></div>" + String("</body></html>");
  server.send(200, "text/html", html);
}

void handleRebootHistoryJson() {
  if (!requireAuth()) return;
  String j = "{\"last_reason\":\"" + esc(resetReasonText()) + "\",\"boot_count\":" + String(bootCounter) + ",\"log\":";
  String lines = "";
  if (LittleFS.exists("/reboot_history.log")) { File f = LittleFS.open("/reboot_history.log", FILE_READ); if (f) { lines = f.readString(); f.close(); } }
  lines.replace("\\", "\\\\"); lines.replace("\"", "\\\""); lines.replace("\n", "\\n");
  j += "\"" + lines + "\"}";
  sendJsonPretty(j);
}

void appWatchdogLoop() {
  appWatchdogLastLoopMs = millis();
  uint32_t heap = ESP.getFreeHeap();
  if (heap < minFreeHeapSeen) minFreeHeapSeen = heap;
  if (heap < 35000) appWatchdogLastStatus = "Heap basso";
  else if (WiFi.status() != WL_CONNECTED) appWatchdogLastStatus = "WiFi offline";
  else appWatchdogLastStatus = "OK";
}

void handleAppWatchdogPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Watchdog applicativo");
  html += "<div class='top'><h1>Watchdog applicativo</h1><div class='sub'>Controlli logici su loop, WiFi, heap, VE.Direct e logger SD.</div><div class='nav'><a href='/'>Dashboard</a><a href='/power'>Power</a><a href='/diagnostic-run'>Diagnostica rapida</a></div></div>";
  html += "<div class='grid'>";
  html += card("Stato", appWatchdogLastStatus, "Controllo interno loop");
  html += card("Heap minimo visto", formatBytes64(minFreeHeapSeen), "Heap attuale: " + formatBytes64(ESP.getFreeHeap()));
  html += card("VE.Direct", victronOnline()?"OK":"No Data", "Ultimo dato: " + String((millis()-lastVictronMs)/1000UL) + " s fa");
  html += card("Logger SD", lastSdLogStatus, "Ultimo OK: " + String((millis()-lastSdLogOkMs)/1000UL) + " s fa");
  html += "</div>" + String("</body></html>");
  server.send(200, "text/html", html);
}

void handleTimeNtpPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Ora / NTP");
  html += "<div class='top'><h1>Ora / NTP</h1><div class='sub'>Orario reale per log SD, backup e statistiche.</div><div class='nav'><a href='/'>Dashboard</a><a href='/ntp-sync'>Sincronizza ora</a><a href='/logs'>Log eventi</a></div></div>";
  html += "<div class='grid'>";
  html += card("Ora attuale", timeText(), timeIsValid()?"NTP valido":"Ora non ancora sincronizzata");
  html += card("Timezone", "Italia CET/CEST", String(TZ_INFO));
  html += card("WiFi", WiFi.status()==WL_CONNECTED?"Connesso":"Offline", WiFi.localIP().toString());
  html += "</div><div class='card'><p><a class='button' href='/ntp-sync'>Forza sincronizzazione NTP</a></p></div>" + String("</body></html>");
  server.send(200, "text/html", html);
}

void handleNtpSyncNow() {
  if (!requireAuth()) return;
  syncNtpNow();
  sendActionPage("NTP", timeIsValid()?"Ora sincronizzata: "+timeText():"Sincronizzazione avviata, riprova tra qualche secondo.", 2, "/time-ntp");
}

void addVedirectRawLine(const String& line) {
  if (line.length() == 0) return;
  vedirectRawRing[vedirectRawPos] = String(millis()/1000UL) + "s " + line;
  vedirectRawPos = (vedirectRawPos + 1) % VEDIRECT_RAW_COUNT;
  if (vedirectRawStored < VEDIRECT_RAW_COUNT) vedirectRawStored++;
  appWatchdogLastVictronParserMs = millis();
}

void handleVedirectRawPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("VE.Direct RAW");
  html += "<div class='top'><h1>VE.Direct RAW</h1><div class='sub'>Ultime righe ricevute dal Victron, utili per debug cavo e parser.</div><div class='nav'><a href='/'>Dashboard</a><a href='/victron-data'>Dati convertiti</a><a href='/vedirect-raw.json'>JSON</a><a href='/vedirect-restart'>Riavvia UART</a></div></div>";
  html += "<div class='grid'>";
  html += card("Stato", victronOnline()?"Online":"No Data", "Ultima riga: " + String((millis()-lastVictronMs)/1000UL) + " s fa");
  html += card("Porta", String(VICTRON_PORT_NAME), String(VICTRON_PORT_DETAIL));
  html += card("Diagnostica UART", String(vedirectByteCount) + " byte", "Righe OK: " + String(vedirectParsedLineCount) + "<br>Righe scartate: " + String(vedirectBadLineCount) + "<br>Ultimo byte: " + String(lastVictronByteMs ? (millis()-lastVictronByteMs)/1000UL : 999999UL) + " s fa<br>Restart UART: " + String(vedirectReinitCount) + "<br>Motivo: " + esc(lastVictronReinitReason));
  html += card("Ultima riga", esc(lastRawLine), "Raw parser");
  html += "</div><div class='card'><pre style='white-space:pre-wrap;max-height:420px;overflow:auto'>";
  for (int i=0;i<vedirectRawStored;i++) { int idx = (vedirectRawPos - vedirectRawStored + i + VEDIRECT_RAW_COUNT) % VEDIRECT_RAW_COUNT; html += esc(vedirectRawRing[idx]) + "\n"; }
  html += "</pre></div>" + String("</body></html>");
  server.send(200, "text/html", html);
}

void handleVedirectRawJson() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  String j = "{\"online\":" + String(victronOnline()?"true":"false") + ",\"last_seconds\":" + String((millis()-lastVictronMs)/1000UL) + ",\"last_byte_seconds\":" + String(lastVictronByteMs ? (millis()-lastVictronByteMs)/1000UL : 999999UL) + ",\"port\":\"" + String(VICTRON_PORT_NAME) + "\",\"rx_gpio\":" + String(VICTRON_RX) + ",\"tx_enabled\":" + String(VICTRON_TX >= 0 ? "true" : "false") + ",\"baud\":" + String(VICTRON_BAUD) + ",\"bytes\":" + String(vedirectByteCount) + ",\"parsed_lines\":" + String(vedirectParsedLineCount) + ",\"bad_lines\":" + String(vedirectBadLineCount) + ",\"uart_restarts\":" + String(vedirectReinitCount) + ",\"lines\":[";
  for (int i=0;i<vedirectRawStored;i++) { int idx=(vedirectRawPos-vedirectRawStored+i+VEDIRECT_RAW_COUNT)%VEDIRECT_RAW_COUNT; String l=vedirectRawRing[idx]; l.replace("\\","\\\\"); l.replace("\"","\\\""); if(i) j+=","; j += "\""+l+"\""; }
  j += "]}";
  sendJsonPretty(j);
}

void handleVedirectRestart() {
  if (!requireAuth()) return;
  restartVictronSerial("manual_web");
  sendActionPage("VE.Direct UART", "Seriale VE.Direct riavviata su IO27. Attendi 10-20 secondi e ricontrolla la pagina RAW.", 2, "/vedirect-raw");
}

String apiStatusJson() {
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"wifi_connected\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  j += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"victron_online\":" + String(victronOnline()?"true":"false") + ",";
  j += "\"sd_mounted\":" + String(sdMounted?"true":"false") + ",";
  j += "\"health\":" + String(healthScoreNow()) + ",";
  j += "\"alerts\":" + String(alertCountNow()) + ",";
  j += "\"uptime_ms\":" + String(millis());
  j += "}";
  return j;
}

void handleNetworkJson() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  String j = "{";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"hostname\":\"" + String(HOSTNAME) + "\",";
  j += "\"ssid\":\"" + esc(WiFi.SSID()) + "\",";
  j += "\"status\":\"" + String(WiFi.status() == WL_CONNECTED ? "connected" : "offline") + "\",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
  j += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
  j += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
  j += "\"mac\":\"" + WiFi.macAddress() + "\",";
  j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"recommendation\":\"Prenota questo MAC nel router per mantenere IP fisso. Consigliato usare DHCP reservation, non IP statico nello sketch.\"";
  j += "}";
  sendJsonPretty(j);
}


void handleQuickCheckPage() {
  if (!requireAuth()) return;
  float ep = espBatteryPercent();
  String html = htmlHeader("Diagnosi rapida");
  html += "<div class='top'><h1>Diagnosi rapida</h1><div class='sub'>Controllo immediato dello stato essenziale del monitor.</div><div class='nav'><a href='/'>Dashboard</a><a href='/network'>Rete/IP</a><a href='/vedirect-raw'>VE.Direct RAW</a><a href='/battery'>Batteria ESP</a></div></div>";
  html += "<div class='grid'>";
  html += card("VE.Direct", victronOnline()?"Online":"No Data", String(VICTRON_PORT_NAME) + "<br>Ultimo dato: " + String(victronSeen ? ((millis()-lastVictronMs)/1000UL) : 0) + " s fa");
  html += card("Batteria ESP", String(isnan(ep)?0:ep,0) + "%", String(isnan(espBatteryVoltage())?0:espBatteryVoltage(),2) + " V su GPIO34");
  html += card("WiFi", String(WiFi.RSSI()) + " dBm", "IP: " + WiFi.localIP().toString() + "<br>MAC: " + WiFi.macAddress());
  html += card("Impianto", String(configuredPanelWatts(),0) + " W pannello", plantInfoSummary());
  html += card("SD", sdMounted?"Montata":"Non montata", sdMounted?("Libera: " + formatBytes64(SD.totalBytes()-SD.usedBytes())):esc(sdLastStatus));
  html += card("Firmware", String(FW_VERSION), "VE.Direct RX IO27, TX disabilitato<br>OTA repo pubblica");
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleNetworkPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Rete / IP");
  html += "<div class='top'><h1>Rete / IP</h1><div class='sub'>Stato WiFi, MAC address e istruzioni per bloccare l'IP dal router.</div>";
  html += "<div class='nav'><a href='/'>Dashboard</a><a href='/settings'>Settings</a><a href='/network.json'>JSON rete</a><a href='/info'>Info</a></div></div>";
  html += "<div class='grid'>";
  html += card("IP attuale", WiFi.localIP().toString(), "Se il router si riavvia puo' cambiare se non e' prenotato.");
  html += card("MAC address", WiFi.macAddress(), "Usalo nel router per prenotazione DHCP / static lease.");
  html += card("Hostname", String(HOSTNAME), "Prova anche: http://" + String(HOSTNAME) + ".local se mDNS e' disponibile.");
  html += card("Segnale WiFi", String(WiFi.RSSI()) + " dBm", "Qualita': " + wifiBadgeClass(WiFi.RSSI()) + "<br>SSID: " + esc(WiFi.SSID()));
  html += card("Gateway / DNS", WiFi.gatewayIP().toString(), "DNS: " + WiFi.dnsIP().toString() + "<br>Subnet: " + WiFi.subnetMask().toString());
  html += "</div>";
  html += "<div class='card'><div class='t'>Come evitare che cambi IP</div>";
  html += "<p>La CYD usa DHCP. Dopo un riavvio router puo' ricevere un nuovo IP. La soluzione consigliata e' fissare l'IP dal router con una <b>prenotazione DHCP</b>.</p>";
  html += "<ol><li>Apri il pannello del router.</li><li>Vai in LAN / DHCP / Dispositivi connessi / Prenotazione indirizzo.</li><li>Cerca il MAC: <b>" + WiFi.macAddress() + "</b>.</li><li>Assegna sempre lo stesso IP, per esempio <b>" + WiFi.localIP().toString() + "</b>.</li><li>Salva e riavvia la CYD o il router.</li></ol>";
  html += "<p class='e'>Meglio farlo dal router invece che mettere IP statico nel firmware, cosi' WiFiManager resta flessibile se cambi rete.</p>";
  html += "</div>";
  html += "<div class='card'><div class='t'>Link rapidi</div>";
  html += "<p><a class='button' href='/api/v1'>API v1</a> <a class='button' href='/json'>JSON live</a> <a class='button' href='/vedirect-raw'>VE.Direct RAW</a> <a class='button' href='/battery.json'>Batteria ESP JSON</a></p>";
  html += "</div>";
  html += String("</body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

void handleApiV1Index(){ if(!requireAuth()) return; String h=htmlHeader("API v1"); h += "<div class='top'><h1>API REST v1</h1><div class='sub'>Endpoint JSON ordinati per integrazioni esterne.</div><div class='nav'><a href='/'>Dashboard</a><a href='/network'>Rete</a><a href='/json'>JSON live</a><a href='/vedirect-raw'>VE.Direct RAW</a></div></div><div class='card'><pre>/api/v1/status\n/api/v1/power\n/api/v1/victron\n/api/v1/battery\n/api/v1/sd\n/api/v1/alerts\n/api/v1/health\n/api/v1/config\n/network.json</pre></div>" + String("</body></html>"); server.send(200,"text/html",h); }
void handleApiV1Status(){ if(!requireAuth()) return; sendJsonPretty(apiStatusJson()); }
void handleApiV1Power(){ if(!requireAuth()) return; sendJsonPretty("{\"reset_reason\":\""+esc(resetReasonText())+"\",\"boot_count\":"+String(bootCounter)+",\"esp_battery_v\":"+String(isnan(espBatteryVoltage())?0:espBatteryVoltage(),3)+",\"esp_battery_pct\":"+String(isnan(espBatteryPercent())?0:espBatteryPercent(),0)+"}"); }
void handleApiV1Victron(){ if(!requireAuth()) return; handleJson(); }
void handleApiV1Battery(){ if(!requireAuth()) return; handleBatteryJson(); }
void handleApiV1Sd(){ if(!requireAuth()) return; handleSdJson(); }
void handleApiV1Alerts(){ if(!requireAuth()) return; handleAlertsJson(); }
void handleApiV1Health(){ if(!requireAuth()) return; sendJsonPretty(systemHealthJson()); }
void handleApiV1Config(){ if(!requireAuth()) return; sendJsonPretty(settingsBackupJson()); }

void handleSdIntegrityPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Integrita' SD");
  html += "<div class='top'><h1>Integrita' microSD</h1><div class='sub'>Controllo cartelle, CSV e indice log.</div><div class='nav'><a href='/'>Dashboard</a><a href='/sd'>MicroSD</a><a href='/sd-repair-index'>Ricrea indice</a></div></div>";
  if (!sdMounted && !sdMount(false)) html += "<div class='card warn'><div class='v'>SD non montata</div><p>" + esc(sdLastStatus) + "</p></div>";
  else {
    sdCreateBaseDirs();
    html += "<div class='grid'>";
    html += card("Cartelle base", "OK", "/logs /backup_recovery /config /exports /diagnostic");
    html += card("File log", String(sdCountEntries("/logs", true)), "Elementi in /logs");
    html += card("Spazio log", formatBytes64(sdDirBytes("/logs")), "Usato dai log");
    html += card("Spazio backup", formatBytes64(sdDirBytes("/backup_recovery")), "Recovery completi");
    html += "</div><div class='card'><p><a class='button' href='/sd-repair-index'>Ricrea indice CSV</a> <a class='button' href='/sd-files?p=/logs'>Apri log</a></p></div>";
  }
  html += String("</body></html>"); server.send(200,"text/html",html);
}

void handleSdRepairCsvIndex() {
  if (!requireAuth()) return;
  if (!sdMounted && !sdMount(false)) { sendActionPage("Indice SD", "SD non montata: " + sdLastStatus, 3, "/sd-integrity"); return; }
  sdEnsureDir("/exports");
  File idx = SD.open("/exports/log_index.csv", FILE_WRITE);
  if (!idx) { sendActionPage("Indice SD", "Impossibile creare /exports/log_index.csv", 3, "/sd-integrity"); return; }
  idx.println("path,size");
  File root = SD.open("/logs");
  int n=0;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while(f){ String p=String(f.name()); if(!f.isDirectory()){ idx.print(p); idx.print(','); idx.println((unsigned long)f.size()); n++; } f=root.openNextFile(); }
  }
  idx.close();
  addEventLog("SD", "Indice CSV ricreato: " + String(n) + " file");
  sendActionPage("Indice SD", "Indice creato con " + String(n) + " file.", 2, "/sd-integrity");
}

void handleOfflineApPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Offline / AP Setup");
  html += "<div class='top'><h1>Modalita' offline / AP</h1><div class='sub'>Cosa succede se il WiFi non e' disponibile.</div><div class='nav'><a href='/'>Dashboard</a><a href='/settings'>Settings</a></div></div>";
  html += "<div class='card'><div class='t'>Fallback AP</div><div class='v'>" + String(WIFI_SETUP_AP) + "</div><p>Password: <b>" + String(WIFI_SETUP_PASS) + "</b></p><p>Se la rete principale non viene trovata, WiFiManager apre il portale di configurazione per 180 secondi.</p></div>";
  html += "<div class='card'><div class='t'>Stato attuale</div><p>WiFi: " + String(WiFi.status()==WL_CONNECTED?"Connesso":"Offline") + "<br>IP: " + WiFi.localIP().toString() + "</p></div>" + String("</body></html>");
  server.send(200,"text/html",html);
}

void handleHardwareTestPage() {
  if (!requireAuth()) return;
  String html = htmlHeader("Hardware test");
  html += "<div class='top'><h1>Autotest hardware</h1><div class='sub'>Verifica rapida di display, touch, SD, VE.Direct, WiFi e backlight.</div><div class='nav'><a href='/'>Dashboard</a><a href='/'>Dashboard</a></div></div>";
  html += "<table><tr><th>Componente</th><th>Stato</th><th>Dettaglio</th></tr>";
  html += alertRow("TFT", "OK", "Dashboard disegnata, driver ILI9341");
  html += alertRow("Touch", touchReady?"OK":"WARN", touchReady?"XPT2046 pronto":"Non pronto / disabilitato");
  html += alertRow("microSD", sdMounted?"OK":"WARN", sdMounted?formatBytes64(SD.usedBytes())+" usati":"Non montata");
  html += alertRow("VE.Direct", victronOnline()?"OK":"WARN", "Ultimo dato " + String((millis()-lastVictronMs)/1000UL) + "s fa");
  html += alertRow("WiFi", WiFi.status()==WL_CONNECTED?"OK":"BAD", WiFi.localIP().toString() + " RSSI " + String(WiFi.RSSI()));
  html += alertRow("Backlight", backlightOn?"OK":"WARN", backlightOn?"ON":"OFF");
  html += "</table>" + String("</body></html>"); server.send(200,"text/html",html);
}

bool sdWritesAllowed() {
  if (!sdWriteProtectEnabled) return true;
  return false;
}

bool sdAppendLineProtected(const String& path, const String& line, bool headerIfNew) {
  if (!sdWritesAllowed()) { lastSdLogStatus = "Scrittura SD protetta: log non scritto"; return false; }
  return sdAppendLine(path, line, headerIfNew);
}

void handleSdWriteProtectionPage() {
  if (!requireAuth()) return;
  prefs.begin("victron", true); bool en = prefs.getBool("sd_wr_prot", false); prefs.end(); sdWriteProtectEnabled = en;
  String html = htmlHeader("Protezione scrittura SD");
  html += "<div class='top'><h1>Protezione scrittura microSD</h1><div class='sub'>Riduce scritture automatiche quando vuoi proteggere la scheda o sei in test.</div><div class='nav'><a href='/'>Dashboard</a><a href='/sd'>MicroSD</a><a href='/storage'>Storage</a></div></div>";
  html += "<div class='card'><div class='t'>Stato</div><div class='v'>" + String(en?"Protetta":"Scrittura attiva") + "</div><p>Se attiva, i log automatici su SD vengono sospesi ma puoi comunque leggere e scaricare file.</p>";
  html += "<form method='POST' action='/sd-write-protection-save'><select name='enabled'><option value='0'" + String(!en?" selected":"") + ">Scrittura attiva</option><option value='1'" + String(en?" selected":"") + ">Proteggi / sospendi scritture</option></select><p><button>Salva</button></p></form></div>" + String("</body></html>");
  server.send(200,"text/html",html);
}

void handleSdWriteProtectionSave() {
  if (!requireAuth()) return;
  bool en = server.hasArg("enabled") && server.arg("enabled") == "1";
  prefs.begin("victron", false); prefs.putBool("sd_wr_prot", en); prefs.end(); sdWriteProtectEnabled = en;
  addEventLog("SD", String("Protezione scrittura: ") + (en?"ON":"OFF"));
  sendActionPage("Protezione SD", en?"Scritture automatiche sospese.":"Scritture automatiche abilitate.", 2, "/sd-write-protection");
}

// ======================= END V10.4.64 TECH PRO SAFE ADDITIONS =======================


void setupBackupRetention5x5Defaults() {
  prefs.begin("victron", false);
  bool migrated = prefs.getBool("ret_5x5_done", false);
  if (!migrated) {
    prefs.putInt("backup_keep", 5);
    prefs.putInt("daily_backup_keep", 5);
    prefs.putBool("ret_5x5_done", true);
  }
  prefs.end();
}

void setup() {
  lastUserActivityMs = millis();
  // V10.4.73: VE.Direct e' su IO27, quindi IO35 resta libero e non disturba GPIO34 batteria ESP.
  Serial.begin(115200);
  loadPublicConfig();
  applyPublicConfigToLegacyPrefs();
  loadDisplayRotationSetting();

  // V10.4.21: se il firmware e' appena stato scritto via OTA, il primo boot
  // viene usato solo per fare un secondo reboot pulito automatico.
  // Questo sostituisce il reset manuale che finora serviva dopo il flash.
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
#ifdef TOUCH_CS
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#endif
  // V10.4.23: una sola inizializzazione TFT completa al boot.
  // Poi mostra una pagina di caricamento e NON reinizializza più il display a fine setup.
  robustTftInit();
  drawBootProgress("Display inizializzato", 12);
  setBacklight(true);

  delay(80);
  drawBootProgress("Lettura contatore boot", 18);
  loadBootCounter();
  safeBootStart();
  drawBootProgress("Avvio filesystem", 25);
  initLittleFs();
  ensurePublicOtaRepoDefaults();
  setupBackupRetention5x5Defaults();
  prefs.begin("victron", true); sdWriteProtectEnabled = prefs.getBool("sd_wr_prot", false); prefs.end();
  addEventLog("BOOT", String(FW_VERSION) + " avvio");
  recordRebootHistory();
  if (!loadHistoryFromFs()) {
    for (int i=0;i<24;i++) resetSlot(hourly[i]);
    for (int i=0;i<31;i++) resetSlot(daily[i]);
    for (int i=0;i<12;i++) resetSlot(monthly[i]);
  }
  if (!loadChargeHistoryFromFs()) {
    for (int i=0;i<24;i++) resetChargeSlot(chHourly[i]);
    for (int i=0;i<31;i++) resetChargeSlot(chDaily[i]);
    for (int i=0;i<12;i++) resetChargeSlot(chMonthly[i]);
  }
  saveCurrentFirmwareInfo();
  drawBootProgress("Filesystem OK", 35);

  // V10.4.23: TFT già inizializzato all'inizio del setup.

  // V10.4.24: NON inizializzare il touch a metà boot.
  // In alcune CYD la init del touch durante la schermata boot sporca il bus e fa
  // sembrare il progresso tornato a zero. Lo inizializziamo alla fine, a TFT stabile.
  drawBootProgress("Seriale Victron", 45);
  VictronSerial.begin(VICTRON_BAUD, SERIAL_8N1, VICTRON_RX, VICTRON_TX);
  lastVictronReinitMs = millis();
  lastVictronReinitReason = "boot";

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(runtimeHostname().c_str());

  // Se vuoi IP fisso, lascia questa riga.
  // Se cambi rete e non funziona, commentala e ricompila.
  // WiFi.config(local_IP, gateway, subnet, dns1);  // disattivato per rendere stabile WiFiManager

  drawBootProgress("Connessione WiFi", 55);
  startWiFiManager(forceWifiPortalAtBoot);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    syncNtpNow();
    saveFirmwareInstallIfChanged();
    autoBackupAfterOtaIfNeeded();

    if (MDNS.begin(runtimeHostname().c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS attivo: http://victron-monitor.local");
    } else {
      Serial.println("mDNS non avviato");
    }

    ArduinoOTA.setHostname(runtimeHostname().c_str());
    ArduinoOTA.begin();
    addEventLog("WIFI", "Connesso IP " + WiFi.localIP().toString() + " RSSI " + String(WiFi.RSSI()));
    drawBootProgress("WiFi OK: " + WiFi.localIP().toString(), 70);
  } else {
    drawBootProgress("WiFi offline", 70);
  }

  // V10.4.23: niente init finale qui. Si continua con lo stesso TFT già stabile.

  server.on("/", HTTP_GET, handleRoot);
  server.on("/tft-test", HTTP_GET, handleTftTestPage);
  server.on("/json", HTTP_GET, handleJson);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/charge-history", HTTP_GET, handleChargeHistory);
  server.on("/history-compact", HTTP_GET, handleHistoryCompact);
  server.on("/history-page", HTTP_GET, handleHistoryPage);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/network", HTTP_GET, handleNetworkPage);
  server.on("/quick-check", HTTP_GET, handleQuickCheckPage);
  server.on("/network.json", HTTP_GET, handleNetworkJson);
  server.on("/updates", HTTP_GET, handleUpdatesHub);
  server.on("/data-center", HTTP_GET, handleDataCenter);
  server.on("/victron-data", HTTP_GET, handleVictronDataPage);
  server.on("/energy-today", HTTP_GET, handleEnergyTodayPage);
  server.on("/alerts", HTTP_GET, handleAlertsPage);
  server.on("/alerts.json", HTTP_GET, handleAlertsJson);
  server.on("/battery-cal", HTTP_GET, handleBatteryCalPage);
  server.on("/history-gx", HTTP_GET, handleHistoryGxPage);
  server.on("/setup-check", HTTP_GET, handleSetupCheckPage);
  server.on("/settings-backup", HTTP_GET, handleSettingsBackupPage);
  server.on("/settings-export", HTTP_GET, handleSettingsExport);
  server.on("/settings-restore", HTTP_POST, handleSettingsRestore);
  server.on("/logs", HTTP_GET, handleLogsPage);
  server.on("/logs.json", HTTP_GET, handleLogsJson);
  server.on("/logs-clear", HTTP_GET, handleLogsClear);
  server.on("/touch-cal", HTTP_GET, handleTouchCalPage);
  server.on("/touch-raw", HTTP_GET, handleTouchRaw);
  server.on("/touch-save", HTTP_POST, handleTouchSave);
  server.on("/touch-save-points", HTTP_POST, handleTouchSavePoints);
  server.on("/history.csv", HTTP_GET, handleHistoryCsv);
  server.on("/ota-center", HTTP_GET, handleOtaCenterPage);
  server.on("/ota-schedule-save", HTTP_POST, handleOtaScheduleSave);
  server.on("/bat-scan", HTTP_GET, handleBatScanPage);
  server.on("/battery", HTTP_GET, handleBatteryPage);
  server.on("/battery-installed", HTTP_GET, handleBatteryInstalledToggle);
  server.on("/battery.json", HTTP_GET, handleBatteryJson);
  server.on("/sd", HTTP_GET, handleSdPage);
  server.on("/sd.json", HTTP_GET, handleSdJson);
  server.on("/sd-mount", HTTP_GET, handleSdMount);
  server.on("/sd-unmount", HTTP_GET, handleSdUnmount);
  server.on("/files", HTTP_GET, handleFilesHubPage);
  server.on("/sd-files", HTTP_GET, handleSdFilesPage);
  server.on("/sd-view", HTTP_GET, handleSdViewFile);
  server.on("/sd-download", HTTP_GET, handleSdDownloadFile);
  server.on("/sd-delete", HTTP_GET, handleSdDeleteFile);
  server.on("/sd-format", HTTP_GET, handleSdFormat);
  server.on("/storage", HTTP_GET, handleStoragePage);
  server.on("/storage-save", HTTP_POST, handleStorageSave);
  server.on("/sd-snapshot", HTTP_GET, handleSdSnapshot);
  server.on("/sd-log.csv", HTTP_GET, handleSdLogCsv);
  server.on("/sd-logs", HTTP_GET, handleSdLogsPage);
  server.on("/sd-log-download", HTTP_GET, handleSdLogDownload);
  server.on("/sd-log-delete", HTTP_GET, handleSdLogDelete);
  server.on("/backup-sd", HTTP_GET, handleBackupToSd);
  server.on("/clone-backup", HTTP_GET, handleFullBackupBackupPage);
  server.on("/backup-recovery", HTTP_GET, handleBackupRecoveryPage);
  server.on("/clone-backup-start", HTTP_GET, handleFullBackupBackupStart);
  server.on("/backup-recovery-start", HTTP_GET, handleFullBackupBackupStart);
  server.on("/clone-progress.json", HTTP_GET, handleBackupProgressJson);
  server.on("/backup-progress.json", HTTP_GET, handleBackupProgressJson);
  server.on("/plant-info", HTTP_GET, handlePlantInfoPage);
  server.on("/plant-info-save", HTTP_POST, handlePlantInfoSave);
  server.on("/thresholds", HTTP_GET, handleThresholdsPage);
  server.on("/thresholds-save", HTTP_POST, handleThresholdsSave);
  server.on("/health", HTTP_GET, handleHealthPage);
  server.on("/diag-snapshot", HTTP_GET, handleDiagnosticSnapshot);
  server.on("/backup-list", HTTP_GET, handleBackupRecoveryListPage);
  server.on("/backup-restore", HTTP_GET, handleBackupRecoveryRestorePage);
  server.on("/backup-restore-start", HTTP_GET, handleBackupRecoveryRestoreStart);
  server.on("/backup-recovery-pro", HTTP_GET, handleBackupRecoveryProPage);
  server.on("/backup-config-restore", HTTP_GET, handleBackupConfigRestoreFromSd);
  server.on("/daily-backups", HTTP_GET, handleDailyBackupsPage);
  server.on("/daily-backup-now", HTTP_GET, handleDailyBackupNow);
  server.on("/alerts-history", HTTP_GET, handleAlertsHistoryPage);
  server.on("/sd-retention", HTTP_GET, handleSdRetentionPage);
  server.on("/sd-retention-save", HTTP_POST, handleSdRetentionSave);
  server.on("/diagnostic-run", HTTP_GET, handleDiagnosticRunPage);
  server.on("/theme", HTTP_GET, handleThemePage);
  server.on("/theme-save", HTTP_POST, handleThemeSave);
  server.on("/power", HTTP_GET, handlePowerPage);
  server.on("/shutdown", HTTP_GET, handleShutdownPage);
  server.on("/shutdown-timed", HTTP_GET, handleShutdownTimed);
  server.on("/shutdown-reset", HTTP_GET, handleShutdownReset);
  server.on("/recovery-restore-pro", HTTP_GET, handleRecoveryRestoreProPage);
  server.on("/recovery-restore-firmware", HTTP_GET, handleRecoveryRestoreFirmwareStart);
  server.on("/watchdog", HTTP_GET, handleAppWatchdogPage);
  server.on("/reboot-history", HTTP_GET, handleRebootHistoryPage);
  server.on("/reboot-history.json", HTTP_GET, handleRebootHistoryJson);
  server.on("/time-ntp", HTTP_GET, handleTimeNtpPage);
  server.on("/ntp-sync", HTTP_GET, handleNtpSyncNow);
  server.on("/vedirect-raw", HTTP_GET, handleVedirectRawPage);
  server.on("/vedirect-raw.json", HTTP_GET, handleVedirectRawJson);
  server.on("/vedirect-restart", HTTP_GET, handleVedirectRestart);
  server.on("/api/v1", HTTP_GET, handleApiV1Index);
  server.on("/api/v1/status", HTTP_GET, handleApiV1Status);
  server.on("/api/v1/power", HTTP_GET, handleApiV1Power);
  server.on("/api/v1/victron", HTTP_GET, handleApiV1Victron);
  server.on("/api/v1/battery", HTTP_GET, handleApiV1Battery);
  server.on("/api/v1/sd", HTTP_GET, handleApiV1Sd);
  server.on("/api/v1/alerts", HTTP_GET, handleApiV1Alerts);
  server.on("/api/v1/health", HTTP_GET, handleApiV1Health);
  server.on("/api/v1/config", HTTP_GET, handleApiV1Config);
  server.on("/sd-integrity", HTTP_GET, handleSdIntegrityPage);
  server.on("/sd-repair-index", HTTP_GET, handleSdRepairCsvIndex);
  server.on("/offline-ap", HTTP_GET, handleOfflineApPage);
  server.on("/hardware-test", HTTP_GET, handleHardwareTestPage);
  server.on("/sd-write-protection", HTTP_GET, handleSdWriteProtectionPage);
  server.on("/sd-write-protection-save", HTTP_POST, handleSdWriteProtectionSave);
  server.on("/sd-maintenance", HTTP_GET, handleSdMaintenancePage);
  server.on("/sd-clean-old-logs", HTTP_GET, handleSdMaintenanceCleanLogs);
  server.on("/stats-sd", HTTP_GET, handleStatsSdPage);
  server.on("/api/live", HTTP_GET, handleApiLive);
  server.on("/live-status", HTTP_GET, handleLiveStatusPage);
  server.on("/api/history", HTTP_GET, handleApiHistory);
  server.on("/api/system", HTTP_GET, handleApiSystem);
  server.on("/system-pro", HTTP_GET, handleSystemPro);
  server.on("/github-update", HTTP_GET, handleGithubUpdatePage);
  server.on("/github-update-start", HTTP_POST, handleGithubUpdateStart);
  server.on("/github-progress", HTTP_GET, handleGithubProgressJson);
  server.on("/ota-notify.json", HTTP_GET, handleOtaNotifyJson);
  server.on("/ota-notify-clear", HTTP_GET, handleOtaNotifyClear);
  server.on("/github-progress-page", HTTP_GET, handleGithubProgressPage);
  server.on("/recovery", HTTP_GET, handleRecoveryPage);
  server.on("/reboot-recovery", HTTP_GET, handleRebootRecovery);
  server.on("/github-save", HTTP_POST, handleGithubSave);
  server.on("/backlight", HTTP_GET, handleBacklight);
  server.on("/touch-clear", HTTP_GET, handleTouchClear);
  server.on("/touch-reset", HTTP_GET, handleTouchReset);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/diag", HTTP_GET, handleDiag);
  server.on("/diag.json", HTTP_GET, handleDiagJson);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/firmware", HTTP_GET, handleUpdatePage);
  server.on("/ota-status", HTTP_GET, handleOtaStatus);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  registerPublicWizardRoutes(server);
  server.onNotFound(handleNotFound);
  server.begin();
  addEventLog("WEB", "Server web avviato");
  drawBootProgress("Server web avviato", 85);
  rollbackInitAndValidate();
  drawBootProgress("Touch posticipato", 90);
  // V10.4.25: non inizializzare il touch durante la schermata di boot.
  // Nelle CYD puo' sporcare il bus/provocare flicker o reset al primo avvio post OTA.
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#ifdef TFT_CS
  digitalWrite(TFT_CS, HIGH);
#endif
  // V10.4.28: touch riattivato in modo protetto.
  // Non durante il boot: parte solo dopo dashboard stabile.
  // Se l'init touch causa un crash, al boot successivo viene disabilitato automaticamente.
  touchReady = false;
  touchInitTried = false;
  touchInitAfterMs = millis() + 5000UL;

  drawBootProgress("Dashboard", 96);
  delay(250);

  // V10.4.24: nessuna seconda init e nessun fillScreen ripetuto durante boot.
  drawDashboard();
  setBacklight(true);
}

void initTouchDeferred() {
  // V10.4.28: init touch protetta e ritardata.
  // Motivo: la V10.4.25 andava in reboot loop quando inizializzava il touch nel loop.
  // Fix: niente IRQ, polling mode, CS stabilizzati, e guardia Preferences.
  if (touchReady || touchInitTried) return;
  if ((long)(millis() - touchInitAfterMs) < 0) return;

  touchInitTried = true;

  prefs.begin("victron", false);
  bool pendingCrash = prefs.getBool("touch_pending", false);
  bool disabled = prefs.getBool("touch_disabled", false);

  if (pendingCrash) {
    // Se siamo arrivati qui dopo un reset mentre touch_pending era ancora true,
    // vuol dire che il tentativo precedente ha probabilmente causato crash/reboot.
    prefs.putBool("touch_pending", false);
    prefs.putBool("touch_disabled", true);
    disabled = true;
  }

  if (disabled) {
    prefs.end();
    Serial.println("Touch XPT2046 disabilitato da crash guard");
    touchReady = false;
    return;
  }

  prefs.putBool("touch_pending", true);
  prefs.end();

#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
#ifdef TOUCH_CS
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#endif
  delay(80);

  // Bus separato VSPI: SCLK 25, MISO 39, MOSI 32, CS 33.
  // Niente IRQ: ts.touched() lavora in polling.
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  delay(80);

  bool ok = ts.begin(touchSPI);
  if (ok) {
    ts.setRotation(1);
    touchReady = true;
    touchWasDown = false;
    Serial.println("Touch XPT2046 OK safe polling");
  } else {
    touchReady = false;
    Serial.println("Touch XPT2046 NON rilevato safe polling");
  }

  prefs.begin("victron", false);
  prefs.putBool("touch_pending", false);
  // Se init non crasha, non lasciarlo disabilitato.
  prefs.putBool("touch_disabled", false);
  prefs.end();
}
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (otaRestartPending && (long)(millis() - otaRestartAtMs) >= 0) {
    cleanRestartNow("ota_pending_restart");
  }
  ensureWiFi();
  updateOtaTimeIfNtpBecomesValid();
  checkGithubUpdate(false);
  weeklyGithubUpdateLoop();
  safeBootLoop();
  readVictron();
  victronAutoRecoveryLoop();
  updateHistory();
  sdLoggerLoop();
  dailyConfigBackupLoop();
  alertHistoryLoop();
  appWatchdogLoop();
  handleDisplayAutoOffLoop();
  initTouchDeferred();
  handleTouch();
  checkFailsafe();
  saveDiagnostics();

  // V10.4.10: recovery reinit disabilitato. Il TFT non va reinizializzato nel loop.

  // V10.4.17: grafica ripristinata ma refresh lento e sicuro.
  // Non reinizializza mai il TFT: ridisegna solo ogni 10 secondi.
  if (!tftTouchCalMode && millis() - lastDrawMs > 8000UL) {
    drawDashboard();
    lastDrawMs = millis();
  }
}
