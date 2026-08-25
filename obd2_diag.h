/******************************************************************************
 * obd2_diag.h — پروتکل OBD2 (SAE J1979)
 * ============================================================================
 * توضیحات:
 *   پیاده‌سازی سرویس‌های OBD2 برای خواندن پارامترهای خودرو از طریق CAN Bus.
 *   از حالت ISO 15765-4 (CAN 11-bit, 500kbps) استفاده می‌کند.
 *
 * PIDهای پشتیبانی‌شده:
 *   0x00 — Supported PIDs (01-20)
 *   0x04 — Engine Load (%)
 *   0x05 — Coolant Temperature (°C)
 *   0x0C — Engine RPM
 *   0x0D — Vehicle Speed (km/h)
 *   0x11 — Throttle Position (%)
 *   0x21 — Distance Traveled (km)
 *   0x2F — Fuel Level (%)
 *   0x46 — Ambient Temperature (°C)
 *   0x51 — Fuel Type
 *
 * تغییرات v5.2:
 *   ✅ مقداردهی supported_pids در begin() — خوانده شود
 *   ✅ افزایش poll interval به ۲ ثانیه
 *   ✅ بهبود parsing داده‌های دریافتی
 *   ✅ پشتیبانی از همه PIDهای رایج
 ******************************************************************************/

#ifndef OBD2_DIAG_H
#define OBD2_DIAG_H

#include <Arduino.h>
#include "can_manager.h"


/* ========================================================================== */
/*   —— ثابت‌های OBD2 ——                                                     */
/* ========================================================================== */

/* --- CAN IDها --- */
#define OBD2_REQ_ID           0x7DF      // ID درخواست (broadcast)
#define OBD2_RESP_ID          0x7E8      // ID پاسخ (ECU پیش‌فرض)

/* --- SIDها (Service IDs) --- */
#define OBD2_SID_READ_PIDS    0x01       // Show Current Data
#define OBD2_SID_READ_FREEZE  0x02       // Show Freeze Frame Data
#define OBD2_SID_READ_DTC     0x03       // Read Diagnostic Trouble Codes
#define OBD2_SID_CLEAR_DTC    0x04       // Clear DTC
#define OBD2_SID_READ_SCAN    0x09       // Read Vehicle Information

/* --- PIDهای پشتیبانی‌شده (Mode 01) --- */
#define PID_SUPPORTED_01_20   0x00       // کدام PIDها پشتیبانی می‌شوند؟
#define PID_ENGINE_LOAD       0x04       // بار موتور (%)
#define PID_COOLANT_TEMP      0x05       // دمای مایع خنک‌کننده (°C)
#define PID_ENGINE_RPM        0x0C       // دور موتور (RPM)
#define PID_VEHICLE_SPEED     0x0D       // سرعت خودرو (km/h)
#define PID_TIMING_ADVANCE    0x0E       // زمان‌بندی جرقه (°BTDC)
#define PID_INTAKE_TEMP       0x0F       // دمای هوای ورودی (°C)
#define PID_MAF_AIR_FLOW      0x10       // جریان هوای MAF (g/s)
#define PID_THROTTLE_POS      0x11       // موقعیت دریچه گاز (%)
#define PID_DISTANCE          0x21       // مسافت طی‌شده (km)
#define PID_FUEL_LEVEL        0x2F       // سطح سوخت (%)
#define PID_AMBIENT_TEMP      0x46       // دمای محیط (°C)
#define PID_FUEL_TYPE         0x51       // نوع سوخت

/* --- Fuel Type codes --- */
#define FUEL_GASOLINE         0x01       // بنزین
#define FUEL_METHANOL         0x02       // متانول
#define FUEL_ETHANOL          0x03       // اتانول
#define FUEL_DIESEL           0x04       // گازوئیل
#define FUEL_LPG              0x05       // LPG
#define FUEL_CNG              0x06       // CNG
#define FUEL_ELECTRIC         0x07       // برقی
#define FUEL_HYBRID           0x08       // هیبرید


