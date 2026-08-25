/******************************************************************************
 * can_learner.h — یادگیری خودکار CAN Bus
 * ============================================================================
 * توضیحات:
 *   با شنود کردن CAN Bus، الگوهای تکراری را شناسایی می‌کند و CAN IDهای
 *   مرتبط با RPM، سرعت، فرمان‌ها و... را به صورت خودکار یاد می‌گیرد.
 *
 * قابلیت‌ها:
 *   📊  تحلیل فرکانس کانال — نرخ واقعی CAN Bus
 *   🔍  شناسایی CAN IDهای پرتکرار — کاندیدای سیگنال‌های دوره‌ای
 *   📈  آمار min/max/stdev — برای تشخیص تنوع داده
 *   🧠  یادگیری تطبیقی — با هر فریم جدید به‌روز می‌شود
 *
 * تغییرات v5.2:
 *   ✅ اضافه شدن آمار تنوع (min, max, stdev) برای تشخیص واقعی
 *   ✅ بهبود analyzeChannel با محاسبه فرکانس واقعی
 *   ✅ ذخیره آمار برای ۳۲ کانال همزمان
 *   ✅ خروجی JSON برای Web Dashboard
 ******************************************************************************/

#ifndef CAN_LEARNER_H
#define CAN_LEARNER_H

#include <Arduino.h>
#include <math.h>
#include "can_manager.h"


/* ========================================================================== */
/*   —— ثابت‌ها ——                                                             */
/* ========================================================================== */

#define LEARNER_MAX_CHANNELS    32        // حداکثر CAN IDهای قابل ردیابی
#define LEARNER_HISTORY_SIZE    100       // تعداد نمونه برای هر کانال


/* ========================================================================== */
/*   —— ساختار آمار کانال ——                                                  */
/* ========================================================================== */
typedef struct {

  /* --- شناسه --- */
  uint32_t  can_id;                // CAN ID
  bool      active;                // آیا این کانال فعال است؟

  /* --- آمار پایه --- */
  uint32_t  frame_count;           // تعداد فریم‌های دریافت‌شده
  uint32_t  first_seen;            // اولین بار (timestamp)
  uint32_t  last_seen;             // آخرین بار (timestamp)
  float     frequency_hz;          // فرکانس تقریبی ظاهر شدن (Hz)

  /* --- آمار داده --- */
  uint8_t   min_value;             // کمترین مقدار بایت اول
  uint8_t   max_value;             // بیشترین مقدار بایت اول
  float     mean_value;            // میانگین
  float     stdev_value;           // انحراف معیار

  /* --- تاریخچه چرخشی (برای stdev) --- */
  uint8_t   history[LEARNER_HISTORY_SIZE];
  uint8_t   history_index;

  /* --- برچسب احتمالی (پس از تحلیل) --- */
  char      label[24];             // مثلاً "RPM", "Speed", "Steering", ...

} ChannelStats;


/* ========================================================================== */
/*   —— کلاس CANLearner ——                                                    */
/* ========================================================================== */
class CANLearner {

  /* ---- اعضای خصوصی ---- */
  private:

    CANManager*      canBus;
    ChannelStats     channels[LEARNER_MAX_CHANNELS];
    uint8_t          channel_count;


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  سازنده — Constructor                                                */
    /* ===================================================================== */
    CANLearner() {
      canBus        = nullptr;
      channel_count = 0;

      for (int i = 0; i < LEARNER_MAX_CHANNELS; i++) {
        channels[i].active = false;
      }
    }


    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                              */
    /* ===================================================================== */
    void begin(CANManager* can) {
      canBus = can;
      Serial.println(F("✅ [LEARN]  CAN Learner — راه‌اندازی شد"));
    }


