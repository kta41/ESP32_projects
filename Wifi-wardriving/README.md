# Wifi-wardriving (ESP32-S3)

Proyecto ESP-IDF para **detectar redes Wi-Fi cercanas** y **guardar un registro JSON** en la memoria flash del ESP32-S3.

## Qué registra

En cada escaneo guarda:
- `timestamp_us` del escaneo
- `total_detected` (AP detectados)
- `stored_results` (AP serializados, máximo 64 por ciclo)
- Arreglo `devices` con:
  - `ssid`
  - `bssid`
  - `rssi`
  - `primary_channel`
  - `secondary_channel`
  - `auth_mode`
  - `pairwise_cipher`
  - `group_cipher`
  - `antenna`

## Persistencia en flash

El JSON se guarda en **NVS** (flash interna) bajo:
- Namespace: `wardrive`
- Key: `last_scan_json`

Cada ciclo sobrescribe el último escaneo completo para conservar siempre la captura más reciente.

## Compilar y flashear

```bash
cd Wifi-wardriving
idf.py set-target esp32s3
idf.py build flash monitor
```

## Nota de uso responsable

Úsalo solo en entornos y redes donde tengas autorización para realizar escaneos.
