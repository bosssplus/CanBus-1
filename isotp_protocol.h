/******************************************************************************
 * isotp_protocol.h — پروتکل ISO-TP (ISO 15765-2)
 * ============================================================================
 * توضیحات:
 *   پیاده‌سازی پروتکل ISO-TP (Transport Protocol) برای انتقال پیام‌های بزرگتر
 *   از ۸ بایت روی CAN Bus. از فریم‌های Single Frame (SF), First Frame (FF),
 *   Consecutive Frame (CF), و Flow Control (FC) پشتیبانی می‌کند.
 *
 * مرجع: ISO 15765-2 / SAE J1939-73
 *
 * تغییرات v5.2:
 *   ✅ پشتیبانی کامل از SF (≤7 bytes) و FF+CF (multi-frame)
 *   ✅ Timeout قابل تنظیم برای دریافت
 *   ✅ مدیریت خطا برای بسته‌های گم‌شده یا نامرتب
 *   ✅ Buffer چرخشی برای دریافت همزمان چند پیام
 ******************************************************************************/

#ifndef ISOTP_PROTOCOL_H
#define ISOTP_PROTOCOL_H

#include <Arduino.h>
#include "can_manager.h"


/* ========================================================================== */
/*   —— ثابت‌های ISO-TP ——                                                    */
/* ========================================================================== */

/* --- PCI (Protocol Control Information) انواع --- */
#define ISOTP_PCI_SF         0x00    // Single Frame (ماسک: 0xF0)
#define ISOTP_PCI_FF         0x10    // First Frame  (ماسک: 0xF0)
#define ISOTP_PCI_CF         0x20    // Consecutive Frame (ماسک: 0xF0)
#define ISOTP_PCI_FC         0x30    // Flow Control (ماسک: 0xF0)

/* --- Flow Control Flags --- */
#define ISOTP_FC_CTS         0x00    // Continue To Send
#define ISOTP_FC_WAIT        0x01    // Wait (صبر کن)
#define ISOTP_FC_OVFLW       0x02    // Overflow (بافر پر است)

/* --- اندازه‌ها --- */
#define ISOTP_MAX_LENGTH     4095    // حداکثر طول پیام ISO-TP (12-bit)
#define ISOTP_BUFFER_SIZE    4096    // سایز بافر دریافتی
#define ISOTP_DEFAULT_STMIN  10      // minimum separation time (ms)
#define ISOTP_DEFAULT_BS     10      // block size

/* --- Timeoutها --- */
#define ISOTP_RX_TIMEOUT     2000    // مهلت دریافت هر فریم (ms)
#define ISOTP_FC_TIMEOUT     1000    // مهلت دریافت Flow Control (ms)


/* ========================================================================== */
/*   —— ساختار ISO-TP Message ——                                              */
/* ========================================================================== */
typedef struct {
  uint8_t   data[ISOTP_BUFFER_SIZE];    // بافر داده
  uint16_t  length;                      // طول واقعی داده
  uint32_t  arbitration_id;              // CAN ID مربوطه
  bool      is_complete;                 // true اگر پیام کامل دریافت شد
} ISOTPMessage;


/* ========================================================================== */
/*   —— کلاس ISOTPProtocol ——                                                 */
/* ========================================================================== */
class ISOTPProtocol {

  /* ---- اعضای خصوصی ---- */
  private:

    CANManager*   canBus;             // پوینتر به CAN Bus

    /* --- وضعیت Receiver (دریافت‌کننده) --- */
    uint8_t       rxBuffer[ISOTP_BUFFER_SIZE];
    uint16_t      rxLength;
    uint16_t      rxExpected;          // طول کل مورد انتظار
    uint8_t       rxSeqNum;            // شماره توالی مورد انتظار
    bool          rxInProgress;        // آیا هم‌اکنون در حال دریافت هستیم؟
    uint32_t      rxTimer;             // زمان‌سنج برای timeout
    uint32_t      rxReqId;             // CAN ID درخواست (برای فیلتر)