/* ========================================================================== */
/*   —— ساختار داده‌های OBD2 ——                                              */
/* ========================================================================== */
typedef struct {

  /* --- پارامترهای لحظه‌ای --- */
  uint16_t  engine_rpm;              // دور موتور (RPM)
  uint8_t   vehicle_speed;           // سرعت (km/h)
  uint8_t   engine_load;             // بار موتور (%)
  int8_t    coolant_temp;            // دمای مایع خنک‌کننده (°C)
  int8_t    intake_temp;             // دمای هوای ورودی (°C)
  uint8_t   throttle_pos;            // موقعیت دریچه گاز (%)
  uint8_t   fuel_level;              // سطح سوخت (%)
  int8_t    ambient_temp;            // دمای محیط (°C)
  uint8_t   fuel_type;               // نوع سوخت
  uint16_t  distance;                // مسافت طی‌شده (km)

  /* --- وضعیت --- */
  bool      valid_engine_rpm;        // آیا داده معتبر است؟
  bool      valid_speed;
  bool      valid_load;
  bool      valid_coolant;
  bool      valid_throttle;
  bool      valid_fuel_level;
  bool      valid_ambient_temp;

  /* --- PIDهای پشتیبانی‌شده (۴ بایت بیت‌مپ) --- */
  uint8_t   supported_pids[4];       // بیت‌ممپ PIDهای 01-20

} OBD2Data;


/* ========================================================================== */
/*   —— کلاس OBD2Diagnostic ——                                                */
/* ========================================================================== */
class OBD2Diagnostic {

  /* ---- اعضای خصوصی ---- */
  private:

    CANManager*   canBus;              // پوینتر به CAN Bus
    OBD2Data      data;                // داده‌های خوانده‌شده


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  سازنده — Constructor                                                */
    /* ===================================================================== */
    OBD2Diagnostic() {
      memset(&data, 0, sizeof(OBD2Data));
      canBus = nullptr;
    }


    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                              */
    /* ===================================================================== */
    void begin(CANManager* can) {
      canBus = can;

      /* ---- خواندن PIDهای پشتیبانی‌شده ---- */
      readSupportedPIDs();

      Serial.println(F("✅ [OBD2]  Diagnostics — راه‌اندازی شد"));

    } /* end of begin */


    /* ===================================================================== */
    /*  خواندن PIDهای پشتیبانی‌شده — readSupportedPIDs                       */
    /* ===================================================================== */
    void readSupportedPIDs() {

      if (!canBus) return;

      uint8_t request[] = {
        0x02,                        // PCI: 2 bytes
        OBD2_SID_READ_PIDS,          // SID: 0x01
        PID_SUPPORTED_01_20          // PID: 0x00
      };

      canBus->send(OBD2_REQ_ID, request, 3);

      /* ---- دریافت پاسخ ---- */
      CANFrame resp;
      uint32_t start = millis();

      while (millis() - start < 500) {
        if (canBus->receive(&resp, 50)) {
          if (resp.id == OBD2_RESP_ID && resp.dlc >= 6) {
            /* پاسخ: [PCI][SID+40][PID][b1][b2][b3][b4] */
            data.supported_pids[0] = resp.data[3];
            data.supported_pids[1] = resp.data[4];
            data.supported_pids[2] = resp.data[5];
            data.supported_pids[3] = resp.data[6];

            Serial.printf("📋 [OBD2]  PID bitmap: %02X %02X %02X %02X\n",
                          data.supported_pids[0], data.supported_pids[1],
                          data.supported_pids[2], data.supported_pids[3]);
            return;
          }
        }
      }

      Serial.println(F("⚠ [OBD2]  خواندن supported PIDs — بدون پاسخ"));

    } /* end of readSupportedPIDs */