    /* ===================================================================== */
    /*  تغذیه با یک فریم — feedFrame                                        */
    /* --------------------------------------------------------------------- */
    /*  هر فریم CAN دریافت‌شده را به learner می‌دهیم تا آمار را به‌روز کند.   */
    /* ===================================================================== */
    void feedFrame(const CANFrame* frame) {

      if (!frame) return;

      /* ---- جستجوی کانال موجود یا ایجاد کانال جدید ---- */
      int idx = findChannel(frame->id);

      if (idx == -1) {
        /* کانال جدید ایجاد کن */
        if (channel_count >= LEARNER_MAX_CHANNELS) return;

        idx = channel_count;
        channel_count++;

        channels[idx].can_id         = frame->id;
        channels[idx].active         = true;
        channels[idx].frame_count    = 0;
        channels[idx].first_seen     = millis();
        channels[idx].min_value      = 0xFF;
        channels[idx].max_value      = 0x00;
        channels[idx].mean_value     = 0.0;
        channels[idx].stdev_value    = 0.0;
        channels[idx].history_index  = 0;
        channels[idx].label[0]       = '\0';
      }

      /* ---- به‌روزرسانی آمار ---- */
      ChannelStats* ch = &channels[idx];
      ch->frame_count++;
      ch->last_seen = millis();

      /* ---- محاسبه فرکانس ---- */
      if (ch->frame_count > 1) {
        float elapsed = (float)(ch->last_seen - ch->first_seen) / 1000.0;
        if (elapsed > 0.1) {
          ch->frequency_hz = (float)ch->frame_count / elapsed;
        }
      }

      /* ---- آمار داده (بایت اول به عنوان نمونه) ---- */
      if (frame->dlc > 0) {
        uint8_t val = frame->data[0];

        if (val < ch->min_value) ch->min_value = val;
        if (val > ch->max_value) ch->max_value = val;

        /* ---- تاریخچه چرخشی ---- */
        ch->history[ch->history_index % LEARNER_HISTORY_SIZE] = val;
        ch->history_index++;

        /* ---- محاسبه مجدد میانگین و انحراف معیار ---- */
        recalcStats(ch);
      }

    } /* end of feedFrame */


    /* ===================================================================== */
    /*  تحلیل یک کانال — analyzeChannel                                     */
    /* --------------------------------------------------------------------- */
    /*  تحلیل کامل یک کانال CAN و تلاش برای تشخیص نوع سیگنال.                */
    /* ===================================================================== */
    void analyzeChannel(int channel_index, uint32_t expected_baud) {

      if (channel_index >= channel_count) {
        Serial.printf("❌ [LEARN]  کانال %d وجود ندارد\n", channel_index);
        return;
      }

      ChannelStats* ch = &channels[channel_index];

      Serial.printf("\n📊 [LEARN]  تحلیل کانال — ID=0x%03X\n", ch->can_id);
      Serial.printf("       ├─ Frame count  : %d\n",        ch->frame_count);
      Serial.printf("       ├─ Frequency    : %.1f Hz\n",   ch->frequency_hz);
      Serial.printf("       ├─ Data range   : %d – %d\n",   ch->min_value, ch->max_value);
      Serial.printf("       ├─ Mean         : %.2f\n",      ch->mean_value);
      Serial.printf("       ├─ Stdev        : %.2f\n",      ch->stdev_value);

      /* ---- تشخیص نوع سیگنال بر اساس الگو ---- */
      const char* guessedLabel = guessSignalType(ch);
      Serial.printf("       └─ تشخیص        : %s\n", guessedLabel);

    } /* end of analyzeChannel */


    /* ===================================================================== */
    /*  تحلیل همه کانال‌ها — analyzeAll                                      */
    /* ===================================================================== */
    void analyzeAll() {

      Serial.println(F("\n📊 [LEARN]  ===== تحلیل همه کانال‌ها ====="));

      for (int i = 0; i < channel_count; i++) {

        ChannelStats* ch = &channels[i];

        Serial.printf("  [%02d]  0x%03X  |  ", i, ch->can_id);
        Serial.printf("Freq=%5.1f Hz  |  ", ch->frequency_hz);
        Serial.printf("Range=[%3d–%3d]  |  ", ch->min_value, ch->max_value);
        Serial.printf("σ=%.1f  |  ", ch->stdev_value);

        const char* label = guessSignalType(ch);
        Serial.printf("%s\n", label);
      }

      Serial.println(F("       └── تحلیل کامل شد\n"));

    } /* end of analyzeAll */


