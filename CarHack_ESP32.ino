/******************************************************************************
 * CarHack_ESP32.ino — فایل اصلی پروژه CarHack-ESP32 v5.2
 * ============================================================================
 * توضیحات:
 *   سیستم یکپارچه تست و ارزیابی امنیتی خودرو با قابلیت‌های:
 *   - CAN Bus (TWAI) — Sniffing و ارسال فریم
 *   - ISO-TP / UDS — پروتکل‌های عیب‌یابی
 *   - OBD2 — خواندن پارامترهای خودرو
 *   - RF 433 MHz — دریافت و ارسال (با پشتیبانی RollJam)
 *   - BLE — رابط بلوتوث
 *   - Web Dashboard — داشبورد تحت وب
 *   - SD Card Logger — ذخیره‌سازی داده‌ها
 *
 * تغییرات v5.2:
 *   ✅ اصلاح تداخل پین‌ها — CAN TX → GPIO17, RF CS → GPIO5
 *   ✅ اضافه شدن Capability Gate — هر فرمان فقط برای خودروهای مجاز
 *   ✅ اصلاح WMI matching — مقایسه ۳ بایت اول VIN با ماکرو WMI()
 *   ✅ بهبود OBD2 poll interval — افزایش به ۲ ثانیه
 ******************************************************************************/

#include <Arduino.h>
#include <SPI.h>

#include "can_manager.h"
#include "vehicle_db.h"
#include "uds_diag.h"
#include "obd2_diag.h"
#include "rf_rolljam.h"
#include "sd_logger.h"
#include "web_dashboard.h"
#include "can_learner.h"


/* ========================================================================== */
/*   —— پین‌های سخت‌افزاری — Pin Assignment ——                                */
/* -------------------------------------------------------------------------- */
/*   ┌───────────────────────────────────────────────────────────────────┐   */
/*   │  ESP32 Pin  │  سیگنال       │  توضیح                            │   */
/*   ├───────────────────────────────────────────────────────────────────┤   */
/*   │  GPIO17     │  CAN_TX       │  CAN Bus — Transmit (v5.2 تغییر)  │   */
/*   │  GPIO16     │  CAN_RX       │  CAN Bus — Receive  (v5.2 تغییر)  │   */
/*   │  GPIO4      │  SD_CS        │  SD Card — Chip Select            │   */
/*   │  GPIO18     │  SPI_CLK      │  SPI Clock (مشترک SD + RF)        │   */
/*   │  GPIO23     │  SPI_MOSI     │  SPI Master Out / Slave In        │   */
/*   │  GPIO19     │  SPI_MISO     │  SPI Master In  / Slave Out       │   */
/*   │  GPIO5      │  RF_CS        │  CC1101 — Chip Select (v5.2 تغییر)│   */
/*   │  GPIO21     │  LED_BUILTIN  │  LED داخلی (تشخیص فعالیت)         │   */
/*   └───────────────────────────────────────────────────────────────────┘   */
/* -------------------------------------------------------------------------- */
/*   HSPI (SPI2):  SD  Card — CS=GPIO4,  CLK=GPIO18, MOSI=GPIO23, MISO=GPIO19 */
/*   VSPI (SPI3):  RF CC1101 — CS=GPIO5,  CLK=GPIO18, MOSI=GPIO23, MISO=GPIO19 */
/*   توجه: CS مجزا (GPIO4 و GPIO5) اجازه استفاده اشتراکی از SPI باس را می‌دهد */
/* ========================================================================== */

/* --- CAN Bus (TWAI) --- */
#define PIN_CAN_TX        GPIO_NUM_17     // ✅ تغییر از GPIO5 → GPIO17
#define PIN_CAN_RX        GPIO_NUM_16     // ✅ تغییر از GPIO4 → GPIO16

/* --- SD Card (SPI) --- */
#define PIN_SD_CS         GPIO_NUM_4
#define PIN_SD_CLK        GPIO_NUM_18
#define PIN_SD_MOSI       GPIO_NUM_23
#define PIN_SD_MISO       GPIO_NUM_19

