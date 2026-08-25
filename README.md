<p align="center">
  <img src="assets/logo.png" alt="CarHack-ESP32 Logo" width="200"/>
</p>

<h1 align="center">
  🚗 CarHack-ESP32 — v5.2
</h1>

<p align="center">
  <strong>سیستم ارزیابی امنیتی خودروهای مدرن</strong>
  <br>
  <em>AI-Powered Automotive Security Assessment Platform</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-Platform-blue?logo=espressif&style=flat-square"/>
  <img src="https://img.shields.io/badge/Version-5.2-brightgreen?style=flat-square"/>
  <img src="https://img.shields.io/badge/Protocol-CAN%20Bus%20|%20ISO--TP%20|%20UDS%20|%20OBD2-orange?style=flat-square"/>
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=flat-square"/>
  <br>
  <img src="https://img.shields.io/badge/RF-433%20MHz%20|%20CC1101-red?style=flat-square"/>
  <img src="https://img.shields.io/badge/BLE-Support-purple?style=flat-square"/>
  <img src="https://img.shields.io/badge/Web-Dashboard-00d4ff?style=flat-square"/>
</p>

<hr>

<br>

# 📋 فهرست مطالب

| بخش | توضیح |
|------|--------|
| [🌟 معرفی پروژه](#معرفی-پروژه) | درباره CarHack-ESP32 |
| [⚡ قابلیت‌های اصلی](#قابلیت‌های-اصلی) | ویژگی‌های فنی |
| [📦 سخت‌افزار مورد نیاز](#سختافزار-مورد-نیاز) | لیست قطعات |
| [🔌 پین‌اتصال](#پیناتصال) | نقشه اتصالات |
| [🛠️ نصب و راه‌اندازی](#نصب-و-راهاندازی) | از صفر تا صد |
| [📡 پروتکل‌های پشتیبانی‌شده](#پروتکلهای-پشتیبانیشده) | CAN, ISO-TP, UDS, OBD2, RF |
| [🌐 Web Dashboard](#web-dashboard) | داشبورد تحت وب |
| [📊 CAN Learner](#can-learner) | یادگیری خودکار CAN |
| [🔄 RollJam](#rolljam) | تکنیک RF RollJam |
| [🔧 عیب‌یابی](#عیبیابی) | مشکلات رایج و راه‌حل‌ها |
| [📜 تغییرات نسخه‌ها](#تغییرات-نسخهها) | تاریخچه نسخه‌ها |
| [🤝 مشارکت](#مشارکت) | چگونه مشارکت کنیم |
| [⚖️ مجوز](#مجوز) | لایسنس |

<br>

---

## 🌟 معرفی پروژه

**CarHack-ESP32** یک پلتفرم قدرتمند و متن‌باز برای **ارزیابی امنیتی خودروهای مدرن** است. این پروژه با استفاده از میکروکنترلر ESP32، ارتباط با **CAN Bus** خودرو، پروتکل‌های **ISO-TP** و **UDS**، خواندن پارامترهای **OBD2**، و تحلیل سیگنال‌های **RF** را ممکن می‌سازد.

### 🎯 اهداف پروژه

- 🔍 **شناسایی آسیب‌پذیری‌ها** در شبکه CAN خودرو
- 📡 **شنود و تحلیل** فریم‌های CAN به صورت real-time
- 🛡️ **تست نفوذ** ماژول‌های RF (کلیدهای هوشمند)
- 📊 **داشبورد تحت وب** برای نمایش لحظه‌ای داده‌ها
- 🧠 **یادگیری خودکار** الگوهای CAN Bus

> ⚠️ **توجه:** این پروژه فقط برای **اهداف آموزشی و امنیتی** در محیط‌های مجاز و قانونی طراحی شده است. استفاده از آن بر روی خودروی شخصی یا دارای مجوز کتبی توصیه می‌شود.

<br>

---

## ⚡ قابلیت‌های اصلی

