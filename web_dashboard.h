/******************************************************************************
 * web_dashboard.h — داشبورد تحت وب CarHack-ESP32
 * ============================================================================
 * توضیحات:
 *   ارائه یک داشبورد وب (WebSocket + HTTP) برای نمایش لحظه‌ای داده‌های
 *   CAN Bus, OBD2, RF, و وضعیت سیستم.
 *
 * تغییرات v5.2:
 *   ✅ اصلاح JSON — حذف trailing comma (استفاده از ArduinoJson serializeJson)
 *   ✅ اضافه شدن bradcastRF برای نمایش داده‌های RF
 *   ✅ بهبود broadcastStatus با JSON استاندارد
 ******************************************************************************/

#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "can_manager.h"


/* ========================================================================== */
/*   —— ثابت‌های Web Dashboard ——                                            */
/* ========================================================================== */

#define WS_PORT           81         // پورت WebSocket
#define HTTP_PORT         80         // پورت HTTP
#define MAX_WS_CLIENTS    4          // حداکثر کلاینت‌های همزمان


/* ========================================================================== */
/*   —— کلاس WebDashboard ——                                                  */
/* ========================================================================== */
class WebDashboard {

  /* ---- اعضای خصوصی ---- */
  private:

    WebServer           httpServer;        // سرور HTTP
    WebSocketsServer    webSocket;         // سرور WebSocket
    bool                initialized = false;


    /* ===================================================================== */
    /*  صفحه HTML داخلی — serveDashboard                                     */
    /* ===================================================================== */
    void serveDashboard() {

      String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>CarHack-ESP32 Dashboard</title>
  <style>
    /* ====== استایل کلی ====== */
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: #0a0e17;
      color: #e0e0e0;
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 1200px;
      margin: 0 auto;
    }

