<div align="center">

# ⚡ FearverkDPI

**High-Performance Native DPI Bypass & Real-Time TUI Network Engine for Windows**

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg?style=for-the-badge&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011-0078D6.svg?style=for-the-badge&logo=windows)](https://microsoft.com)
[![Driver](https://img.shields.io/badge/Driver-WinDivert%202.2%2B-red.svg?style=for-the-badge)](https://reqcrypt.org/windivert.html)
[![License](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)
[![Speed](https://img.shields.io/badge/Speed-100%25%20Direct%20Line-brightgreen.svg?style=for-the-badge)](#)

[English](#-english) • [Русский](#-русский)
 
</div>

---

## 🌐 English

### 📖 Overview
**FearverkDPI** is a native, low-latency DPI circumvention engine written in modern C++ utilizing the `WinDivert` kernel architecture. Unlike conventional VPNs, FearverkDPI **does not** route your traffic through remote relays. It manipulates local raw packet streams on the fly by fragmenting TLS SNI buffers, dropping HTTP/3 QUIC (UDP 443) streams, and injecting desynchronized low-TTL fake TCP packets.

### Don't forget about our site! https://furrywex.github.io/fearverkdpi

### ✨ Key Features
* 🚀 **Zero Latency & 100% Bandwidth:** Pure ISP wire speed without proxy latency or bandwidth throttling.
* 🎬 **Instant 4K YouTube Playback:** Drops UDP 443 packets to seamlessly force browsers onto fragmented TCP.
* 🛡️ **Precise TLS Desynchronization:** Deep byte-level SNI parser with middle-split segmentation and low-TTL fake injection.
* 🎮 **Full Voice & Gaming Compatibility:** Eliminates voice issues in Discord, fixes Roblox Studio Team Create disconnects, and unblocks restricted web endpoints.
* 📊 **Interactive TUI Dashboard:** Real-time terminal UI displaying live gateway ping, domain hit counters, and packet modification telemetry.

---

### 📂 Directory Structure

```text
FearverkDPI/
├── bin/
│   └── FearverkDPI.exe       # Compiled binary
├── libs/
│   ├── windivert.h           # Header files
│   ├── WinDivert.lib         # Static library
│   ├── WinDivert.dll         # User-mode library
│   └── WinDivert64.sys       # Signed kernel driver
├── src/
│   └── FearverkDPI.cpp       # Core source code
├── general.bat               # Auto Mode (Fastest gateway selection)
├── general_jp.bat            # Japan profile preset
├── general_us.bat            # USA profile preset
└── general_de.bat            # Germany/EU profile preset
```

---

### 🚀 Quick Start

1. Download the latest precompiled release archive from the **Releases** page.
2. Extract the archive into any folder.
3. Right-click **`general.bat`** and select **Run as Administrator**.
4. Launch your browser, Discord, or Roblox Studio — all restricted endpoints will load instantly.

---

### 🛠️ Building from Source

Requirements: MinGW-w64 (GCC 10+) or Clang with C++17 support.

```bash
g++ -std=c++17 -O3 src/FearverkDPI.cpp -o bin/FearverkDPI.exe -Ilibs -Llibs -lWinDivert -lws2_32 -liphlpapi
```

---

### ⚙️ Command-Line Arguments

| Option | Parameter | Description | Default |
| :--- | :--- | :--- | :--- |
| `-c, --country` | `<CODE>` | Target gateway profile (`AUTO`, `JP`, `US`, `DE`, `NL`, `KR`) | `AUTO` |
| `--no-quic` | *None* | Disable automatic QUIC (UDP 443) packet dropping | `Enabled` |
| `--no-fake` | *None* | Disable fake TLS packet injection | `Enabled` |
| `--ttl` | `<VAL>` | Custom Time-To-Live for fake packets | `3` |

---

## 🇷🇺 Русский

### 📖 О проекте
**FearverkDPI** — это высокопроизводительный нативный инструмент для обхода DPI-блокировок провайдеров (ТСПУ), написанный на C++17 с использованием низкоуровневого драйвера `WinDivert`.

Программа **не использует чужие серверы** и не является классическим VPN. Весь процесс происходит локально на вашем компьютере: драйвер перехватывает исходящие пакеты перед отправкой в сетевую карту, делит `SNI` (имя сайта) на части, сбрасывает протокол QUIC и отправляет фейковые пакеты с заниженным TTL для ослепления сетевых фильтров.

### ✨ Главные преимущества
* 🚀 **Максимальная скорость провайдера:** Пинг в играх и звонках не увеличивается, скорость загрузки не режется.
* 🎬 **Мгновенный запуск YouTube в 4K:** Принудительный перевод браузеров с заблокированного QUIC (UDP 443) на оптимизированный TCP.
* 🛡️ **Глубокий парсинг TLS:** Нарезка пакета ClientHello точно посередине имени домена и инъекция защитных Fake-пакетов.
* 🎮 **Работает с играми и приложениями:** Полная работоспособность Discord (включая войс-каналы), Roblox Studio (Team Create) и веб-сайтов.
* 📊 **Интерактивный TUI-интерфейс:** Консольная панель с мониторингом пинга, счетчиком обработанных доменов и статистикой в реальном времени.

---

### 🚀 Быстрый запуск

1. Скачайте готовый архив в разделе **Releases**.
2. Распакуйте архив в любую папку.
3. Запустите файл **`general.bat`** (или профиль нужной страны) **от имени Администратора**.
4. Готово! YouTube, Discord и Roblox снова работают на полной скорости.

---
### Не забывайте о нашем сайте! https://furrywex.github.io/fearverkdpi
---

### 📜 Профили запуска (.bat)

| Файл | Описание профиля |
| :--- | :--- |
| `general.bat` | **Автоматический выбор**: автоматический замер пинга и выбор наилучшего маршрута |
| `general_jp.bat` | Профиль с ориентацией на шлюзы Азии и Японии |
| `general_us.bat` | Профиль с ориентацией на серверы США (Cloudflare Anycast) |
| `general_de.bat` | Профиль для европейских шлюзов (Франкфурт / Амстердам) |

---

### 🛠️ Сборка из исходного кода

Для самостоятельной компиляции через MinGW (GCC) выполните команду в корне проекта:

```bash
g++ -std=c++17 -O3 src/FearverkDPI.cpp -o bin/FearverkDPI.exe -Ilibs -Llibs -lWinDivert -lws2_32 -liphlpapi
```

---

### ⚠️ Важное примечание (Disclaimer)

Программе требуются права Администратора исключительно для загрузки подписанного системного драйвера `WinDivert.sys`. Исходный код полностью прозрачен, не собирает телеметрию и не взаимодействует со сторонними серверами.

---

<div align="center">

**Made with ⚡ for gamers, developers, and open web enthusiasts.**

</div>
