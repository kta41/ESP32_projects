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
├── mouse/          # USB HID Mouse Jiggler (prevent sleep / idle emulation)
└── Wifi-wardriving/ # Escaneo Wi-Fi y registro wardriving en JSON (ESP32-S3)
```

### 1. `mouse/` — USB HID Jiggler
* **What it does:** Acts as a plug-and-play USB device that periodically sends subtle, non-intrusive cursor movements to prevent the operating system from entering idle or sleep mode.
* **Tech stack:** ESP32-S3 Native USB capabilities (USB MSC/HID stack).

### 2. `Wifi-wardriving/` — Wi-Fi Wardriving Logger
* **What it does:** Escanea redes Wi-Fi cercanas y guarda un inventario en formato JSON dentro de la memoria flash del ESP32-S3.
* **Tech stack:** ESP-IDF, escaneo Wi-Fi en modo STA, serialización JSON y persistencia en NVS.

---

## 🛠️ Requirements & Tooling

* **Framework:** ESP-IDF / Arduino IDE with ESP32 board support packages.
* **Hardware:** ESP32-S3 DevKitC-1 or compatible development boards equipped with native USB support and external sensor/RF modules.

## 🚀 Getting Started

1. Clone the repository and navigate to the desired project directory:
   ```bash
   git clone [https://github.com/tu-usuario/nombre-del-repo.git](https://github.com/tu-usuario/nombre-del-repo.git)
   cd nombre-del-repo/mouse
   ```
2. Build and flash the firmware to your ESP32-S3 board using your preferred toolchain (ESP-IDF or Arduino IDE).