    /* ====== هدر ====== */
    .header {
      background: linear-gradient(135deg, #1a1f2e 0%, #0d1b2a 100%);
      padding: 20px 30px;
      border-radius: 12px;
      margin-bottom: 25px;
      border: 1px solid #2a3a5c;
      text-align: center;
    }
    .header h1 {
      color: #00d4ff;
      font-size: 28px;
      letter-spacing: 1px;
    }
    .header .subtitle {
      color: #8899aa;
      font-size: 14px;
      margin-top: 5px;
    }
    .header .status-badge {
      display: inline-block;
      margin-top: 10px;
      padding: 4px 16px;
      border-radius: 20px;
      font-size: 12px;
      font-weight: bold;
    }
    .badge-connected { background: #00c853; color: #fff; }
    .badge-disconnected { background: #ff1744; color: #fff; }

    /* =====现 Grid کارت‌ها ====== */
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
      gap: 20px;
      margin-bottom: 25px;
    }
    .card {
      background: #111827;
      border-radius: 12px;
      padding: 20px;
      border: 1px solid #1e2d4a;
      transition: border-color 0.3s;
    }
    .card:hover { border-color: #00d4ff; }
    .card-title {
      color: #00d4ff;
      font-size: 16px;
      font-weight: bold;
      margin-bottom: 15px;
      padding-bottom: 8px;
      border-bottom: 1px solid #1e2d4a;
    }
    .card-value {
      font-size: 32px;
      font-weight: bold;
      color: #fff;
    }
    .card-label {
      font-size: 12px;
      color: #667788;
      margin-top: 4px;
    }

    /* ====== CAN Log ====== */
    .log-area {
      background: #0a0e17;
      border: 1px solid #1e2d4a;
      border-radius: 8px;
      padding: 15px;
      max-height: 300px;
      overflow-y: auto;
      font-family: 'Courier New', monospace;
      font-size: 12px;
      line-height: 1.6;
      direction: ltr;
      text-align: left;
    }
    .log-area .log-entry {
      color: #aabbcc;
      padding: 2px 0;
      border-bottom: 1px solid #151e30;
    }
    .log-area .log-entry .id { color: #ffab00; }
    .log-area .log-entry .data { color: #69f0ae; }

    /* ====== RF Section ====== */
    .rf-section {
      display: flex;
      gap: 10px;
      margin-top: 10px;
      flex-wrap: wrap;
    }
    .rf-btn {
      padding: 8px 20px;
      border: none;
      border-radius: 6px;
      background: #1a2a4a;
      color: #00d4ff;
      cursor: pointer;
      font-size: 13px;
      transition: all 0.3s;
    }
    .rf-btn:hover { background: #00d4ff; color: #0a0e17; }

    /* ====== scrollbar ====== */
    ::-webkit-scrollbar { width: 6px; }
    ::-webkit-scrollbar-track { background: #0a0e17; }
    ::-webkit-scrollbar-thumb { background: #2a3a5c; border-radius: 3px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🚗 CarHack-ESP32</h1>
      <div class="subtitle">Automotive Security Assessment Dashboard</div>
      <div id="statusBadge" class="status-badge badge-disconnected">⚡ Disconnected</div>
    </div>

    <div class="grid">
      <div class="card">
        <div class="card-title">🔌 CAN Bus</div>
        <div id="canStatus" class="card-value">---</div>
        <div class="card-label">فریم در ثانیه</div>
      </div>
      <div class="card">
        <div class="card-title">🚙 خودرو</div>
        <div id="vehicleName" class="card-value">---</div>
        <div class="card-label" id="vehicleVin">VIN: ---</div>
      </div>
      <div class="card">
        <div class="card-title">📻 RF Signal</div>
        <div id="rfRssi" class="card-value">---</div>
        <div class="card-label">RSSI (dBm)</div>
      </div>
      <div class="card">
        <div class="card-title">⚡ وضعیت</div>
        <div id="sysStatus" class="card-value">✅</div>
        <div class="card-label" id="sysVersion">v5.2</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">📡 CAN Log</div>
      <div id="canLog" class="log-area"></div>
    </div>

    <div class="card">
      <div class="card-title">📻 RF Control</div>
      <div class="rf-section">
        <button class="rf-btn" onclick="sendCmd('scan_rf')">🔍 اسکان فرکانس</button>
        <button class="rf-btn" onclick="sendCmd('capture_rf')">📥 کپچر RF</button>
        <button class="rf-btn" onclick="sendCmd('status')">📊 وضعیت</button>
        <button class="rf-btn" onclick="sendCmd('restart')">🔄 ری‌استارت</button>
      </div>
      <div id="rfOutput" class="log-area" style="margin-top: 15px; max-height: 150px;">
        پیام‌های RF در اینجا نمایش داده می‌شوند...
      </div>
    </div>
  </div>

  <script>
    /* ---- WebSocket Connection ---- */
    const ws = new WebSocket(`ws://${location.hostname}:81/`);

    ws.onopen = () => {
      document.getElementById('statusBadge').className = 'status-badge badge-connected';
      document.getElementById('statusBadge').textContent = '✅ Connected';
    };

    ws.onclose = () => {
      document.getElementById('statusBadge').className = 'status-badge badge-disconnected';
      document.getElementById('statusBadge').textContent = '⚡ Disconnected';
      setTimeout(() => window.location.reload(), 3000);
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);

        switch (msg.type) {

          case 'status':
            if (msg.key === 'vehicle') {
              document.getElementById('vehicleName').textContent = msg.value;
            } else if (msg.key === 'vin') {
              document.getElementById('vehicleVin').textContent = 'VIN: ' + msg.value;
            } else if (msg.key === 'system') {
              document.getElementById('sysVersion').textContent = msg.value;
            } else if (msg.key === 'can_fps') {
              document.getElementById('canStatus').textContent = msg.value;
            }
            break;

          case 'can_frame':
            const logDiv = document.getElementById('canLog');
            const entry = document.createElement('div');
            entry.className = 'log-entry';
            entry.innerHTML = `<span class="id">0x${msg.id.toString(16).padStart(3, '0').toUpperCase()}</span>  ` +
                              `<span class="data">${msg.data}</span>`;
            logDiv.appendChild(entry);
            if (logDiv.children.length > 100) {
              logDiv.removeChild(logDiv.firstChild);
            }
            logDiv.scrollTop = logDiv.scrollHeight;
            break;

          case 'rf_signal':
            document.getElementById('rfRssi').textContent = msg.rssi + ' dBm';
            const rfDiv = document.getElementById('rfOutput');
            rfDiv.innerHTML = `📻 بسته ${msg.len} بایتی دریافتی در ${new Date().toLocaleTimeString()}`;
            break;

          case 'rf_result':
            document.getElementById('rfOutput').innerHTML = msg.message;
            break;
        }
      } catch (e) {
        console.error('JSON parse error:', e);
      }
    };

    function sendCmd(cmd) {
      ws.send(JSON.stringify({ command: cmd }));
    }
  </script>
</body>
</html>
)rawliteral";

      httpServer.send(200, "text/html; charset=utf-8", html);

    } /* end of serveDashboard */


    /* ===================================================================== */
  /*  هندلر WebSocket — handleWebSocket                                   */
    /* ===================================================================== */
    void handleWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {

      switch (type) {

        case WStype_CONNECTED:
          Serial.printf("🌐 [WEB]  کلاینت جدید متصل شد — ID: %d\n", num);
          webSocket.sendTXT(num, "{\"type\":\"status\",\"key\":\"system\",\"value\":\"CarHack-ESP32 v5.2\"}");
          break;

        case WStype_DISCONNECTED:
          Serial.printf("🌐 [WEB]  کلاینت قطع شد — ID: %d\n", num);
          break;

        case WStype_TEXT: {
          /* ---- پردازش JSON دریافتی از کلاینت ---- */
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, payload, length);

          if (err) {
            Serial.println(F("⚠ [WEB]  JSON نامعتبر از کلاینت دریافت شد"));
            break;
          }

          const char* command = doc["command"];
          if (command) {
            Serial.printf("📨 [WEB]  فرمان دریافت شد: %s\n", command);
            /* فرمان‌ها در main loop پردازش می‌شوند */
          }

          break;
        }

        default:
          break;
      }

    } /* end of handleWebSocket */


  /* ---- اعضای عمومی ---- */
  public:

    /* =================================================================== */
    /*  سازنده — Constructor                                              */
    /* =================================================================== */
    WebDashboard() : httpServer(HTTP_PORT), webSocket(WS_PORT) {}

    /* =================================================================== */
    /*  مقداردهی اولیه — begin                                            */
    /* =================================================================== */
    void begin() {

      /* ---- راه‌اندازی WebSocket ---- */
      webSocket.begin();
      webSocket.onEvent([this](uint8_t num, WStype_t type,
                                uint8_t* payload, size_t length) {
        this->handleWebSocket(num, type, payload, length);
      });

      /* ---- راه‌اندازی HTTP server ---- */
      httpServer.on("/", [this]() { this->serveDashboard(); });
      httpServer.begin();

      initialized = true;

      Serial.println(F("✅ [WEB]  Dashboard ready on ports 80 (HTTP) & 81 (WS)"));

    } /* end of begin */


    /* =================================================================== */
    /*  حلقه اصلی — loop (برای WebSocket)                                 */
    /* =================================================================== */
    void loop() {
      if (!initialized) return;
      webSocket.loop();
      httpServer.handleClient();
    }


    /* =================================================================== */
    /*  broadcastStatus — ارسال وضعیت به همه کلاینت‌ها                     */
    /* ------------------------------------------------------------------ */
    /*  بدون trailing comma — با کتابخانه ArduinoJson                     */
    /* =================================================================== */
    void broadcastStatus(const char* key, const char* value) {

      if (!initialized || webSocket.count() == 0) return;

      DynamicJsonDocument doc(256);

      doc["type"]  = "status";
      doc["key"]   = key;
      doc["value"] = value;

      String json;
      serializeJson(doc, json);   // ✅ استاندارد — بدون trailing comma

      webSocket.broadcastTXT(json);

    } /* end of broadcastStatus */


    /* =================================================================== */
    /*  broadcastCAN — ارسال فریم CAN                                      */
    /* =================================================================== */
    void broadcastCAN(const CANFrame& frame) {

      if (!initialized || webSocket.count() == 0) return;

      DynamicJsonDocument doc(256);

      doc["type"] = "can_frame";
      doc["id"]   = frame.id;
      doc["dlc"]  = frame.dlc;

      String dataStr = "";
      for (int i = 0; i < frame.dlc; i++) {
        if (i > 0) dataStr += " ";
        char hex[4];
        sprintf(hex, "%02X", frame.data[i]);
        dataStr += hex;
      }
      doc["data"] = dataStr;

      String json;
      serializeJson(doc, json);

      webSocket.broadcastTXT(json);

    } /* end of broadcastCAN */


    /* =================================================================== */
    /*  broadcastRF — ارسال داده‌های RF                                    */
    /* =================================================================== */
    void broadcastRF(const uint8_t* data, uint8_t len, int rssi) {

      if (!initialized || webSocket.count() == 0) return;

      DynamicJsonDocument doc(256);

      doc["type"] = "rf_signal";
      doc["len"]  = len;
      doc["rssi"] = rssi;

      String dataStr = "";
      for (int i = 0; i < len; i++) {
        if (i > 0) dataStr += " ";
        char hex[4];
        sprintf(hex, "%02X", data[i]);
        dataStr += hex;
      }
      doc["data"] = dataStr;

      String json;
      serializeJson(doc, json);

      webSocket.broadcastTXT(json);

    } /* end of broadcastRF */


    /* =================================================================== */
    /*  broadcastRFResult — ارسال نتیجه عملیات RF                          */
    /* =================================================================== */
    void broadcastRFResult(const char* message) {

      if (!initialized || webSocket.count() == 0) return;

      DynamicJsonDocument doc(128);
      doc["type"]    = "rf_result";
      doc["message"] = message;

      String json;
      serializeJson(doc, json);

      webSocket.broadcastTXT(json);

    } /* end of broadcastRFResult */


};  /* end of class WebDashboard */


#endif  /* WEB_DASHBOARD_H */
