/******************************************************************************
 * uds_diag.h — پروتکل UDS (ISO 14229) روی ISO-TP (ISO 15765-2)
 * ============================================================================
 * توضیحات:
 *   پیاده‌سازی سرویس‌های UDS شامل ReadDataByIdentifier, WriteDataByIdentifier,
 *   DiagnosticSessionControl, ECUReset, TesterPresent, و SecurityAccess.
 *
 * تغییرات v5.2:
 *   ✅ اصلاح sendTesterPresent — ارسال از طریق ISO-TP به جای CAN مستقیم
 *   ✅ بررسی isoTpSessionActive قبل از ارسال TesterPresent
 *   ✅ مستندسازی کامل تمام SIDها و NRCها
 ******************************************************************************/

#ifndef UDS_DIAG_H
#define UDS_DIAG_H

#include <Arduino.h>
#include "can_manager.h"
#include "isotp_protocol.h"


/* ========================================================================== */
/*   —— ثابت‌های UDS ——                                                       */
/* ========================================================================== */

/* --- SIDها (Service Identifiers) مطابق ISO 14229-1 --- */
#define UDS_SID_DIAG_SESSION_CTRL        0x10   // DiagnosticSessionControl
#define UDS_SID_ECU_RESET                0x11   // ECUReset
#define UDS_SID_READ_DATA_BY_ID          0x22   // ReadDataByIdentifier
#define UDS_SID_WRITE_DATA_BY_ID         0x2E   // WriteDataByIdentifier
#define UDS_SID_SECURITY_ACCESS          0x27   // SecurityAccess
#define UDS_SID_COMMUNICATION_CTRL       0x28   // CommunicationControl
#define UDS_SID_TESTER_PRESENT           0x3E   // TesterPresent
#define UDS_SID_ACCESS_TIMING_PARAM      0x83   // AccessTimingParameters
#define UDS_SID_SECURED_DATA_TRANSMIT    0x84   // SecuredDataTransmission
#define UDS_SID_CONTROL_DTC_SETTINGS     0x85   // ControlDTCSettings
#define UDS_SID_RESPONSE_OFFSET          0x40   // به ازای هر SID، پاسخ = SID + 0x40

/* --- Session Typeها (برای DiagnosticSessionControl) --- */
#define UDS_SESSION_DEFAULT              0x01   // Session پیش‌فرض
#define UDS_SESSION_PROGRAMMING          0x02   // Session برنامه‌ریزی
#define UDS_SESSION_EXTENDED             0x03   // Session گسترده
#define UDS_SESSION_SAFETY               0x04   // Session ایمنی

/* --- Sub-functionهای TesterPresent --- */
#define UDS_TP_ZERO_RESPONSE             0x80   // بدون پاسخ از ECU

/* --- NRCها (Negative Response Codes) مطابق ISO 14229-1 --- */
#define UDS_NRC_GENERAL_REJECT           0x10   // رد عمومی
#define UDS_NRC_SERVICE_NOT_SUPPORTED    0x11   // سرویس پشتیبانی نمی‌شود
#define UDS_NRC_SUBFUNC_NOT_SUPPORTED    0x12   // زیرفرمان پشتیبانی نمی‌شود
#define UDS_NRC_INVALID_MESSAGE_LEN      0x13   // طول پیام نامعتبر
#define UDS_NRC_RESPONSE_TOO_LONG        0x14   // پاسخ طولانی
#define UDS_NRC_CONDITIONS_NOT_CORRECT   0x22   // شرایط فراهم نیست
#define UDS_NRC_REQUEST_SEQUENCE_ERR     0x24   // توالی درخواست اشتباه
#define UDS_NRC_SECURITY_ACCESS_DENIED   0x33   // دسترسی امنیتی رد شد
#define UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED 0x70  // آپلود/دانلود رد شد


/* ========================================================================== */
/*   —— کلاس UDSDiagnostic ——                                                  */
/* ========================================================================== */
class UDSDiagnostic {

  /* ---- اعضای خصوصی ---- */
  private:

    CANManager*   canBus;             // پوینتر به مدیر CAN
    ISOTPProtocol* isoTp;             // پوینتر به لایه ISO-TP

    bool          isTpSessionActive   = false;   // آیا ISO-TP session فعال است؟
    uint16_t      currentReqId        = 0x7DF;   // درخواست ID پیش‌فرض
    uint16_t      currentRespId       = 0x7E8;   // پاسخ ID پیش‌فرض


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  مقداردهی اولیه                                                       */
    /* ===================================================================== */
    void begin(CANManager* can, ISOTPProtocol* tp) {
      canBus = can;
      isoTp  = tp;
      isTpSessionActive = false;
    }


    /* ===================================================================== */
    /*  شروع Diagnostic Session                                              */
    /* --------------------------------------------------------------------- */
    /*  sessionType = UDS_SESSION_DEFAULT / PROGRAMMING / EXTENDED / SAFETY  */
    /* ===================================================================== */
    bool startSession(uint8_t sessionType, uint16_t reqId, uint16_t respId) {

      currentReqId  = reqId;
      currentRespId = respId;

      uint8_t request[] = {
        0x02,                      // PCI: 2 bytes
        UDS_SID_DIAG_SESSION_CTRL, // SID: 0x10
        sessionType                // Sub-function: session type
      };

      if (!isoTp || !isoTp->send(currentReqId, request, sizeof(request))) {
        Serial.println(F("❌ [UDS]  ارسال DiagnosticSessionControl failed"));
        return false;
      }

      /* ---- دریافت پاسخ ---- */
      uint8_t response[64];
      uint16_t respLen = 0;
      
      if (isoTp->receive(currentRespId, response, &respLen, 1000)) {

        /* بررسی Positive Response: byte[1] == SID + 0x40 */
        if (respLen >= 2 && response[0] == 0x03 &&
            response[1] == (UDS_SID_DIAG_SESSION_CTRL | UDS_SID_RESPONSE_OFFSET)) {
          
          isTpSessionActive = true;
          
          Serial.printf("✅ [UDS]  Session %02X started — req=0x%03X resp=0x%03X\n",
                        sessionType, currentReqId, currentRespId);
          return true;
        }

        /* بررسی Negative Response */
        if (respLen >= 3 && response[1] == 0x7F) {
          Serial.printf("❌ [UDS]  Session start NRC: 0x%02X\n", response[2]);
        }

      }

      isTpSessionActive = false;
      return false;

    } /* end of startSession */


