/******************************************************************************
 * sd_logger.h — ذخیره‌سازی داده‌ها روی SD Card
 * ============================================================================
 * توضیحات:
 *   ذخیره فریم‌های CAN، گزارش‌های OBD2، و ایونت‌های سیستم روی کارت SD.
 *
 * تغییرات v5.2:
 *   ✅ بررسی card_present در startSession — جلوگیری از کرش در نبود SD
 *   ✅ اضافه شدن isCardPresent برای تشخیص سلامت SD
 ******************************************************************************/

#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include "can_manager.h"


/* ========================================================================== */
/*   —— کلاس SDLogger ——                                                      */
/* ========================================================================== */
class SDLogger {

  /* ---- اعضای خصوصی ---- */
  private:

    bool  card_present   = false;
    int   cs_pin         = 0;
    File  logFile;
    bool  loggingActive  = false;


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                               */
    /* ===================================================================== */
    bool begin(int cs) {
      cs_pin = cs;

      if (!SD.begin(cs_pin)) {
        card_present = false;
        Serial.println(F("⚠ [SD]   کارت SD یافت نشد — logging غیرفعال است"));
        return false;
      }

      card_present = true;

      Serial.println(F("✅ [SD]   کارت SD آماده است"));

      /* ---- ایجاد دایرکتوری log (اگر وجود نداشته باشد) ---- */
      if (!SD.exists("/carhack")) {
        SD.mkdir("/carhack");
      }

      return true;

    } /* end of begin */


    /* ===================================================================== */
    /*  شروع جلسه ثبت — startSession                                        */
    /* --------------------------------------------------------------------- */
    /*  v5.2: بررسی card_present قبل از باز کردن فایل                        */
    /* ===================================================================== */
    bool startSession() {

      /* ---- ✅ بررسی وجود SD کارت (اصلاح v5.2) ---- */
      if (!card_present) {
        Serial.println(F("⚠ [SD]   کارت SD وجود ندارد — startSession رد شد"));
        return false;
      }

      /* ---- ساختن نام فایل بر اساس timestamp ---- */
      char filename[32];
      snprintf(filename, sizeof(filename),
               "/carhack/log_%lu.csv", millis());

      logFile = SD.open(filename, FILE_WRITE);
      if (!logFile) {
        Serial.printf("❌ [SD]   نمی‌توان فایل %s را باز کرد\n", filename);
        return false;
      }

      /* ---- نوشتن هدر CSV ---- */
      logFile.println("Timestamp_ms, CAN_ID, DLC, Data, Comment");
      logFile.flush();

      loggingActive = true;
      Serial.printf("✅ [SD]   جلسه ثبت شروع شد: %s\n", filename);

      return true;

    } /* end of startSession */


    /* ===================================================================== */
    /*  ثبت فریم CAN — logCAN                                               */
    /* ===================================================================== */
    void logCAN(const CANFrame& frame, const char* comment = nullptr) {

      if (!loggingActive || !logFile) return;

      logFile.print(millis());
      logFile.print(", 0x");
      logFile.print(frame.id, HEX);
      logFile.print(", ");
      logFile.print(frame.dlc);
      logFile.print(", ");

      for (int i = 0; i < frame.dlc; i++) {
        if (frame.data[i] < 0x10) logFile.print("0");
        logFile.print(frame.data[i], HEX);
        if (i < frame.dlc - 1) logFile.print(" ");
      }

      if (comment != nullptr) {
        logFile.print(", ");
        logFile.print(comment);
      }

      logFile.println();

    } /* end of logCAN */


    /* ===================================================================== */
    /*  پایان جلسه — endSession                                             */
    /* ===================================================================== */
    void endSession() {
      if (logFile) {
        logFile.close();
        loggingActive = false;
        Serial.println(F("✅ [SD]   جلسه ثبت پایان یافت"));
      }
    }


    /* ===================================================================== */
    /*  توابع دسترسی                                                        */
    /* ===================================================================== */

    bool  isCardPresent() { return card_present;  }
    bool  isLogging()     { return loggingActive; }


};  /* end of class SDLogger */


#endif  /* SD_LOGGER_H */
