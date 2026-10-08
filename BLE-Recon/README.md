# 📶 BLE-Recon (ESP32-S3)

Bluetooth Low Energy reconnaissance scanner built on the **NimBLE** host stack — the BLE companion to `Wifi-Wardriving`.

> **Status:** work in progress — **M2** (iBeacon/Eddystone/FindMy parsing) implemented. See the full plan in [`docs/ROADMAP.md`](../docs/ROADMAP.md).

## 🔍 Current functionality

### Scanning (M1)
* Continuous asynchronous scanning (60 ms window, 100 % duty cycle) via NimBLE.
* 128-entry seen-address cache so the log only reports each device once.
* 30-second summary with the unique device count.
* Raw ADV payload hex dump at `DEBUG` log level.

### Beacon format decoding (M2, `main/ble_parser.c`)
* **iBeacon** — Apple manufacturer data `0x004C` / type `0x0215`: UUID, major, minor, calibrated TX power.
* **Eddystone** — service data `0xFEAA`:
  * `UID` frame: namespace + instance.
  * `URL` frame: URL scheme/suffix expansion into the full URL.
  * `TLM` frame: battery voltage, temperature (8.8 fixed-point), advertisement counter, uptime.
* **FindMy heuristic** — Apple manufacturer data type `0x10`/`0x12` (offline finding) flagged as a *candidate*; identifiers rotate, so they are not a stable fingerprint (honestly noted).
* Classification counters folded into the 30-second summary (`unicos | iBeacon | Eddystone | FindMy? | otros`).

## 🚀 Build and flash (ESP-IDF)

```bash
cd BLE-Recon
idf.py set-target esp32s3
idf.py build flash monitor
```

## Responsible Use Notice
Receive-only reconnaissance of the ambient BLE environment. Upcoming milestones add iBeacon/Eddystone/FindMy parsing, JSON persistence and a lab TX self-test mode. Flooding or spoofing third-party devices is out of scope by design.
