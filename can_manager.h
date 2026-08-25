/******************************************************************************
 * can_manager.h — مدیریت CAN Bus با TWAI Driver (ESP32)
 * ============================================================================
 * توضیحات:
 *   یکپارچه‌سازی با TWAI (Two-Wire Automotive Interface) ESP-IDF برای
 *   ارسال و دریافت فریم‌های CAN 2.0.
 *
 * تغییرات v5.2:
 *   ✅ بدون تغییر — کد از ابتدا بر اساس مستندات Espressif نوشته شده
 ******************************************************************************/

#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <Arduino.h>
#include <driver/twai.h>


/* ========================================================================== */
/*   —— ساختار CANFrame ——                                                    */
/* ========================================================================== */
typedef struct {
  uint32_t  id;               // شناسه CAN (11-bit standard)
  uint8_t   dlc;              // طول داده (0-8)
  uint8_t   data[8];          // داده
  bool      isExtended;       // true اگر 29-bit extended
} CANFrame;


/* ========================================================================== */
/*   —— کلاس CANManager ——                                                    */
/* ========================================================================== */
class CANManager {

  /* ---- اعضای خصوصی ---- */
  private:

    bool  initialized = false;
    int   tx_pin, rx_pin;


  /* ---- اعضای عمومی ---- */
  public:

    /* ===================================================================== */
    /*  مقداردهی اولیه — begin                                              */
    /* ===================================================================== */
    bool begin(gpio_num_t tx, gpio_num_t rx, twai_speed_t speed = TWAI_SPEED_500KBPS) {

      tx_pin = tx;
      rx_pin = rx;

      /* ---- تنظیمات TWAI ---- */
      twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, TWAI_MODE_NORMAL);
      twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_500KBITS();
      twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

      /* ---- نصب درایور ---- */
      if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println(F("❌ [CAN]  نصب TWAI driver FAILED"));
        return false;
      }

      /* ---- شروع درایور ---- */
      if (twai_start() != ESP_OK) {
        Serial.println(F("❌ [CAN]  شروع TWAI driver FAILED"));
        return false;
      }

      initialized = true;
      return true;

    } /* end of begin */


    /* ===================================================================== */
    /*  ارسال فریم — send                                                   */
    /* ===================================================================== */
    bool send(uint32_t id, const uint8_t* data, uint8_t dlc, bool extended = false) {

      if (!initialized) return false;
      if (dlc > 8)      return false;

      twai_message_t msg;
      msg.identifier  = id;
      msg.extd        = extended ? 1 : 0;
      msg.rtr         = 0;
      msg.data_length_code = dlc;
      memcpy(msg.data, data, dlc);

      esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
      return (err == ESP_OK);

    } /* end of send */


    /* ===================================================================== */
    /*  دریافت فریم — receive                                               */
    /* ===================================================================== */
    bool receive(CANFrame* out, uint32_t timeout_ms = 10) {

      if (!initialized) return false;

      twai_message_t msg;
      esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(timeout_ms));

      if (err != ESP_OK) return false;

      out->id         = msg.identifier;
      out->dlc        = msg.data_length_code;
      out->isExtended = msg.extd;
      memcpy(out->data, msg.data, out->dlc);

      return true;

    } /* end of receive */


    /* ===================================================================== */
    /*  توابع دسترسی                                                        */
    /* ===================================================================== */

    bool  isInitialized() { return initialized; }


};  /* end of class CANManager */


#endif  /* CAN_MANAGER_H */
