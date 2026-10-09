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

## 📂 Projects Layout

```text
├── ESP32-RF-Lab/    # 2.4 GHz spectrum occupancy & link analysis (dual nRF24 + LVGL)
├── Mouse/           # USB HID Mouse Jiggler (prevent sleep / idle emulation)
└── Wifi-Wardriving/ # Wi-Fi scan and wardriving log in JSON (ESP32-S3)
```

### 1. `Mouse/` — USB HID Jiggler
* **What it does:** Acts as a plug-and-play USB device that periodically sends subtle, non-intrusive cursor movements to prevent the operating system from entering idle or sleep mode.
* **Tech stack:** ESP32-S3 Native USB capabilities (USB MSC/HID stack).

### 2. `Wifi-Wardriving/` — Wi-Fi Wardriving Logger
* **What it does:** Scans nearby Wi-Fi networks and saves an inventory in JSON format inside the ESP32-S3 flash memory.
* **Tech stack:** ESP-IDF, Wi-Fi scanning in STA mode, JSON serialization, and NVS persistence.

### 3. `ESP32-RF-Lab/` — 2.4 GHz Spectrum & Link Analyzer
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