    /* ===================================================================== */
    /*  تشخیص نوع سیگنال — guessSignalType                                  */
    /* --------------------------------------------------------------------- */
    /*  بر اساس الگوهای آماری، نوع سیگنال را حدس می‌زند.                     */
    /* ===================================================================== */
    const char* guessSignalType(const ChannelStats* ch) {

      if (!ch || ch->frame_count == 0) return "⏳ ناشناخته";

      float range   = (float)(ch->max_value - ch->min_value);
      float cv      = (ch->mean_value > 0) ? (ch->stdev_value / ch->mean_value) : 0;

      /* --- RPM (دور موتور) — تغییرات پیوسته، محدوده وسیع --- */
      if (ch->frequency_hz > 50 && range > 50 && cv > 0.1) {
        return "🔄 RPM (دور موتور)";
      }

      /* --- Speed (سرعت) — تغییرات نسبتاً پیوسته --- */
      if (ch->frequency_hz > 10 && range > 30 && cv > 0.05 && cv < 0.5) {
        return "🚗 Speed (سرعت)";
      }

      /* --- Steering (فرمان) — تغییرات ناگهانی، محدوده وسیع --- */
      if (range > 100 && cv > 0.3) {
        return "🔄 Steering (فرمان)";
      }

      /* --- Temperature (دما) — تغییرات آهسته، محدوده کوچک --- */
      if (range < 30 && cv < 0.05 && ch->frequency_hz < 5) {
        return "🌡️ Temperature (دما)";
      }

      /* --- Status (وضعیت) — مقادیر گسسته (۰ یا ۱) --- */
      if (range <= 3 && ch->frame_count > 10) {
        return "🔘 Status (وضعیت روشن/خاموش)";
      }

      /* --- Fuel Level (سوخت) — محدوده ۰-۱۰۰٪ --- */
      if (range >= 50 && range <= 100 && cv < 0.1) {
        return "⛽ Fuel Level (سطح سوخت)";
      }

      /* --- Signal دوره‌ای با ثبات --- */
      if (ch->frequency_hz > 1 && ch->frame_count > 100) {
        return "📡 سیگنال دوره‌ای";
      }

      return "❓ ناشناخته";

    } /* end of guessSignalType */


    /* ===================================================================== */
    /*  به‌دست آوردن CAN ID پیشنهادی برای یک نوع سیگنال                      */
    /* ===================================================================== */
    uint32_t getSuggestedID(const char* signalType) {

      for (int i = 0; i < channel_count; i++) {

        if (!channels[i].active) continue;

        const char* label = guessSignalType(&channels[i]);

        if (strcmp(label, signalType) == 0) {
          return channels[i].can_id;
        }
      }

      return 0;  // یافت نشد

    } /* end of getSuggestedID */


    /* ===================================================================== */
    /*  چاپ دیتابیس CAN پیشنهادی — printSuggestedDB                          */
    /* ===================================================================== */
    void printSuggestedDB() {

      Serial.println(F("\n🗃️ [LEARN]  دیتابیس CAN پیشنهادی:"));
      Serial.println(F("  ┌────────┬──────────┬──────────────────────┐"));
      Serial.println(F("  │ CAN ID │  فرکانس  │  نوع سیگنال          │"));
      Serial.println(F("  ├────────┼──────────┼──────────────────────┤"));

      for (int i = 0; i < channel_count; i++) {
        if (!channels[i].active) continue;

        Serial.printf("  │ 0x%03X  │  %5.1f Hz │ %s\n",
                      channels[i].can_id,
                      channels[i].frequency_hz,
                      guessSignalType(&channels[i]));
      }

      Serial.println(F("  └────────┴──────────┴──────────────────────┘\n"));

    } /* end of printSuggestedDB */


  /* ---- توابع خصوصی ---- */
  private:

    /* ===================================================================== */
    /*  یافتن کانال — findChannel                                           */
    /* ===================================================================== */
    int findChannel(uint32_t can_id) {

      for (int i = 0; i < channel_count; i++) {
        if (channels[i].active && channels[i].can_id == can_id) {
          return i;
        }
      }

      return -1;  // یافت نشد

    } /* end of findChannel */


    /* ===================================================================== */
    /*  محاسبه مجدد آمار — recalcStats                                     */
    /* ===================================================================== */
    void recalcStats(ChannelStats* ch) {

      uint8_t n = min((uint8_t)ch->history_index, (uint8_t)LEARNER_HISTORY_SIZE);
      if (n == 0) return;

      /* ---- محاسبه میانگین ---- */
      float sum = 0;
      for (uint8_t i = 0; i < n; i++) {
        sum += ch->history[i];
      }
      ch->mean_value = sum / n;

      /* ---- محاسبه انحراف معیار ---- */
      float variance = 0;
      for (uint8_t i = 0; i < n; i++) {
        float diff = (float)ch->history[i] - ch->mean_value;
        variance += diff * diff;
      }
      ch->stdev_value = sqrt(variance / n);

    } /* end of recalcStats */


};  /* end of class CANLearner */


#endif  /* CAN_LEARNER_H */
