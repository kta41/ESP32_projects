# 📡 Wi-Fi Wardriving (ESP32-S3)

Sensor de reconocimiento Wi-Fi pasivo en **ESP-IDF v6.1** para **ESP32-S3** que opera como cliente STA sigiloso de un hotspot móvil (visible u oculto), almacena capturas crudas en un ring buffer en RAM y transmite los lotes en formato **JSONL v1 (`POST /scans`)** al receptor ejecutándose en **Termux / Kali NetHunter** en el teléfono.

---

## 🔍 Arquitectura

1. **Cliente STA sigiloso (`main/wifi_mgr.c`)**:
   - Genera una **MAC aleatoria por sesión** (unicast + locally administered) antes de `esp_wifi_start()`.
   - Aplica potencia de transmisión reducida (`CONFIG_WARDRIVE_TX_POWER`, por defecto `40` = 10 dBm).
   - Escaneo activo de todos los canales en el momento de asociar (`WIFI_ALL_CHANNEL_SCAN`) para soportar **SSID oculto** en el hotspot.
   - Reconexión automática con rate-limit de 5 s mediante timer FreeRTOS y coordinación con el escáner para no bloquear `esp_wifi_scan_start()`.
2. **Escáner pasivo + Sniffer Promiscuo 802.11 (`main/scanner.c`, `main/promisc.c`)**:
   - Ciclo estricto de 15 s entre inicios (`vTaskDelayUntil`) con `WIFI_SCAN_TYPE_PASSIVE` y dwell de 300 ms/canal (0 tramas de probe emitidas durante el barrido).
   - **Modo promiscuo simultáneo (`WIFI_PKT_MGMT | WIFI_PKT_DATA`)**: captura en tiempo real estaciones clientes asociadas a cada AP (tramas de datos `ToDS`/`FromDS`), peticiones `Probe Request` con los SSIDs buscados por dispositivos cercanos y contadores de tramas `Deauth`/`Disassoc`.
   - **Triple filtro de identidad**: excluye automáticamente la MAC propia del ESP32, el BSSID asociado actual, el último BSSID recordado del hotspot en la sesión y el `CONFIG_WARDRIVE_WIFI_SSID` tanto de la lista de APs como de los clientes y probes promiscuos.
3. **Ring buffer en RAM (`main/scan_store.c`)**:
   - Preasigna `CONFIG_WARDRIVE_RING_SLOTS` (8 slots × 128 APs crudos en BSS) sin escribir capturas en flash.
   - Si el enlace cae durante varios ciclos y el buffer se llena, descarta el escaneo más antiguo e incrementa el contador acumulado `lost_before`.
   - Marca `buffered: true` en los escaneos retenidos tras un fallo de red.
4. **Cliente HTTP Push por lotes (`main/pusher.c`)**:
   - Serializa en el momento del envío con `build_scan_json_v1()` (`main/jsonl_writer.c`) y envía lotes de hasta 4 escaneos por petición `POST http://<gateway_ip>:47471/scans` (`Content-Type: application/x-ndjson`).
   - Cabeceras de trazabilidad `X-Session-Id`, `X-First-Scan-Id`, `X-Last-Scan-Id`, timeout de 8 s y reintentos escalonados (`+1 s`, `+4 s`, `+8 s`). Si no hay enlace Wi-Fi, omite el push sin gastar timeouts.
5. **Suite móvil para Termux / Kali NetHunter (`tools/`)**:
   - `tools/listener.py` (`warwifi`): servidor HTTP multihilo (`POST /scans`, `GET /health`), deduplicación en memoria por `(session_id, scan_id)`, visualización en tiempo real de las redes detectadas en cada barrido y guardado append-only en `wardrive_YYYYMMDD_HHMM.jsonl`.
   - `tools/warmap.py` (`warmap`): analizador de sesiones JSONL (ocupación por canal, desglose de autenticación/cifrado, redes abiertas/WEP/TKIP, WPS expuesto, redes ocultas, resolución OUI y exportación CSV).
   - `tools/validate.py` (`warcheck`): verificador de esquema v1, continuidad de `scan_id`, coherencia de `lost_before` y ausencia de duplicados.
   - `tools/android/`: lanzador `proot` de Kali NetHunter (`nethunter-launcher.sh`), comandos cortos (`warwifi`, `warmap`, `warcheck`) y script de despliegue automatizado por ADB/SSH (`deploy_phone.sh`).

---

## 📦 Esquema del Payload JSONL v1

Cada línea enviada por el ESP32-S3 y envuelta por `listener.py` contiene:

```json
{
  "recv_at": "2026-10-20T15:20:24.163+02:00",
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
    ]
  }
}
```
*(Si un SSID contiene bytes fuera del rango ASCII imprimible 32–126, se reemplazan por `'?'` y se añade automáticamente `"ssid_hex"` con los bytes originales en hexadecimal).*

---

## 📱 Uso en el móvil (Termux / Kali NetHunter)

Para desplegar o actualizar las herramientas en el teléfono por USB (ADB + SSH en puerto `8022`):

```bash
./tools/android/deploy_phone.sh
```

Comandos disponibles directamente en Termux o dentro de Kali (`nh`):

### En este PC (WSL)

```bash
warpull                        # Baja todos los .jsonl del móvil (USB o Wi-Fi) y genera dashboard.html + wigle.csv + redes.csv
```

### En el móvil (Termux / Kali NetHunter)

```bash
warwifi                        # Inicia el listener en vivo en 0.0.0.0:47471 (con wake-lock)
warmap                         # Muestra el informe de auditoría (APs + Clientes + Probes)
warmap --csv redes.csv         # Exporta el catálogo único de BSSIDs a CSV
warmap --wigle wigle.csv       # Exporta en formato oficial WiGLE CSV v1.4
warmap --html dashboard.html   # Genera un dashboard interactivo offline en HTML
warcheck                       # Valida la integridad y continuidad de todos los .jsonl
```

---

## 🚀 Configuración, compilación y flasheo (ESP-IDF v6.1)

Las credenciales del hotspot se configuran mediante `Kconfig` y se guardan únicamente en el `sdkconfig` local (ignorado por Git):

```bash
source "$HOME/.espressif/v6.1/esp-idf/export.sh"
cd Wifi-Wardriving
idf.py set-target esp32s3
idf.py menuconfig
# Entrar en "Wardrive (sensor sigiloso)" -> configurar SSID y clave WPA2 del hotspot
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Tests locales

```bash
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

---

## Aviso de uso responsable

Esta herramienta está diseñada exclusivamente para auditorías de seguridad autorizadas, análisis de espectro e investigación educativa en entornos propios o con permiso explícito.


