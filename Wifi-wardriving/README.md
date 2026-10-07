# 📡 Wi-Fi Wardriving (ESP32-S3)

An ESP-IDF integrated application for **nearby wireless network reconnaissance** and structured telemetry persistence in JSON format directly onto the internal flash memory of the ESP32-S3.

---

## 🔍 Data Structure (Log Schema)

Each scan cycle generates a complete snapshot containing:

* `timestamp_us` — Microcontroller activity timestamp in microseconds.
* `total_detected` — Total number of access points detected in the current pass.
* `stored_results` — Number of serialized access points retained (capped at a maximum of 64 per cycle to optimize memory usage).
* `devices` — Array of objects with telemetry for the detected networks:
  * `ssid` — Wireless network name.
  * `bssid` — Hardware MAC address of the access point.
  * `rssi` — Received Signal Strength Indicator (signal level in dBm).
  * `primary_channel` — Primary operational Wi-Fi channel.
  * `secondary_channel` — Secondary channel (applicable for wider channels).
  * `auth_mode` — Authentication framework used (e.g., WPA2, WPA3).
  * `pairwise_cipher` — Pairwise cipher suite.
  * `group_cipher` — Group cipher suite.
  * `antenna` — Active antenna configuration index.

---

## 💾 Flash Persistence Architecture

Scan payloads are serialized into JSON strings and stored directly in **NVS** (Non-Volatile Storage) within the flash memory:

* **Namespace:** `wardrive`
* **Key:** `last_scan_json`

> **Behavioral Note:** Every successful scan cycle overwrites the previous state, ensuring that the flash media retains only the most recent capture and avoids fragmentation or unnecessary wear (*wear-leveling* overhead).

---

## 🚀 Configuration and Compilation (ESP-IDF)

### 1. Dependencies and Registration (`main/CMakeLists.txt`)
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi)
```


### 2. Build and flash

```bash
cd Wifi-wardriving
idf.py set-target esp32s3
idf.py build flash monitor
```

## Responsible Use Notice
This tool is designed exclusively for authorized security audits, network mapping, and educational telemetry testing. Ensure you have explicit authorization before performing scans in environments or networks that you do not own or manage.
