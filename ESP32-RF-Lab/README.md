# 📶 ESP32-RF-Lab (ESP32-S3)

A portable ESP-IDF platform for **passive 2.4 GHz spectrum-occupancy analysis and link-quality characterization**, built around two nRF24L01+ transceivers, a 1.54" IPS ST7789 240x240 display driven by LVGL, and a four-button keypad. The device is **receive-only**: all measurement modes listen to the band and never inject frames or carriers into it.

---

## 🔍 Measurement Modes

Eight analysis profiles selectable from the on-screen roller (`OK` starts, `OK` again toggles off, `BACK` is a hardware stop):

* `SWEEP` — Both radios hop pseudo-randomly across the full 80-channel band (2400–2479 MHz).
* `FAST SWEEP` — Radio 1 hops without PLL settle (broadband energy estimate) while radio 2 dwells for stable per-channel readings.
* `ADV+SWEEP` — Radio 1 monitors the three BLE advertising channels while radio 2 sweeps the full band.
* `INTERLEAVED` — Alternates advertising-channel focus and full-band sampling between radios.
* `BLE ALL` — Sequential walk across the 40 BLE data channels.
* `BLE ADV` — Continuous focus on the BLE advertising channels (2, 26, 80).
* `BLE RANDOM` — Pseudo-random sampling within the BLE channel table.
* `LINK/PER` — Point-to-point packet-error-rate test between the two onboard radios (numbered payloads, rotating channel; standard-compliant traffic exchanged only between the lab's own two modules).

### Occupancy metrics

* `RPD` (Received Power Detector, register `0x09`) — Latches high whenever received power exceeds **≈ -64 dBm** in RX mode; used as a per-channel energy/occupancy indicator.
* `frames_seen` — Environment packets that pass the 2-byte CRC, i.e. real traffic observed on air.
* Periodic report — Every 10,000 samples the engine logs total samples, peak channel and its occupancy percentage for each radio.

---

## 🧩 Hardware Architecture

Shared **SPI bus (`SPI2_HOST`)** topology: the display, the MicroSD reader and both radios share SCK/MOSI/MISO, isolated by dedicated chip-select lines. All pin assignments are centralized in `main/hw_config.h`.

### Shared SPI highway
| Signal | ESP32-S3 Pin | Wired to |
| :--- | :--- | :--- |
| **SCK** | `GPIO 12` | Display `CK`, radios `SCK`, SD `SCK` |
| **MOSI** | `GPIO 11` | Display `SI`, radios `MOSI`, SD `MOSI` |
| **MISO** | `GPIO 13` | Display `SO`, radios `MISO`, SD `MISO` |

### Peripherals
| Peripheral | Control pins | Notes |
| :--- | :--- | :--- |
| **ST7789 240x240 IPS** | `TFT_CS` GPIO 15, `DC` GPIO 17, `RST` GPIO 16 | Backlight tied to 3V3; LVGL via `esp_lvgl_port` |
| **MicroSD reader (rear)** | `SD_CS` GPIO 14 | Forced `HIGH` at boot — a floating CS corrupts the MISO line |
| **nRF24L01+ Radio 1** | `CE` GPIO 9, `CSN` GPIO 10 | Primary scanner |
| **nRF24L01+ Radio 2** | `CE` GPIO 47, `CSN` GPIO 21 | Secondary scanner |

### User inputs (internal pull-ups, active low)
| Button | GPIO |
| :--- | :--- |
| `UP` / `DOWN` | `GPIO 1` / `GPIO 2` |
| `OK` / `BACK` | `GPIO 19` / `GPIO 20` |

---

## ⚙️ Firmware Architecture

Boot sequence orchestrated in `main.c`:

1. **Preventive CS lockdown** — `SD_CS`, `CSN_1`, `CSN_2` forced `HIGH` on the very first lines to keep slaves off the bus.
2. **Single SPI bus initialization** (`SPI2_HOST`, auto DMA channel).
3. **Display bring-up** — ST7789 panel integrated with LVGL.
4. **Radio bring-up** — neutral standby configuration plus a diagnostics battery (`nrf24_diagnostics.c`): SPI register round-trip, 100-packet transfers in both directions, and a multi-channel sweep.
5. **Mode queue + survey engine** — a FreeRTOS queue (`mode_queue`) feeds `rf_task`, pinned to **Core 1** at maximum priority; LVGL and the keypad run on Core 0.

> **Behavioral Note:** selecting the same profile twice toggles the measurement off, and the `BACK` key invokes `survey_stop()` directly from the keypad driver, providing a stop path independent of the UI state.

---

## 🚀 Configuration and Compilation (ESP-IDF)

### 1. Dependencies and Registration (`main/CMakeLists.txt`)
```cmake
idf_component_register(SRCS "nrf24_diagnostics.c" "main.c" "radios_nrf24.c" "rf_survey_engine.c" "display_st7789.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver nvs_flash)
```

> **Dependency Note:** the UI stack is pulled in as managed components through `main/idf_component.yml` (`lvgl/lvgl >=8.3.0`, `espressif/esp_lvgl_port ^2.0`), and a custom partition table (`partitions.csv`, factory app `0x180000`) is selected in the top-level `CMakeLists.txt`.

### 2. Build and flash

```bash
cd ESP32-RF-Lab
idf.py set-target esp32s3
idf.py build flash monitor
```

## Responsible Use Notice
This platform is **receive-only**: it listens to the 2.4 GHz ISM band and exchanges standard-compliant packets exclusively between its own two radios. It does not transmit interference and does not inject frames into third-party networks. Use it for channel planning, interference hunting, EMC education and link characterization. For the academic background on why active radio interference is deliberately out of scope, see [`docs/THEORY.md`](docs/THEORY.md).
