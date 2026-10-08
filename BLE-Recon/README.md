# 📶 BLE-Recon (ESP32-S3)

Bluetooth Low Energy reconnaissance scanner built on the **NimBLE** host stack — the BLE companion to `Wifi-Wardriving`.

> **Status:** work in progress — **M1** (continuous scan → serial log) implemented. See the full plan in [`docs/ROADMAP.md`](../docs/ROADMAP.md).

## 🔍 Current functionality (M1)

* Continuous asynchronous scanning (60 ms window, 100 % duty cycle) via NimBLE.
* Per-device serial log: address, address type, advertisement type, RSSI, payload length and local name (from ADV data / SCAN_RSP).
* 128-entry seen-address cache so the log only reports each device once (first sighting).
* 30-second summary with the unique device count.
* Raw ADV payload hex dump at `DEBUG` log level.

## 🚀 Build and flash (ESP-IDF)

```bash
cd BLE-Recon
idf.py set-target esp32s3
idf.py build flash monitor
```

## Responsible Use Notice
Receive-only reconnaissance of the ambient BLE environment. Upcoming milestones add iBeacon/Eddystone/FindMy parsing, JSON persistence and a lab TX self-test mode. Flooding or spoofing third-party devices is out of scope by design.