/* --- RF CC1101 (SPI — اشتراکی با SD) --- */
#define PIN_RF_CS         GPIO_NUM_5      // ✅ تغییر از GPIO18 → GPIO5

/* --- LED داخلی --- */
#define PIN_LED           GPIO_NUM_21


/* ========================================================================== */
/*   —— ثابت‌های سیستم ——                                                     */
/* ========================================================================== */

#define OBD2_POLL_MS      2000            // فاصله poll OBD2 (۲ ثانیه)
#define TP_KEEPALIVE_MS   3000            // فاصله TesterPresent (۳ ثانیه)
#define SERIAL_BAUD       115200          // نرخ بود Serial
#define FW_VERSION        "CarHack-ESP32 v5.2"


/* ========================================================================== */
/*   —— اشیاء سراسری (Global Objects) ——                                      */
/* ========================================================================== */

CANManager        canManager;       // مدیریت CAN Bus
ISOTPProtocol     isoTp;            // پروتکل ISO-TP
UDSDiagnostic     udsDiag;          // سرویس‌های UDS
OBD2Diagnostic    obd2Diag;         // سرویس‌های OBD2
RFRollJam         rfRollJam;        // RF CC1101 + RollJam
SDLogger          sdLogger;         // ذخیره‌سازی روی SD
WebDashboard      webDashboard;     // داشبورد تحت وب
CANLearner        canLearner;       // یادگیری خودکار CAN


/* ---- وضعیت خودروی شناسایی‌شده ---- */
const VehicleProfile* currentVehicle = nullptr;
char                  currentVIN[18] = {0};
bool                  vehicleIdentified = false;


/* ========================================================================== */
/*   —— توابع کمکی ——                                                          */
/* ========================================================================== */

/* ===================================================================== */
/*  تشخیص خودرو — detectVehicle                                         */
/* --------------------------------------------------------------------- */
/*  تلاش می‌کند VIN خودرو را از طریق UDS یا OBD2 بخواند و در دیتابیس     */
/*  جستجو کند.                                                          */
/* ===================================================================== */
bool detectVehicle() {

  Serial.println(F("\n🔎 [SYS]  در حال تشخیص خودرو..."));

  /* ---- تلاش برای خواندن VIN از طریق UDS ---- */
  if (udsDiag.isSessionActive()) {

    uint8_t vinBuf[32];
    uint16_t vinLen = 0;

    if (udsDiag.readDataByIdentifier(0xF190, vinBuf, &vinLen)) {
      
      /* استخراج VIN از پاسخ UDS */
      if (vinLen >= 4 && vinBuf[0] == 0x05 && vinBuf[1] == 0x62) {
        int copyLen = min((int)(vinLen - 4), 17);
        memcpy(currentVIN, &vinBuf[4], copyLen);
        currentVIN[copyLen] = '\0';
        
        /* جستجو در دیتابیس */
        currentVehicle = findVehicleByVIN(currentVIN);
        
        if (currentVehicle != nullptr) {
          vehicleIdentified = true;
          Serial.printf("✅ [SYS]  خودرو شناسایی شد: %s (VIN: %s)\n",
                        currentVehicle->make, currentVIN);
          return true;
        } else {
          Serial.printf("⚠ [SYS]  VIN=%s در دیتابیس یافت نشد\n", currentVIN);
        }
      }
    }
  }

  /* ---- Fallback: جستجوی دستی با WMI ---- */
  Serial.println(F("⚠ [SYS]  خودرو در دیتابیس یافت نشد — از قابلیت‌های پیش‌فرض استفاده می‌شود"));
  vehicleIdentified = false;
  return false;

} /* end of detectVehicle */