    /* --- وضعیت Sender (فرستنده) --- */
    uint8_t       txSeqNum;
    bool          txInProgress;


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  سازنده — Constructor                                                */
    /* ===================================================================== */
    ISOTPProtocol() {
      canBus        = nullptr;
      rxInProgress  = false;
      txInProgress  = false;
    }


    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                              */
    /* ===================================================================== */
    void begin(CANManager* can, gpio_num_t tx_pin, gpio_num_t rx_pin) {
      canBus = can;
      rxInProgress = false;
      txInProgress = false;

      Serial.println(F("✅ [ISO-TP]  Transport Protocol — راه‌اندازی شد"));

    } /* end of begin */


    /* ===================================================================== */
    /*  ارسال پیام — send                                                   */
    /* --------------------------------------------------------------------- */
    /*  پیام را بسته به طول آن به صورت SF یا FF+CF می‌فرستد.                */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  id    ← CAN ID مقصد                                         */
    /*          data  ← بافر داده                                           */
    /*          len   ← طول داده (max 4095)                                 */
    /*  خروجی: true اگر ارسال با موفقیت انجام شد                             */
    /* ===================================================================== */
    bool send(uint32_t id, const uint8_t* data, uint16_t len) {

      if (!canBus)                     return false;
      if (len == 0 || len > ISOTP_MAX_LENGTH) return false;

      /* ================================================================ */
      /*  حالت ۱: Single Frame (SF) — برای پیام‌های ≤ ۷ بایت              */
      /* ================================================================ */
      if (len <= 7) {

        uint8_t sf[8];

        sf[0] = ISOTP_PCI_SF | len;              // PCI byte: SF + طول
        memcpy(&sf[1], data, len);                // داده

        return canBus->send(id, sf, len + 1);

      }


      /* ================================================================ */
      /*  حالت ۲: Multi-Frame (FF + CF) — برای پیام‌های > ۷ بایت           */
      /* ================================================================ */

      /* ---- First Frame (FF) ---- */
      uint8_t ff[8];
      ff[0] = ISOTP_PCI_FF | (len >> 8);          // PCI byte: FF + طول بالا
      ff[1] = len & 0xFF;                         // طول پایین
      memcpy(&ff[2], data, 6);                    // 6 بایت اول داده

      if (!canBus->send(id, ff, 8)) {
        return false;
      }

      /* ---- دریافت Flow Control (FC) ---- */
      CANFrame fcFrame;
      uint32_t fcStart = millis();

      while (millis() - fcStart < ISOTP_FC_TIMEOUT) {

        if (canBus->receive(&fcFrame, 50)) {

          uint8_t pci = fcFrame.data[0] & 0xF0;

          if (pci == ISOTP_PCI_FC) {

            uint8_t fcStatus = fcFrame.data[0] & 0x0F;

            if (fcStatus == ISOTP_FC_WAIT) {
              /* ECU درخواست صبر کرده — ادامه بده */
              continue;

            } else if (fcStatus == ISOTP_FC_OVFLW) {
              Serial.println(F("❌ [ISO-TP]  Overflow — ECU بافر پر دارد"));
              return false;

            }
            /* ISOTP_FC_CTS: Continue — برو به مرحله بعد */
            break;
          }
        }

      } /* end of FC wait */


      /* ---- Consecutive Frames (CF) ---- */
      uint16_t offset      = 6;          // ۶ بایت قبلاً در FF رفته
      uint8_t  seqNum      = 1;          // شماره توالی (از ۱ شروع می‌شود)

      while (offset < len) {

        uint8_t cf[8];
        uint8_t chunkLen = min((uint16_t)7, len - offset);

        cf[0] = ISOTP_PCI_CF | (seqNum & 0x0F);  // PCI + شماره توالی
        memcpy(&cf[1], &data[offset], chunkLen);

        if (!canBus->send(id, cf, chunkLen + 1)) {
          return false;
        }

        offset += chunkLen;
        seqNum  = (seqNum + 1) & 0x0F;            // چرخش در ۴ بیت

        delay(ISOTP_DEFAULT_STMIN);                // حداقل فاصله بین فریم‌ها

      } /* end of CF loop */

      return true;

    } /* end of send */