    /* ===================================================================== */
    /*  خواندن یک PID — readPID                                             */
    /* --------------------------------------------------------------------- */
    /*  PID مورد نظر را می‌خواند و نتیجه را در struct ذخیره می‌کند.          */
    /* ===================================================================== */
    bool readPID(uint8_t pid) {

      if (!canBus) return false;

      /* ---- ارسال درخواست ---- */
      uint8_t request[] = {
        0x02,                        // PCI: 2 bytes
        OBD2_SID_READ_PIDS,          // SID: 0x01
        pid                          // PID مورد نظر
      };

      if (!canBus->send(OBD2_REQ_ID, request, 3)) {
        return false;
      }

      /* ---- دریافت پاسخ ---- */
      CANFrame resp;
      uint32_t start = millis();

      while (millis() - start < 500) {
        if (canBus->receive(&resp, 50)) {

          if (resp.id != OBD2_RESP_ID) continue;
          if (resp.dlc < 3)           continue;

          /* پاسخ باید با SID + 0x40 شروع شود */
          if (resp.data[1] != (OBD2_SID_READ_PIDS | 0x40)) {
            continue;
          }

          /* ---- پردازش PID ---- */
          switch (pid) {

            case PID_ENGINE_LOAD:            // PID 0x04: A*100/255 = %
              if (resp.dlc >= 4) {
                data.engine_load    = (resp.data[3] * 100) / 255;
                data.valid_load     = true;
              }
              break;

            case PID_COOLANT_TEMP:           // PID 0x05: A-40 = °C
              if (resp.dlc >= 4) {
                data.coolant_temp   = (int8_t)(resp.data[3] - 40);
                data.valid_coolant  = true;
              }
              break;

            case PID_ENGINE_RPM:             // PID 0x0C: ((A*256)+B)/4
              if (resp.dlc >= 5) {
                data.engine_rpm     = ((uint16_t)resp.data[3] << 8 |
                                       (uint16_t)resp.data[4]) / 4;
                data.valid_engine_rpm = true;
              }
              break;

            case PID_VEHICLE_SPEED:          // PID 0x0D: A = km/h
              if (resp.dlc >= 4) {
                data.vehicle_speed  = resp.data[3];
                data.valid_speed    = true;
              }
              break;

            case PID_INTAKE_TEMP:            // PID 0x0F: A-40 = °C
              if (resp.dlc >= 4) {
                data.intake_temp    = (int8_t)(resp.data[3] - 40);
              }
              break;

            case PID_THROTTLE_POS:           // PID 0x11: A*100/255 = %
              if (resp.dlc >= 4) {
                data.throttle_pos   = (resp.data[3] * 100) / 255;
                data.valid_throttle = true;
              }
              break;

            case PID_DISTANCE:               // PID 0x21: (A*256)+B = km
              if (resp.dlc >= 5) {
                data.distance       = ((uint16_t)resp.data[3] << 8 |
                                       (uint16_t)resp.data[4]);
              }
              break;

            case PID_FUEL_LEVEL:             // PID 0x2F: A*100/255 = %
              if (resp.dlc >= 4) {
                data.fuel_level     = (resp.data[3] * 100) / 255;
                data.valid_fuel_level = true;
              }
              break;

            case PID_AMBIENT_TEMP:           // PID 0x46: A-40 = °C
              if (resp.dlc >= 4) {
                data.ambient_temp   = (int8_t)(resp.data[3] - 40);
                data.valid_ambient_temp = true;
              }
              break;

            case PID_FUEL_TYPE:              // PID 0x51: A = fuel type
              if (resp.dlc >= 4) {
                data.fuel_type      = resp.data[3];
              }
              break;

            default:
              /* PID پشتیبانی‌نشده — نادیده گرفته شود */
              break;
          }

          return true;
        }
      }

      return false;  // Timeout

    } /* end of readPID */


