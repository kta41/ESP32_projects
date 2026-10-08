# 🗺️ Roadmap

Planned next projects for this workspace. Both reuse the **ESP32-RF-Lab hardware baseline**: the shared `SPI2_HOST` bus, the ST7789 240x240 display with LVGL, the four-button keypad (UP `GPIO 1` / DOWN `GPIO 2` / OK `GPIO 19` / BACK `GPIO 20`) and the MicroSD socket (`GPIO 14`).

Status: `BLE-Recon` — **M2 implemented** (iBeacon/Eddystone/FindMy parsing). `SubGHz-Replay` — planned.

---

## 📡 `SubGHz-Replay/` — Sub-GHz Capture & Analysis (CC1101)

A test bench for auditing **fixed-code** remotes (EV1527 and similar) on **authorized/own equipment only**: OOK/ASK signal capture, frame analysis, and replay behind an explicit confirmation flow.

### Hardware
| Element | Assignment |
| :--- | :--- |
| CC1101 (VCC **3.3 V** — never 5 V) | Shared `SPI2_HOST` bus: SCK `GPIO 12`, MOSI `GPIO 11`, MISO `GPIO 13` |
| `CSN` | `GPIO 5` |
| `GDO0` (baseband data in/out) | `GPIO 4` |
| `GDO2` (optional carrier detect) | `GPIO 8` |
| UI & storage | RF-Lab ST7789 + LVGL + keypad + MicroSD |

### Modules
| Module | Responsibility |
| :--- | :--- |
| `cc1101_driver.c/h` | Register-level SPI driver: `PARTNUM` self-test, ASK/OOK configuration for 433.92 / 868.35 MHz (SmartRF-derived), async RX/TX modes |
| `signal_capture.c/h` | Capture via the **RMT peripheral** (hardware edge timing at 1 µs resolution — no ISR bit-banging) into pulse-duration buffers |
| `signal_analyze.c/h` | Short/long pulse clustering, EV1527 decode (20-bit ID + 4-bit key), **rolling-code detection (HCS301-style)** — flagged honestly as "replay will not work" |
| `signal_store.c/h` | 8 capture slots in RAM + JSON/CSV export to MicroSD |
| `signal_replay.c/h` | Replay via RMT TX on GDO0 with an "authorized target?" confirmation dialog and a hard repeat cap |
| `main.c` + UI | Orchestrator + LVGL screens: capture list, pulse waveform (canvas), statistics, region picker 433/868 |

### Design guardrails
* Replay requires double confirmation; LED lit during emission.
* **Out of scope by design**: rolling-code cracking, brute-forcers, RollJam techniques, code databases.
* EU note: 433/868 MHz bands carry duty-cycle limits; replaying signals against third-party systems is a criminal offense in most jurisdictions (Spain: art. 197 bis CP).

### Milestones
1. **M1** — Driver bring-up: SPI + `PARTNUM` check, RX config at 433.92 MHz
2. **M2** — RMT capture + pulse-train rendering in LVGL
3. **M3** — Capture slots + JSON persistence to SD
4. **M4** — Analyzer (EV1527 decode + rolling-code flag)
5. **M5** — Replay with confirmation + region selector
6. **M6** — README (repo style) + `docs/THEORY.md` update + root layout

---

## 📶 `BLE-Recon/` — Bluetooth LE Scanner (NimBLE)

The BLE companion to `Wifi-Wardriving`: **passive by default**.

### Modules
| Module | Responsibility |
| :--- | :--- |
| `ble_scanner.c/h` | NimBLE host (lighter than Bluedroid), continuous asynchronous scanning (60 ms window), FreeRTOS tasks |
| `ble_parser.c/h` | Decode: address type, RSSI, local name, UUIDs, manufacturer data → **iBeacon** (`0x4C00`/`0x0215`: UUID/major/minor), **Eddystone** (`0xFEAA`), **FindMy/AirTag heuristics** (rotating identifiers, honestly noted) |
| `ble_logger.c/h` | MAC deduplication (hash table: first/last seen, counters), JSON snapshot to NVS (`namespace=blerecon`, wardriving-style) + JSONL sessions on SD |
| `ibeacon_tx.c/h` | **Lab TX mode**: standard iBeacon broadcaster (100 ms interval, configurable UUID/major/minor) |
| `main.c` + UI | Dashboard: totals, iBeacons, tags, peak RSSI, latest names |

### Scope note
The active third-party "pairing flood" mode (spoofed Apple TV/AirPods/Swift Pair solicitations) is **deliberately out of scope**: it is indiscriminate against bystanders' devices and cannot be authorized by anyone. The legitimate replacement is the `ibeacon_tx` **self-test loop**: the board emits its own beacon with its own identity, and the scanner must detect and log it — validating the full TX→RX→parser→JSON pipeline with no extra hardware.

### Milestones
1. **M1** — NimBLE scan → serial log
2. **M2** — iBeacon/Eddystone/FindMy parser
3. **M3** — LVGL dashboard
4. **M4** — NVS + SD persistence
5. **M5** — iBeacon TX↔RX self-test
6. **M6** — README + `docs/THEORY.md` update + root layout

---

## Conventions

* Folder names follow the established PascalCase convention (`SubGHz-Replay/`, `BLE-Recon/`).
* Each project ships an English README in the `Wifi-Wardriving` style plus a Responsible Use Notice.
* Suggested order: **BLE-Recon first** (zero new hardware) while the CC1101 gets wired; then SubGHz-Replay.
* Optional later: extract `display_st7789` + keypad into a shared `components/lab_ui/` component once both projects exist and the overlap is proven.
