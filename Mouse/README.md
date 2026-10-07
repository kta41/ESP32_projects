# 🖱️ Mouse Jiggler (ESP32-S3)

An ESP-IDF application that turns the ESP32-S3 into a **plug-and-play USB HID mouse**, periodically sending subtle pseudo-random cursor movements through the native USB peripheral to prevent the host operating system from entering idle or sleep mode.

---

## 🎯 Movement Behavior

Each activity cycle (currently 5 seconds — a test interval adjustable in `app_main()`) performs:

* `esp_random()` — Generates two independent deltas `dx`/`dy` in the range **[-2, 2]**; the burst is skipped whenever both resolve to zero.
* **Net-zero displacement:** the report `(dx, dy)` is transmitted, and after a 150 ms hold the exact inverse `(-dx, -dy)` is sent, returning the cursor to its original position so the jitter stays non-intrusive.
* **Status feedback:** the onboard LED (**GPIO 48**) lights up for the duration of each movement burst.
* **Enumeration guard:** reports are only sent while `tud_mounted()` returns true; otherwise a warning is logged until the USB host enumerates the device.

---

## 🧩 USB HID Stack (TinyUSB)

* **Report descriptor:** standard mouse layout registered with `HID_REPORT_ID(1)` (buttons, X/Y axes, vertical/horizontal wheel).
* **Custom configuration descriptor:** single HID interface, remote-wakeup capable, 100 mA power budget, IN endpoint `0x81` (16 bytes, 10 ms polling interval).
* **No external PHY:** the TinyUSB driver is installed against the ESP32-S3 **internal USB OTG PHY**.
* **Lifecycle callbacks:** `tud_mount_cb()` / `tud_umount_cb()` log host enumeration and disconnection events.

---

## 🚀 Configuration and Compilation (ESP-IDF)

### 1. Dependencies and Registration (`main/CMakeLists.txt`)
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio espressif__esp_tinyusb log freertos)
```

> **Dependency Note:** TinyUSB is pulled in as a managed component via `main/idf_component.yml` (`espressif/esp_tinyusb`, `espressif/usb`), and `sdkconfig.defaults` enables exactly one HID instance (`CONFIG_TINYUSB_HID_COUNT=1`).

### 2. Build and flash

```bash
cd Mouse
idf.py set-target esp32s3
idf.py build flash monitor
```

## Responsible Use Notice
This tool is intended to keep your own workstations and test benches awake (demos, burn-in tests, long-running pipelines). Do not use it to bypass idle-timeout or screen-lock policies on systems you do not own or manage.