    /* ===================================================================== */
    /*  خواندن همه پارامترها — pollAll                                       */
    /* --------------------------------------------------------------------- */
    /*  تمام PIDهای رایج را یکبار می‌خواند.                                   */
    /*  v5.2: فاصله بین هر PID کاهش یافته تا باس CAN شلوغ نشود.               */
    /* ===================================================================== */
    void pollAll() {

      if (!canBus) return;

      Serial.println(F("📊 [OBD2]  Polling parameters..."));

      /* ---- لیست PIDهای قابل خواندن ---- */
      const uint8_t pids[] = {
        PID_ENGINE_LOAD,
        PID_COOLANT_TEMP,
        PID_ENGINE_RPM,
        PID_VEHICLE_SPEED,
        PID_INTAKE_TEMP,
        PID_THROTTLE_POS,
        PID_DISTANCE,
        PID_FUEL_LEVEL,
        PID_AMBIENT_TEMP,
        PID_FUEL_TYPE
      };

      /* ---- خواندن یک‌یک ---- */
      for (int i = 0; i < (int)(sizeof(pids) / sizeof(pids[0])); i++) {
        readPID(pids[i]);
        delay(50);  // ✅ فاصله ۵۰ms بین هر PID — جلوگیری از شلوغی CAN
      }

      /* ---- نمایش نتایج ---- */
      printData();

    } /* end of pollAll */


    /* ===================================================================== */
    /*  نمایش داده‌ها — printData                                            */
    /* ===================================================================== */
    void printData() {

      Serial.println(F("┌────────────────────────────────────────────────────┐"));

      if (data.valid_engine_rpm)
        Serial.printf("│ 🔄  RPM              : %4d  rpm                │\n",
                      data.engine_rpm);
      if (data.valid_speed)
        Serial.printf("│ 🚗  Speed            : %3d   km/h              │\n",
                      data.vehicle_speed);
      if (data.valid_load)
        Serial.printf("│ ⚙️   Engine Load      : %3d   %%                 │\n",
                      data.engine_load);
      if (data.valid_coolant)
        Serial.printf("│ 🌡️  Coolant Temp     : %3d   °C                 │\n",
                      data.coolant_temp);
      if (data.valid_throttle)
        Serial.printf("│ 🚦  Throttle         : %3d   %%                 │\n",
                      data.throttle_pos);
      if (data.valid_fuel_level)
        Serial.printf("│ ⛽  Fuel Level       : %3d   %%                 │\n",
                      data.fuel_level);
      if (data.valid_ambient_temp)
        Serial.printf("│ 🌤️  Ambient Temp     : %3d   °C                 │\n",
                      data.ambient_temp);

      /* ---- نوع سوخت ---- */
      const char* fuelStr = "Unknown";
      switch (data.fuel_type) {
        case FUEL_GASOLINE: fuelStr = "بنزین";     break;
        case FUEL_METHANOL: fuelStr = "متانول";    break;
        case FUEL_ETHANOL:  fuelStr = "اتانول";    break;
        case FUEL_DIESEL:   fuelStr = "گازوئیل";   break;
        case FUEL_LPG:      fuelStr = "LPG";        break;
        case FUEL_CNG:      fuelStr = "CNG";        break;
        case FUEL_ELECTRIC: fuelStr = "برقی";      break;
        case FUEL_HYBRID:   fuelStr = "هیبرید";    break;
      }

      if (data.fuel_type > 0)
        Serial.printf("│ ⛽  Fuel Type        : %s                 │\n", fuelStr);

      Serial.println(F("└────────────────────────────────────────────────────┘"));

    } /* end of printData */


    /* ===================================================================== */
    /*  دسترسی به داده‌ها — getData                                          */
    /* ===================================================================== */
    const OBD2Data* getData() const {
      return &data;
    }


};  /* end of class OBD2Diagnostic */


#endif  /* OBD2_DIAG_H */
