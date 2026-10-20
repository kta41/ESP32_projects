# 📡 Wi-Fi Wardriving (ESP32-S3)

Stealth passive & promiscuous 802.11 reconnaissance sensor built on **ESP-IDF v6.1** for the **ESP32-S3**. It operates as a low-power STA client connected to a mobile hotspot (visible or hidden), buffers raw captures in a RAM ring buffer, and pushes **JSONL v1 (`POST /scans`)** batches to a companion receiver running on **Termux / Kali NetHunter** on the phone.

> 🛰️ **Central Intelligence & Analysis Hub (Docker Web Console):**
> Visual correlation, multi-session device/network tracking, the 40,355-entry IEEE OUI database, and persistent entity tagging live in the dedicated [**kta41/SIGINT-Console**](https://github.com/kta41/SIGINT-Console) repository — the central intelligence hub for all sensor projects in `ESP32_projects`.

---

## 🔍 Architecture

1. **Stealth STA Client (`main/wifi_mgr.c`)**:
   - Generates a **per-session randomized MAC** (unicast + locally administered) prior to `esp_wifi_start()`.
   - Enforces reduced TX power (`CONFIG_WARDRIVE_TX_POWER`, default `40` = 10 dBm).
   - Uses full-channel scan on association (`WIFI_ALL_CHANNEL_SCAN`) so **hidden SSID** hotspots work reliably out of the box.
   - Non-blocking FreeRTOS reconnection timer (5 s rate limit) coordinated with the scanner task so connection retries never block `esp_wifi_scan_start()`.
2. **Passive Scanner + Simultaneous 802.11 Promiscuous Sniffer (`main/scanner.c`, `main/promisc.c`)**:
   - Strict 15 s start-to-start cadence (`vTaskDelayUntil`) using `WIFI_SCAN_TYPE_PASSIVE` and a 300 ms/channel dwell time (zero probe frames transmitted during channel sweeps).
   - **Simultaneous promiscuous capture (`WIFI_PKT_MGMT | WIFI_PKT_DATA`)**: extracts associated client stations (`ToDS`/`FromDS` data frames + `Assoc`/`Reassoc`/`Auth` management frames), client `Probe Request` PNLs (searched SSIDs), and `Deauth`/`Disassoc` frame counters.
   - **Triple identity exclusion**: automatically strips the ESP32's own MAC, the active hotspot BSSID, the last remembered hotspot BSSID, and `CONFIG_WARDRIVE_WIFI_SSID` from both AP lists and promiscuous tables.
3. **RAM Ring Buffer (`main/scan_store.c`)**:
   - Pre-allocates `CONFIG_WARDRIVE_RING_SLOTS` (8 slots × 128 raw AP records in BSS) with zero flash writes.
   - Drops the oldest scan and increments the cumulative `lost_before` counter if the uplink stays down across more than 8 cycles.
   - Flags retained scans with `buffered: true` whenever a push cycle fails.
4. **Batched HTTP Pusher (`main/pusher.c`)**:
   - Serializes payloads on the fly via `build_scan_json_v1()` (`main/jsonl_writer.c`) and sends up to 4 scans per `POST http://<gateway_ip>:47471/scans` (`Content-Type: application/x-ndjson`).
   - Includes `X-Session-Id`, `X-First-Scan-Id`, and `X-Last-Scan-Id` headers, an 8 s timeout, and stepped retries (`+1 s`, `+4 s`, `+8 s`). Skips HTTP push immediately without wasting timeouts when Wi-Fi is disconnected.
5. **Mobile Collector & CLI Suite for Termux / Kali NetHunter (`tools/`)**:
   - `tools/listener.py` (`warwifi`): multi-threaded HTTP receiver (`POST /scans`, `GET /health`), in-memory `(session_id, scan_id)` deduplication, live colorized terminal table (APs + `[STA]` clients + `[PROBE]` PNLs + `DEAUTH` alerts), and append-only persistence in `wardrive_YYYYMMDD_HHMM.jsonl`.
   - `tools/warmap.py` (`warmap`): CLI & offline SIGINT analyzer (channel occupancy, auth/cipher breakdown, weak networks, exposed WPS, hidden SSIDs, 40,355-prefix IEEE OUI resolution, CSV, WiGLE CSV v1.4, and standalone HTML dashboard export).
   - `tools/validate.py` (`warcheck`): schema v1 verifier checking `scan_id` monotonicity, `lost_before` consistency, and duplicate absence.
   - `tools/android/`: Kali NetHunter `proot` launcher (`nethunter-launcher.sh`), mobile CLI wrappers (`warwifi`, `warmap`, `warcheck`), phone deployment script (`deploy_phone.sh`), and PC sync utility (`warpull`).

---

## 📦 JSONL v1 Payload Schema

Each scan line emitted by the ESP32-S3 and wrapped by `listener.py` follows:

```json
{
  "recv_at": "2026-10-20T15:20:24.163+02:00",
  "session_name": null,
  "gps": null,
  "payload": {
    "v": 1,
    "mode": "scan",
    "scan_type": "passive",
    "session_id": "9f3a21c4",
    "scan_id": 1,
    "device_ts_us": 496234567,
    "scan_duration_ms": 4200,
    "total_detected": 1,
    "stored": 1,
    "dropped": 0,
    "lost_before": 0,
    "buffered": false,
    "devices": [
      {
        "ssid": "ExampleNet",
        "bssid": "AA:BB:CC:DD:EE:FF",
        "rssi": -58,
        "primary_channel": 6,
        "secondary_channel": 0,
        "auth_mode": "WPA2_WPA3_PSK",
        "pairwise_cipher": "CCMP",
        "group_cipher": "CCMP",
        "proto": "bgn",
        "band_mhz": 20,
        "wps": false,
        "hidden": false
      }
    ],
    "promisc": {
      "mgmt_frames": 42,
      "data_frames": 18,
      "probe_req_frames": 5,
      "deauth_frames": 0,
      "probes": [
        {
          "sta_mac": "12:34:56:78:9A:BC",
          "ssid": "CorpWiFi",
          "rssi": -64,
          "channel": 6,
          "count": 3
        }
      ],
      "clients": [
        {
          "sta_mac": "DE:AD:BE:EF:00:01",
          "bssid": "AA:BB:CC:DD:EE:FF",
          "rssi": -52,
          "channel": 6,
          "frames": 9
        }
      ]
    }
  }
}
```
*(If an SSID contains bytes outside printable ASCII 32–126, they are replaced with `'?'` and `"ssid_hex"` is automatically added with the raw hex bytes).*

---

## 📱 Mobile & PC Workflow

Deploy or update the collector suite on an Android phone over USB (ADB + SSH on port `8022`):

```bash
./tools/android/deploy_phone.sh
```

### On the PC (WSL) + Sync with [SIGINT-Console](https://github.com/kta41/SIGINT-Console)

```bash
warpull                        # Pulls all .jsonl files from the phone (USB or Wi-Fi), builds local reports, and syncs to ../SIGINT-Console/data/
```

To launch the central Docker hub ([**kta41/SIGINT-Console**](https://github.com/kta41/SIGINT-Console)):

```bash
git clone https://github.com/kta41/SIGINT-Console.git
cd SIGINT-Console
docker compose up -d --build
# Open http://127.0.0.1:8080 (avoid https:// / HSTS on localhost by using 127.0.0.1)
```

### On the Phone (Termux / Kali NetHunter)

```bash
warwifi                        # Starts the live listener on 0.0.0.0:47471 (with wake-lock)
warmap                         # Prints the SIGINT audit report (APs + Clients + Probes)
warmap --csv networks.csv      # Exports deduplicated BSSID catalog to CSV
warmap --wigle wigle.csv       # Exports in official WiGLE CSV v1.4 format
warmap --html dashboard.html   # Generates a standalone offline HTML SIGINT console
warcheck                       # Validates integrity and continuity of all .jsonl files
```

---

## 🚀 Configuration, Build, and Flash (ESP-IDF v6.1)

Hotspot credentials are configured via `Kconfig` and stored exclusively in your local `sdkconfig` (git-ignored):

```bash
source "$HOME/.espressif/v6.1/esp-idf/export.sh"
cd Wifi-Wardriving
idf.py set-target esp32s3
idf.py menuconfig
# Navigate to "Wardrive (sensor sigiloso)" -> set hotspot SSID and WPA2 password
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Local Unit Tests

```bash
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

---

## Responsible Use Notice

This project is designed exclusively for authorized security assessments, spectrum analysis, and educational research in environments you own or have explicit permission to audit.


