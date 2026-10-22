# 📡 Wi-Fi & BLE Wardriving Sensor (ESP32-S3)

Multi-protocol stealth **802.11 + Bluetooth Low Energy (BLE)** reconnaissance sensor built on **ESP-IDF v6.1** for the **ESP32-S3**. It operates as a low-power STA client connected to a mobile hotspot (visible or hidden), executes passive 2.4 GHz sweeps alongside 802.11 promiscuous sniffing, selective **PCAP + Radiotap** handshake capture, and concurrent **NimBLE passive observation**, buffers telemetry in a compact RAM ring buffer, and streams **JSONL v1 (`POST /scans`)** + **PCAP (`POST /pcap`)** batches to the unified `warwifi` collector running on **Termux / Kali NetHunter**.

> 🛰️ **Central Intelligence & Analysis Hub (Docker Web Console):**
> Visual correlation, multi-session device/network tracking, cross-protocol Wi-Fi ↔ BLE timelines, the 40,355-entry IEEE OUI database, and persistent entity tagging live in the dedicated [**kta41/SIGINT-Console**](https://github.com/kta41/SIGINT-Console) repository — the central intelligence hub for all sensor projects in `ESP32_projects`.

---

## 🔍 Architecture

1. **Stealth STA Client (`main/wifi_mgr.c`)**:
   - Generates a **per-session randomized MAC** (unicast + locally administered) prior to `esp_wifi_start()`.
   - Enforces reduced TX power (`CONFIG_WARDRIVE_TX_POWER`, default `40` = 10 dBm).
   - Uses full-channel scan on association (`WIFI_ALL_CHANNEL_SCAN`) so **hidden SSID** hotspots associate reliably out of the box.
   - Coordinates initial boot association, in-progress 802.11/DHCP handshakes, and immediate post-scan reconnection so channel sweeps never starve STA association under Wi-Fi + BLE coexistence.
2. **Passive 802.11 Scanner + Promiscuous Sniffer & Selective PCAP/Radiotap (`main/scanner.c`, `main/promisc.c`, `main/pcap_store.c`)**:
   - Strict 15 s start-to-start cadence (`vTaskDelayUntil`) using `WIFI_SCAN_TYPE_PASSIVE` (zero probe requests transmitted during channel sweeps) with automatic coexistence dwell adaptation when NimBLE is active.
   - **Simultaneous promiscuous capture (`WIFI_PKT_MGMT | WIFI_PKT_DATA`)**: extracts associated client stations (`ToDS`/`FromDS` data frames + `Assoc`/`Reassoc`/`Auth` management frames), client `Probe Request` PNLs (searched SSIDs), `Deauth`/`Disassoc` counters, and per-entity PCAP trace flags (`EAPOL`, `DEAUTH`, `ASSOC`, `AUTH`, `PROBE_REQ`, `PROBE_RESP`, `ACTION`, `BEACON`).
   - **Selective PCAP + Radiotap (`DLT_IEEE802_11_RADIO = 127`)**: buffers raw 802.11 management frames (deduplicated Beacons per BSSID/cycle, Probe Req/Resp, Assoc/Reassoc, Auth, Deauth/Disassoc, Action) and **WPA/WPA2/WPA3 EAPOL handshakes** (`0x888E`) in RAM (`CONFIG_WARDRIVE_PCAP_BUF_KB`, default 16 KB) and streams them via `POST /pcap` to the phone, which writes standard `.pcap` files with UTC-rebased timestamps ready for Wireshark / `hcxpcapngtool` / `hashcat` / `aircrack-ng`.
   - **Triple identity exclusion**: automatically strips the ESP32's own MAC, the active hotspot BSSID, the last remembered hotspot BSSID, and `CONFIG_WARDRIVE_WIFI_SSID` from AP lists, promiscuous tables, and `.pcap` captures.
3. **BLE Fusion (`main/ble_fusion.c`, `main/ble_parser.c`, default `y`)**:
   - A 100 % **passive** NimBLE observer (`passive = 1`, 50 % coexistence duty cycle `30 ms / 60 ms` — never transmits `SCAN_REQ`) runs concurrently with the Wi-Fi stack via `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`.
   - Every scan cycle embeds a `"ble"` summary block into the JSONL v1 payload: unique BLE addresses (cycle cache of 96 MACs), classification counters (`iBeacon`, `Eddystone UID/URL/TLM`, `FindMy/AirTag` candidates, `other`), total `adv_frames`, and the top-N strongest BLE devices (`CONFIG_WARDRIVE_MAX_BLE_DEVICES`, default 24).
4. **Opt-In Red Team Deauth TX & Authenticated UDP Control (`main/deauth_tx.c`, `main/deauth_udp.c`, default `n`)**:
   - **Disabled by default (`CONFIG_WARDRIVE_DEAUTH_ENABLE=n`)**: standard builds compile 100 % receive-only with inert stubs.
   - When explicitly enabled for authorized assessments, exposes a silent UDP control socket on `:47472` gated by a shared secret (`CONFIG_WARDRIVE_DEAUTH_TOKEN` — unauthenticated packets receive zero response).
   - Enforces hard Kconfig guardrails: max frames per burst (`CONFIG_WARDRIVE_DEAUTH_MAX_BURST`, default 16), minimum cooldown between bursts (`CONFIG_WARDRIVE_DEAUTH_MIN_PERIOD_S`, default 10 s), and hard session quota per boot (`CONFIG_WARDRIVE_DEAUTH_MAX_TOTAL`, default 256).
   - Queued bursts execute synchronously between scan cycles and log auditable telemetry (`frames`, `bursts`, `rejected`) in the JSONL `"deauth_tx"` block.
5. **Compact RAM Ring Buffer & Zero-DOM Streaming Serializer (`main/scan_store.c`, `main/jsonl_writer.c`, `main/pusher.c`)**:
   - Pre-allocates `CONFIG_WARDRIVE_RING_SLOTS` (8 slots × 128 compact `wardrive_ap_record_t` entries in BSS) with zero flash wear, leaving **~73 KB of free internal DRAM heap** alongside Wi-Fi + NimBLE.
   - Two-pass `vsnprintf` streaming serializer (`build_scan_json_v1()`) formats complete JSONL v1 lines in a single contiguous allocation without intermediate `cJSON` DOM trees, preventing heap fragmentation.
   - Pushes NDJSON (`POST /scans`) and PCAP (`POST /pcap`) with `X-Session-Id`, `X-First-Scan-Id`, `X-Last-Scan-Id` headers and stepped retries (`+1 s`, `+4 s`, `+8 s`).
6. **Companion Repositories (3-Tier Architecture)**:
   - [**`kta41/SIGINT-Client`**](https://github.com/kta41/SIGINT-Client) *(Private)*: Unified `warwifi` CLI for Android / Termux / Kali NetHunter and PC (live radar, per-scan GPS, PCAP triage, orchestrated offline cracking, session management, tactical dossiers, and delta sync).
   - [**`kta41/SIGINT-Console`**](https://github.com/kta41/SIGINT-Console) *(Private)*: Central Docker intelligence & correlation web console listening on port `47891` (HTTP) / `47892` (HTTPS) with deduplicated JSONL/PCAP storage and session management APIs.

---

## 📦 JSONL v1 Payload Schema

Each scan line emitted by the ESP32-S3 and wrapped by `listener.py` follows:

```json
{
  "recv_at": "2026-10-22T23:59:12.410+02:00",
  "session_name": "Ruta_Noche",
  "gps": {
    "lat": 40.416775,
    "lon": -3.70379,
    "acc": 4.5
  },
  "payload": {
    "v": 1,
    "mode": "scan",
    "scan_type": "passive",
    "session_id": "9f3a21c4",
    "scan_id": 1,
    "device_ts_us": 17312000,
    "scan_duration_ms": 10200,
    "total_detected": 32,
    "stored": 32,
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
      "mgmt_frames": 84,
      "data_frames": 31,
      "probe_req_frames": 6,
      "deauth_frames": 0,
      "eapol_frames": 4,
      "pcap_frames": 64,
      "pcap_dropped": 0,
      "pcap_bytes": 16374,
      "probes": [
        {
          "sta_mac": "12:34:56:78:9A:BC",
          "ssid": "CorpWiFi",
          "rssi": -64,
          "channel": 6,
          "count": 3,
          "pcap_traces": ["PROBE_REQ"]
        }
      ],
      "clients": [
        {
          "sta_mac": "DE:AD:BE:EF:00:01",
          "bssid": "AA:BB:CC:DD:EE:FF",
          "rssi": -52,
          "channel": 6,
          "frames": 9,
          "pcap_traces": ["EAPOL", "ASSOC"]
        }
      ],
      "ap_pcap": [
        {
          "bssid": "AA:BB:CC:DD:EE:FF",
          "pcap_frames": 5,
          "pcap_traces": ["EAPOL", "BEACON"]
        }
      ]
    },
    "ble": {
      "unique_devices": 22,
      "ibeacon": 1,
      "eddystone": 0,
      "findmy_candidates": 3,
      "other": 18,
      "adv_frames": 145,
      "devices": [
        {
          "mac": "F4:31:C3:11:22:33",
          "addr_type": "RANDOM",
          "type": "FINDMY_CANDIDATE",
          "rssi": -49,
          "count": 12
        }
      ]
    }
  }
}
```
*(If an SSID contains bytes outside printable ASCII 32–126, they are replaced with `'?'` and `"ssid_hex"` is automatically added with the raw hex bytes. When `CONFIG_WARDRIVE_DEAUTH_ENABLE=y` and bursts occur, an optional `"deauth_tx": {"frames": N, "bursts": M, "rejected": K}` block is appended).*

---

## 📱 Mobile Collector ([`kta41/SIGINT-Client`](https://github.com/kta41/SIGINT-Client)) & Central Hub ([`kta41/SIGINT-Console`](https://github.com/kta41/SIGINT-Console))

The Android / Termux / Kali NetHunter collector and synchronization tools live in the private [**`kta41/SIGINT-Client`**](https://github.com/kta41/SIGINT-Client) repository, unified under the single `warwifi` CLI:

```bash
cd ../SIGINT-Client
./android/deploy_phone.sh      # Installs unified 'warwifi' CLI on the phone and PC
```

On the phone (Termux / Kali NetHunter) or PC:

```bash
warwifi                                  # Starts live JSONL + PCAP radar collector on 0.0.0.0:47471
warwifi --radar --name "Ruta" --gps      # Session name + per-scan GPS tagging
warwifi --sync                           # Auto-discovers SIGINT-Console (:47891) and pushes delta JSONL + PCAP
warwifi --watch                          # Waits in background for home Wi-Fi and auto-syncs to SIGINT-Console
warwifi --status                         # Shows local capture stats & SIGINT-Console connectivity
warwifi --sessions                       # Lists session inventory across local node and SIGINT-Console hub
warwifi --rm --keep-last 3 --orphans     # Safe session/orphan PCAP cleanup (--yes to execute, supports --hub/--local)
warwifi --graph <session_id>             # Renders AP ↔ Client relationship & PNL correlation graph
warwifi --target <BSSID|SSID>            # Tactical dossier + ready-to-run Aircrack-ng / Hashcat commands
warwifi --map                            # Full SIGINT audit report (APs, Clients, PNL, OUI, --airodump, --wigle, --html)
warwifi --audit                          # Deep PCAP Red Team triage (EAPOL M1-M4, PMKID, WPS, .hc22000 export)
warwifi --crack [--wordlist W] [--bssid B] [--show]  # Orchestrated offline cracking (hashcat/aircrack-ng)
warwifi --vault                          # Inspect or store cracked PSKs and audit evidence in the vault
warwifi --report                         # Generates a structured Markdown Security Audit Report (.md)
warwifi --check                          # Validates schema integrity and continuity of all .jsonl files
warwifi --pull                           # (PC) Pulls captures from phone via SSH/ADB and syncs to SIGINT-Console
warwifi --deploy                         # (PC) Updates warwifi on the phone over SSH/ADB
```

To launch the central Docker hub ([**`kta41/SIGINT-Console`**](https://github.com/kta41/SIGINT-Console)):

```bash
cd ../SIGINT-Console
docker compose up -d --build
# Open http://127.0.0.1:47891
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

> **Note (2 MB modules):** BLE Fusion raises the binary above the default 1 MB `factory` app partition. The project ships a custom `partitions.csv` (enabled via `CONFIG_PARTITION_TABLE_CUSTOM` in `sdkconfig.defaults`) that allocates the whole 2 MB flash to the app. `idf.py flash` will also write the new partition table automatically.

---

## Responsible Use Notice

This project is designed exclusively for authorized security assessments, spectrum analysis, and educational research in environments you own or have explicit permission to audit.