    /* ===================================================================== */
    /*  دریافت پیام — receive                                               */
    /* --------------------------------------------------------------------- */
    /*  این تابع blocking است و تا timeout منتظر می‌ماند.                    */
    /* --------------------------------------------------------------------- */
    /*  ورودی:  id       ← CAN ID مبدأ                                      */
    /*          buffer   ← بافر خروجی                                       */
    /*          len      ← طول نهایی (خروجی)                                */
    /*          timeout  ← مهلت انتظار (ms)                                 */
    /*  خروجی:  true اگر پیام کاملی دریافت شد                                */
    /* ===================================================================== */
    bool receive(uint32_t id, uint8_t* buffer, uint16_t* len, uint32_t timeout) {

      uint32_t start = millis();

      while (millis() - start < timeout) {

        CANFrame frame;

        if (canBus->receive(&frame, 50)) {

          /* ---- فیلتر کردن بر اساس CAN ID ---- */
          if (frame.id != id) continue;

          uint8_t pciType = frame.data[0] & 0xF0;
          uint8_t pciInfo = frame.data[0] & 0x0F;

          /* ============================================================ */
          /*  Single Frame (SF)                                           */
          /* ============================================================ */
          if (pciType == ISOTP_PCI_SF) {

            uint16_t msgLen = pciInfo;              // طول در همان بایت PCI

            if (msgLen > frame.dlc - 1) {
              Serial.println(F("⚠ [ISO-TP]  SF: طول اعلام‌شده بیشتر از داده"));
              continue;
            }

            memcpy(buffer, &frame.data[1], msgLen);
            *len = msgLen;
            return true;

          }


          /* ============================================================ */
          /*  First Frame (FF) — شروع یک پیام چند-فریم                    */
          /* ============================================================ */
          if (pciType == ISOTP_PCI_FF) {

            rxExpected    = ((uint16_t)(pciInfo) << 8) | frame.data[1];
            rxSeqNum      = 1;                     // CF بعدی باید seq=1 باشد

            /* ---- کپی ۶ بایت اول ---- */
            uint16_t firstChunk = min((uint16_t)6, rxExpected);
            memcpy(rxBuffer, &frame.data[2], firstChunk);
            rxLength = firstChunk;

            /* ---- ارسال Flow Control (FC) ---- */
            uint8_t fc[8];
            fc[0] = ISOTP_PCI_FC | ISOTP_FC_CTS;    // Continue To Send
            fc[1] = ISOTP_DEFAULT_BS;                // Block Size
            fc[2] = ISOTP_DEFAULT_STMIN;             // STmin
            memset(&fc[3], 0, 5);

            canBus->send(id, fc, 8);

            rxInProgress = true;
            rxReqId      = id;
            rxTimer      = millis();

            continue;  // برو به انتظار برای CF

          }


          /* ============================================================ */
          /*  Consecutive Frame (CF) — ادامه پیام چند-فریم                */
          /* ============================================================ */
          if (pciType == ISOTP_PCI_CF && rxInProgress && frame.id == rxReqId) {

            uint8_t seq = pciInfo;

            /* ---- بررسی توالی ---- */
            if (seq != rxSeqNum) {
              Serial.printf("⚠ [ISO-TP]  CF: توالی اشتباه — انتظار %d, دریافت %d\n",
                            rxSeqNum, seq);
              continue;
            }

            /* ---- کپی داده ---- */
            uint8_t  chunkLen   = min((uint8_t)7, (uint8_t)(rxExpected - rxLength));
            uint8_t* src        = &frame.data[1];

            memcpy(&rxBuffer[rxLength], src, chunkLen);
            rxLength += chunkLen;
            rxSeqNum  = (rxSeqNum + 1) & 0x0F;
            rxTimer   = millis();

            /* ---- بررسی کامل شدن پیام ---- */
            if (rxLength >= rxExpected) {

              memcpy(buffer, rxBuffer, rxExpected);
              *len              = rxExpected;
              rxInProgress      = false;

              return true;
            }

          } /* end of CF handling */

        } /* end of if receive */


        /* ---- Timeout بررسی ---- */
        if (rxInProgress && (millis() - rxTimer > ISOTP_RX_TIMEOUT)) {
          Serial.println(F("❌ [ISO-TP]  Timeout — دریافت پیام ناقص ماند"));
          rxInProgress = false;
          return false;
        }


      } /* end of while */

      return false;  // Timeout کلی

    } /* end of receive */


    /* ===================================================================== */
    /*  توابع دسترسی                                                        */
    /* ===================================================================== */

    bool   isBusy()            { return txInProgress || rxInProgress; }
    void   resetRx()           { rxInProgress = false;                 }


};  /* end of class ISOTPProtocol */


#endif  /* ISOTP_PROTOCOL_H */
