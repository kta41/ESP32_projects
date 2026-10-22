<div align="center">

# ESP32-S3 Hardware Lab ⚡
> *A collection of embedded firmware, sensor integrations, and wireless experimentation projects powered by ESP32-S3.*

<p align="center">
  <img alt="C++" src="https://img.shields.io/badge/C++-blue?style=flat-square&logo=cplusplus">
  <img alt="Platform: ESP32-S3" src="https://img.shields.io/badge/Platform-ESP32--S3-orange?style=flat-square&logo=espressif">
  <img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square">
</p>

</div>

---

A modular workspace for microcontroller experimentation, USB HID emulation, and wireless security research using ESP32-S3 development boards.

> 🛰️ **Central Intelligence & Analysis Hub:** All wireless telemetry captured by the sensors in this repository is ingested, correlated, and visualized in [**kta41/SIGINT-Console**](https://github.com/kta41/SIGINT-Console) — our Dockerized SIGINT web platform for multi-session tracking, IEEE OUI resolution, PNL client correlation, and security auditing.

## 📂 Projects Layout

```text
├── BLE-Recon/       # Standalone BLE passive reconnaissance & beacon parser (iBeacon / Eddystone / FindMy)
├── ESP32-RF-Lab/    # 2.4 GHz spectrum occupancy & link analysis (dual nRF24 + LVGL)
├── Mouse/           # USB HID Mouse Jiggler (prevent sleep / idle emulation)
└── Wifi-Wardriving/ # Multi-protocol 802.11 + BLE stealth sensor (PCAP/Radiotap, EAPOL, BLE fusion, HTTP NDJSON)
```

### 1. `Mouse/` — USB HID Jiggler
* **What it does:** Acts as a plug-and-play USB device that periodically sends subtle, non-intrusive cursor movements to prevent the operating system from entering idle or sleep mode.
* **Tech stack:** ESP32-S3 Native USB capabilities (USB MSC/HID stack).

### 2. `Wifi-Wardriving/` — Multi-Protocol 802.11 + BLE Stealth Sensor
* **What it does:** Connects as a stealth STA (per-session randomized MAC, 10 dBm TX power) to a mobile hotspot, performs passive 2.4 GHz channel sweeps + simultaneous **802.11 promiscuous sniffing** (associated clients, Probe Request PNLs, Deauth/Disassoc counters), captures selective **PCAP + Radiotap (`DLT 127`)** management frames and **WPA2/WPA3 EAPOL handshakes**, runs a concurrent **100 % passive NimBLE observer (`ble_fusion`)** for cross-protocol Wi-Fi ↔ BLE correlation, includes an opt-in token-gated **Red Team Deauth TX control channel (`udp/47472`)**, and streams NDJSON + PCAP batches to the mobile receiver (`warwifi`) and [**SIGINT-Console**](https://github.com/kta41/SIGINT-Console).
* **Tech stack:** ESP-IDF v6.1, FreeRTOS, Wi-Fi/BLE software coexistence, compact RAM Ring Buffer (`scan_store`), zero-DOM streaming JSONL serializer, Termux/Kali collector suite.

### 3. `BLE-Recon/` — Bluetooth Low Energy Passive Reconnaissance
* **What it does:** Standalone continuous NimBLE passive scanner with protocol decoders for Apple Continuity / FindMy / AirTag, iBeacon, and Eddystone frames (also integrated directly into `Wifi-Wardriving` via `ble_fusion`).

### 4. `ESP32-RF-Lab/` — 2.4 GHz Spectrum & Link Analyzer
* **What it does:** Passive spectrum-occupancy scanner with dual nRF24L01+ receivers, an on-device LVGL UI, and a point-to-point packet-error-rate test between its own radios.
* **Tech stack:** ESP-IDF, LVGL (`esp_lvgl_port`), shared SPI bus (`SPI2_HOST`), RPD-based energy detection.

---

## 🛠️ Hardware Pinout & Connections (LCD 1602 Reference)

| Component | Module Pin | Function | ESP32-S3 Pin / Power |
| :--- | :--- | :--- | :--- |
| **LCD 1602** | `Pin 1 (VSS)` | Logic Ground | `GND` |
| | `Pin 2 (VDD)` | Power Supply | `3V3` |
| | `Pin 3 (V0)` | Contrast | `GND` |
| | `Pin 4 (RS)` | Register Select | **GPIO 5** |
| | `Pin 5 (RW)` | R/W Mode | `GND` (Write) |
| | `Pin 6 (E)` | Enable / Clock | **GPIO 6** |
| | `Pin 11 (D4)`| Data Bus D4 | **GPIO 7** |
| | `Pin 12 (D5)`| Data Bus D5 | **GPIO 15** |
| | `Pin 13 (D6)`| Data Bus D6 | **GPIO 16** |
| | `Pin 14 (D7)`| Data Bus D7 | **GPIO 17** |
| | `Pin 15 (A)` | Backlight Anode | `3V3` |
| | `Pin 16 (K)` | Backlight Cathode | `GND` |
| **Pushbutton** | Terminal 1 | Digital Input | **GPIO 4** (Internal Pull-Up enabled) |
| | Terminal 2 | Return | `GND` |

---

## 🛠️ Requirements & Tooling

* **Framework:** ESP-IDF / Arduino IDE with ESP32 board support packages.
* **Hardware:** ESP32-S3 DevKitC-1 or compatible development boards equipped with native USB support and external sensor/RF modules.

## 🚀 Getting Started

1. Clone the repository and navigate to the desired project directory:
   ```bash
   git clone https://github.com/kta41/ESP32_projects.git
   cd <project>
   ```
2. Build and flash the firmware to your ESP32-S3 board using your preferred toolchain (ESP-IDF or Arduino IDE).