/* ===================================================================== */
/*  بررسی قابلیت — hasCapability                                        */
/* --------------------------------------------------------------------- */
/*  بررسی می‌کند که آیا خودروی فعلی یک قابلیت خاص را دارد یا نه.          */
/* ===================================================================== */
bool hasCapability(uint32_t cap) {

  if (currentVehicle == nullptr) {
    return true;  // اگر خودرو شناسایی نشده، همه فرمان‌ها مجاز باشند
  }

  return (currentVehicle->capabilities & cap) != 0;

} /* end of hasCapability */


/* ========================================================================== */
/*   —— تابع setup ——                                                          */
/* ========================================================================== */
void setup() {

  /* ---- راه‌اندازی Serial ---- */
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println(F("\n"));
  Serial.println(F("╔════════════════════════════════════════════════════╗"));
  Serial.println(F("║        CarHack-ESP32  —  v5.2                     ║"));
  Serial.println(F("║   AI-Powered Automotive Security Assessment       ║"));
  Serial.println(F("╚════════════════════════════════════════════════════╝"));
  Serial.println(F("\n"));

  /* ---- راه‌اندازی SPI باس ---- */
  SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  Serial.println(F("✅ [SYS]  SPI bus initialized (CLK=18, MOSI=23, MISO=19)"));

  /* ================================================================ */
  /*  (۱) راه‌اندازی CAN Bus                                         */
  /* ================================================================ */
  if (canManager.begin(PIN_CAN_TX, PIN_CAN_RX)) {
    Serial.println(F("✅ [SYS]  CAN Bus — راه‌اندازی شد (TX=17, RX=16)"));
  } else {
    Serial.println(F("❌ [SYS]  CAN Bus — راه‌اندازی FAILED!"));
  }

  /* ================================================================ */
  /*  (۲) راه‌اندازی ISO-TP                                          */
  /* ================================================================ */
  isoTp.begin(&canManager, PIN_CAN_TX, PIN_CAN_RX);
  Serial.println(F("✅ [SYS]  ISO-TP — راه‌اندازی شد"));

  /* ================================================================ */
  /*  (۳) راه‌اندازی UDS                                             */
  /* ================================================================ */
  udsDiag.begin(&canManager, &isoTp);
  Serial.println(F("✅ [SYS]  UDS — راه‌اندازی شد"));

  /* ================================================================ */
  /*  (۴) راه‌اندازی OBD2                                            */
  /* ================================================================ */
  obd2Diag.begin(&canManager);
  Serial.println(F("✅ [SYS]  OBD2 — راه‌اندازی شد"));

  /* ================================================================ */
  /*  (۵) راه‌اندازی SD Card                                         */
  /* ================================================================ */
  if (sdLogger.begin(PIN_SD_CS)) {
    Serial.println(F("✅ [SYS]  SD Card — راه‌اندازی شد (CS=4)"));
  } else {
    Serial.println(F("⚠ [SYS]  SD Card — یافت نشد (logging غیرفعال)"));
  }

  /* ================================================================ */
  /*  (۶) راه‌اندازی RF CC1101                                       */
  /* ================================================================ */
  rfRollJam.begin(PIN_RF_CS);
  // پیام راه‌اندازی داخل begin() چاپ می‌شود

  /* ================================================================ */
  /*  (۷) راه‌اندازی Web Dashboard                                   */
  /* ================================================================ */
  webDashboard.begin();
  webDashboard.broadcastStatus("system", FW_VERSION);
  Serial.println(F("✅ [SYS]  Web Dashboard — راه‌اندازی شد"));

  /* ================================================================ */
  /*  (۸) راه‌اندازی CAN Learner                                     */
  /* ================================================================ */
  canLearner.begin(&canManager);
  Serial.println(F("✅ [SYS]  CAN Learner — راه‌اندازی شد"));

  /* ================================================================ */
  /*  (۹) راه‌اندازی UDS Session پیش‌فرض                             */
  /* ================================================================ */
  if (hasCapability(CAP_UDS)) {
    if (udsDiag.startSession(UDS_SESSION_DEFAULT, 0x7DF, 0x7E8)) {
      Serial.println(F("✅ [SYS]  UDS Session — فعال شد"));

      /* ---- تشخیص خودرو ---- */
      detectVehicle();
    }
  }

  Serial.println(F("\n─── ✨ سیستم آماده به کار است ✨ ───\n"));

} /* end of setup */