    /* ===================================================================== */
    /*  TesterPresent — نگه‌داری Session فعال                                */
    /* --------------------------------------------------------------------- */
    /*  این تابع باید به صورت دوره‌ای (هر ۲-۵ ثانیه) فراخوانی شود.            */
    /*  تغییر v5.2: ارسال از طریق ISO-TP به جای CAN مستقیم.                  */
    /* ===================================================================== */
    bool sendTesterPresent(uint16_t req_id, uint16_t resp_id) {

      /* ---- بررسی فعال بودن ISO-TP session ---- */
      if (!isTpSessionActive) {
        Serial.println(F("⚠ [UDS]  TesterPresent: ISO-TP session فعال نیست"));
        Serial.println(F("        └─ ابتدا startSession را فراخوانی کنید"));
        return false;
      }

      /* ---- بسته TesterPresent (بدون پاسخ از ECU) ---- */
      uint8_t tp_frame[] = {
        0x02,                        // PCI: 2 bytes following
        UDS_SID_TESTER_PRESENT,      // SID: 0x3E
        UDS_TP_ZERO_RESPONSE         // Sub-function: suppress response
      };

      /* ---- ارسال از طریق ISO-TP (نه مستقیم CAN) ---- */
      bool result = isoTp->send(req_id, tp_frame, sizeof(tp_frame));

      if (result) {
        Serial.println(F("💚 [UDS]  TesterPresent — sent (keep-alive)"));
      } else {
        Serial.println(F("❌ [UDS]  TesterPresent — ارسال failed"));
      }

      return result;

    } /* end of sendTesterPresent */


    /* ===================================================================== */
    /*  ReadDataByIdentifier — خواندن داده با شناسه                           */
    /* --------------------------------------------------------------------- */
    /*  dataId شامل DIDهایی مثل 0xF190 (VIN), 0xF18C (ECU Serial) و...       */
    /* ===================================================================== */
    bool readDataByIdentifier(uint16_t dataId,
                              uint8_t* outBuf, uint16_t* outLen) {

      if (!isTpSessionActive) {
        Serial.println(F("⚠ [UDS]  ReadDataByIdentifier: session فعال نیست"));
        return false;
      }

      uint8_t request[] = {
        0x04,                        // PCI: 4 bytes
        UDS_SID_READ_DATA_BY_ID,     // SID: 0x22
        (uint8_t)(dataId >> 8),      // DID high byte
        (uint8_t)(dataId & 0xFF)     // DID low byte
      };

      if (!isoTp->send(currentReqId, request, sizeof(request))) {
        return false;
      }

      /* ---- دریافت پاسخ ---- */
      if (isoTp->receive(currentRespId, outBuf, outLen, 2000)) {
        return (*outLen > 0);
      }

      return false;

    } /* end of readDataByIdentifier */


    /* ===================================================================== */
    /*  SecurityAccess — دسترسی امنیتی (Unlock ECU)                          */
    /* --------------------------------------------------------------------- */
    /*  seed → key challenge-response برای دسترسی به سرویس‌های حساس          */
    /* ===================================================================== */
    bool securityAccess(uint8_t accessLevel,
                        uint8_t* seed, uint8_t seedLen,
                        uint8_t* key,  uint8_t keyLen) {

      if (!isTpSessionActive) return false;

      /* ---- مرحله ۱: درخواست Seed ---- */
      uint8_t req_seed[] = {
        0x03,                         // PCI: 3 bytes
        UDS_SID_SECURITY_ACCESS,      // SID: 0x27
        accessLevel                   // Sub-function (odd = request seed)
      };

      if (!isoTp->send(currentReqId, req_seed, sizeof(req_seed))) {
        return false;
      }

      uint8_t resp_seed[64];
      uint16_t resp_len = 0;
      
      if (!isoTp->receive(currentRespId, resp_seed, &resp_len, 2000)) {
        return false;
      }

      /* ---- مرحله ۲: ارسال Key ---- */
      uint8_t req_key[4 + keyLen];
      req_key[0] = 0x03 + keyLen;                // PCI
      req_key[1] = UDS_SID_SECURITY_ACCESS;      // SID: 0x27
      req_key[2] = accessLevel + 1;              // Sub-function (even = send key)
      memcpy(&req_key[3], key, keyLen);

      return isoTp->send(currentReqId, req_key, sizeof(req_key));

    } /* end of securityAccess */


    /* ===================================================================== */
    /*  توابع کمکی                                                           */
    /* ===================================================================== */

    bool  isSessionActive()        { return isTpSessionActive; }
    void  setSessionActive(bool a) { isTpSessionActive = a;    }


};  /* end of class UDSDiagnostic */


#endif  /* UDS_DIAG_H */