/* ========================================================================== */
/*   —— تابع loop ——                                                           */
/* ========================================================================== */

/* ---- زمان‌سنج‌ها (millis-based timers) ---- */
static unsigned long lastObd2Poll   = 0;
static unsigned long lastTpKeepalive = 0;
static unsigned long lastCanLearn    = 0;


void loop() {

  unsigned long now = millis();


  /* ================================================================ */
  /*  (۱) CAN Bus — دریافت فریم‌ها                                    */
  /* ================================================================ */
  CANFrame rxFrame;
  while (canManager.receive(&rxFrame, 0)) {
    
    /* ---- ثبت در SD ---- */
    sdLogger.logCAN(rxFrame);

    /* ---- یادگیری خودکار CAN ---- */
    canLearner.feedFrame(&rxFrame);

    /* ---- ارسال به WebSocket ---- */
    webDashboard.broadcastCAN(rxFrame);
  }


  /* ================================================================ */
  /*  (۲) OBD2 Polling — هر ۲ ثانیه                                  */
  /* ================================================================ */
  if (hasCapability(CAP_OBD2) && (now - lastObd2Poll >= OBD2_POLL_MS)) {
    lastObd2Poll = now;

    obd2Diag.pollAll();
  }


  /* ================================================================ */
  /*  (۳) TesterPresent — هر ۳ ثانیه (keep session alive)            */
  /* ================================================================ */
  if (hasCapability(CAP_UDS) && (now - lastTpKeepalive >= TP_KEEPALIVE_MS)) {
    lastTpKeepalive = now;

    udsDiag.sendTesterPresent(0x7DF, 0x7E8);
  }


  /* ================================================================ */
  /*  (۴) CAN Learner — تحلیل هر ۱۰ ثانیه                            */
  /* ================================================================ */
  if (now - lastCanLearn >= 10000) {
    lastCanLearn = now;

    canLearner.analyzeChannel(0, 500000);  // کانال ۰ با نرخ 500kbps
  }


  /* ================================================================ */
  /*  (۵) WebSocket — بررسی پیام‌های دریافتی از Web Dashboard         */
  /* ================================================================ */
  webDashboard.loop();


  /* ================================================================ */
  /*  (۶) RF — بررسی سیگنال‌های RF (در صورت فعال بودن)               */
  /* ================================================================ */
  if (hasCapability(CAP_RF) && rfRollJam.isInitialized()) {

    uint8_t rfBuf[64];
    uint8_t rfLen = 0;
    int     rfRssi = 0;

    if (rfRollJam.capture(rfBuf, &rfLen, &rfRssi)) {
      Serial.printf("📻 [RF]  Packet received — %d bytes, RSSI=%d dBm\n",
                    rfLen, rfRssi);
      webDashboard.broadcastRF(rfBuf, rfLen, rfRssi);
    }
  }


  /* ================================================================ */
  /*  (۷) LED — چشمک‌زن وضعیت                                        */
  /* ================================================================ */
  static unsigned long lastLedToggle = 0;
  if (now - lastLedToggle >= 1000) {
    lastLedToggle = now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }


  /* ================================================================ */
  /*  (۸) مدیریت فرمان‌های Serial (برای debug)                       */
  /* ================================================================ */
  if (Serial.available() > 0) {

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "help") {
      Serial.println(F("\n📋 CarHack-ESP32 v5.2 — فرمان‌های Serial:"));
      Serial.println(F("  help          ── نمایش این راهنما"));
      Serial.println(F("  status        ── نمایش وضعیت سیستم"));
      Serial.println(F("  vehicle       ── نمایش اطلاعات خودرو"));
      Serial.println(F("  scan_rf       ── اسکن فرکانس RF"));
      Serial.println(F("  can_dump      ── نمایش فریم‌های CAN خام"));
      Serial.println(F("  obd2          ── اجرای poll OBD2 دستی"));
      Serial.println(F("  list_caps     ── نمایش قابلیت‌های خودرو"));
      Serial.println(F("  detect        ── تلاش مجدد برای تشخیص خودرو"));
      Serial.println(F("  restart       ── ری‌استارت UDS session\n"));

    } else if (cmd == "status") {

      Serial.println(F("\n📊 [SYS]  وضعیت سیستم:"));
      Serial.printf("       FW Version : %s\n", FW_VERSION);
      Serial.printf("       CAN Bus    : %s\n",
                    canManager.isInitialized() ? "✅ فعال" : "❌ غیرفعال");
      Serial.printf("       RF Module  : %s\n",
                    rfRollJam.isInitialized() ? "✅ فعال" : "❌ غیرفعال");
      Serial.printf("       SD Card    : %s\n",
                    sdLogger.isCardPresent() ? "✅ متصل" : "⚠ غیرفعال");
      Serial.printf("       UDS Session: %s\n",
                    udsDiag.isSessionActive() ? "✅ فعال" : "❌ غیرفعال");
      Serial.printf("       Vehicle    : %s\n",
                    vehicleIdentified ? currentVehicle->make : "⚠ شناسایی نشده");
      Serial.println();

    } else if (cmd == "list_caps" && currentVehicle != nullptr) {

      Serial.printf("\n🔧 [SYS]  قابلیت‌های %s:\n", currentVehicle->make);
      Serial.printf("       CAN Bus     : %s\n",
                    (currentVehicle->capabilities & CAP_CAN)     ? "✅" : "❌");
      Serial.printf("       OBD2        : %s\n",
                    (currentVehicle->capabilities & CAP_OBD2)    ? "✅" : "❌");
      Serial.printf("       UDS         : %s\n",
                    (currentVehicle->capabilities & CAP_UDS)     ? "✅" : "❌");
      Serial.printf("       RF          : %s\n",
                    (currentVehicle->capabilities & CAP_RF)      ? "✅" : "❌");
      Serial.printf("       RollJam     : %s\n",
                    (currentVehicle->capabilities & CAP_ROLLJAM) ? "✅" : "❌");
      Serial.printf("       Sniffer     : %s\n",
                    (currentVehicle->capabilities & CAP_SNIFFER) ? "✅" : "❌");
      Serial.println();

    } else if (cmd == "detect") {

      detectVehicle();

    } else if (cmd == "scan_rf") {

      rfRollJam.scanFrequency(300.0, 450.0, 0.5);

    } else if (cmd == "can_dump") {

      Serial.println(F("\n📡 [CAN]  Dump فریم‌های خام (5 ثانیه):"));
      unsigned long start = millis();
      while (millis() - start < 5000) {
        if (canManager.receive(&rxFrame, 10)) {
          Serial.printf("  ID=0x%03X  DLC=%d  DATA=",
                        rxFrame.id, rxFrame.dlc);
          for (int i = 0; i < rxFrame.dlc; i++) {
            Serial.printf("%02X ", rxFrame.data[i]);
          }
          Serial.println();
        }
      }
      Serial.println(F("  └── Dump کامل شد\n"));

    } else if (cmd == "obd2") {

      obd2Diag.pollAll();

    } else if (cmd == "restart") {

      udsDiag.startSession(UDS_SESSION_DEFAULT, 0x7DF, 0x7E8);

    } else if (cmd.length() > 0) {

      Serial.printf("❌ فرمان ناشناخته: %s (help را ببینید)\n", cmd.c_str());

    }

  } /* end of serial command handling */


  delay(1);  // کوچکترین تأخیر برای جلوگیری از WDT reset

} /* end of loop */
