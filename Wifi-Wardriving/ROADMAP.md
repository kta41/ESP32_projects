# ROADMAP — Wardrive ESP32-S3 «Nivel 3» (sensor sigiloso con push al móvil)

> **Fuente de verdad del proyecto.** El agente de desarrollo debe ejecutar las fases EN ORDEN,
> marcar los checkboxes al completarlas y hacer un commit por fase.
> Las decisiones marcadas **[CONGELADO]** no se re-discuten ni se "mejoran" sin aprobación explícita del usuario.

---

## 1. Contexto

- **Hardware:** ESP32-S3 (devkit). Alimentado por USB-C desde el móvil del usuario durante las sesiones.
- **Framework:** ESP-IDF v6.1, FreeRTOS, cJSON vía componente gestionado `espressif/cjson` (desde v6.0 cJSON dejó de estar en el core del IDF; decisión aceptada por el usuario el 2026-10-19).
- **Idioma:** logs, comentarios y commits en español.
- **Repositorio:** GitHub (estado actual = baseline descrito abajo).

### 1.1 Estado actual (baseline)

Proyecto funcional de escaneo WiFi en `main/`:
- `app_main`: init de NVS/netif/event loop, `WIFI_MODE_STA`, bucle infinito con escaneo **activo** cada 15 s.
- Funciones existentes que se CONSERVAN y evolucionan: `authmode_to_str`, `bssid_to_str`, `build_scan_json`.
- Lógica que se ELIMINA: `save_json_to_flash` (blob en NVS) — el móvil pasa a ser el almacén.
- Constantes actuales: `MAX_SCAN_RESULTS 64`, `SCAN_INTERVAL_MS 15000`.

### 1.2 Objetivo (arquitectura «Nivel 3»)

Sensor wardriving **sigiloso**:
1. Cliente **STA** del hotspot oculto (WPA2) del móvil — **nunca AP, nunca balizas**.
2. **Escaneo pasivo** — cero transmisiones durante el escaneo (solo escucha beacons).
3. **Push HTTP** del resultado al móvil (listener en Termux) tras cada escaneo.
4. **Ring buffer en RAM** para sobrevivir cortes de enlace (sin tocar flash).
5. **MAC aleatoria por sesión** + potencia de TX mínima.
6. Sin servidor HTTP en el ESP32, sin NVS de datos, sin GPS en el ESP32.

---

## 2. Decisiones congeladas

| Decisión | Valor |
|---|---|
| Tope de APs por escaneo | **128** |
| Ring buffer | **8 slots** (≈ 2 min de autonomía ante cortes), registros crudos |
| Puerto del listener | **47471** |
| Ciclo de escaneo | 15 s entre inicios de escaneo |
| Tipo de escaneo | Pasivo (dwell ~300 ms/canal) |
| Modelo de datos | Push (ESP32 envía), no polling |
| Transporte | HTTP POST al gateway del hotspot |
| Timeout del push | 8 s; reintentos a +1 s, +4 s, +8 s |
| Lote máximo por POST | 4 escaneos (POSTs secuenciales hasta drenar el ring) |
| Persistencia | Solo el móvil (JSONL append-only). El ESP32 no escribe datos en flash |
| Endpoint | `POST /scans` |
| GPS | Del móvil vía `termux-location`, en el envelope. Nada de hardware GPS en el ESP32 |

---

## 3. Especificación técnica

### 3.1 Configuración vía Kconfig (nunca hardcodear secretos)

Añadir `Kconfig.projbuild` con entradas (valores por defecto vacíos/sanos) y garantizar que `sdkconfig` está en `.gitignore`:

- `CONFIG_WARDRIVE_WIFI_SSID` — SSID del hotspot oculto del móvil
- `CONFIG_WARDRIVE_WIFI_PASS` — clave WPA2 (mín. 8 chars)
- `CONFIG_WARDRIVE_LISTENER_PORT` — default `47471`
- `CONFIG_WARDRIVE_SCAN_INTERVAL_MS` — default `15000`
- `CONFIG_WARDRIVE_MAX_APS` — default `128`
- `CONFIG_WARDRIVE_RING_SLOTS` — default `8`
- `CONFIG_WARDRIVE_TX_POWER` — default `40` (unidades de 0.25 dBm → 10 dBm; rango válido 8–84)

### 3.2 WiFi y conexión

- Modo: `WIFI_MODE_STA` único. Credenciales desde Kconfig.
- Si la asociación al SSID oculto falla con defaults, configurar `.sta.scan_method = WIFI_ALL_CHANNEL_SCAN` y `.sort_method = WIFI_CONNECT_AP_BY_SIGNAL`.
- **MAC aleatoria por sesión**: generar antes de `esp_wifi_start()` con `esp_wifi_set_mac(WIFI_IF_STA, mac)`. Regla del primer byte: `mac[0] = (esp_random() & 0xFC) | 0x02` (unicast + locally-administered); resto aleatorio. Loggearla al arrancar.
- **Potencia de TX mínima**: `esp_wifi_set_max_tx_power(CONFIG_WARDRIVE_TX_POWER)` DESPUÉS de `esp_wifi_start()`.
- Manejador de eventos: `WIFI_EVENT_STA_START` → conectar; `IP_EVENT_STA_GOT_IP` → guardar IP + **gateway** (objetivo del push) + flag `conectado`; `WIFI_EVENT_STA_DISCONNECTED` → limpiar flag y programar reintento (no bloquear en el handler; rate-limit 1 reintento/5 s desde el task de escaneo).
- NVS: se inicializa como hasta ahora (el driver WiFi lo necesita), pero **sin ningún blob de datos**.

### 3.3 Escaneo pasivo

- `wifi_scan_config_t`: `channel = 0` (todos), `show_hidden = true`, `scan_type = WIFI_SCAN_TYPE_PASSIVE`, `scan_time.passive ≈ 300` ms/canal (total ≈ 4–5 s).
- Recoger hasta `CONFIG_WARDRIVE_MAX_APS` registros con `esp_wifi_scan_get_ap_records()`.
- **Filtrar el BSSID del AP asociado** (el hotspot del propio móvil, obtenido con `esp_wifi_sta_get_ap_info()`) del array de dispositivos. No aparece en el JSON.
- Escaneo **bloqueante** dentro del task de escaneo (no hay servidor HTTP que atender).

### 3.4 Identidad de sesión

- `session_id`: 8 caracteres hex a partir de `esp_random()` generado una vez por arranque. Es la identidad del dispositivo en los ficheros (la MAC cambia por sesión).
- `scan_id`: `uint32_t` monótono, empieza en 1 por arranque.

### 3.5 Esquema del payload JSONL v1 **[CONGELADO]**

Una línea JSON por escaneo (terminada en `\n`). Ejemplo:

```json
{
  "v": 1,
  "mode": "scan",
  "scan_type": "passive",
  "session_id": "9f3a21c4",
  "scan_id": 47,
  "device_ts_us": 481234567,
  "scan_duration_ms": 4950,
  "total_detected": 31,
  "stored": 31,
  "dropped": 0,
  "lost_before": 0,
  "buffered": false,
  "devices": [
    {
      "ssid": "MiRouter",
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
```

Semántica de campos de cabecera:

| Campo | Semántica |
|---|---|
| `v` | Versión de esquema (1) |
| `mode` | Siempre `"scan"` por ahora (futuro: `"promisc"`) |
| `scan_type` | `"passive"` |
| `session_id` | Identidad de la sesión (3.4) |
| `scan_id` | Contador monótono por sesión, empieza en 1 |
| `device_ts_us` | `esp_timer_get_time()` al completar el escaneo |
| `scan_duration_ms` | Duración real del escaneo (auditoría de salud) |
| `total_detected` | APs vistos antes de recortar |
| `stored` | APs incluidos en `devices` |
| `dropped` | `total_detected - stored` |
| `lost_before` | Acumulado de **escaneos enteros** descartados por ring lleno (persistente durante la sesión) |
| `buffered` | `true` si este escaneo sufrió al menos un ciclo de push fallido antes de enviarse |

Reglas de serialización por dispositivo:

- Mantener nombres de campo actuales (`ssid`, `bssid`, `rssi`, `primary_channel`, `secondary_channel`, `auth_mode`).
- `auth_mode`: extender `authmode_to_str` a TODOS los enums de IDF v5.x; fallback `"UNKNOWN_%d"`.
- `pairwise_cipher` / `group_cipher`: **ahora en texto** (mapa de `wifi_cipher_type_t` → `"NONE","WEP40","WEP104","TKIP","CCMP","TKIP_CCMP","AES_CMAC_128","GCMP_128",...`; fallback `"UNKNOWN_%d"`).
- `proto`: string con los flags `phy_11b`/`phy_11g`/`phy_11n` → `"b"`, `"g"`, `"n"` combinados (p. ej. `"bgn"`). Campo adicional `"lr": true` solo si `phy_lr`.
- `band_mhz`: 20 si `secondary == WIFI_SECOND_CHAN_NONE`, si no 40.
- `wps`: flag `wps` del registro.
- `hidden`: `true` si SSID de longitud 0.
- **Sanitización de SSID**: bytes imprimibles ASCII (32–126) se conservan; el resto se sustituye por `'?'`. Si hubo sustitución, añadir campo `ssid_hex` con los bytes originales en hexadecimal. El JSON debe ser SIEMPRE válido UTF-8.

### 3.6 Ring buffer

- Array preasignado de `CONFIG_WARDRIVE_RING_SLOTS` slots. Cada slot: metadatos (scan_id, ts, duration, counters, flags) + `wifi_ap_record_t[CONFIG_WARDRIVE_MAX_APS]` + contador. **NO se almacenan strings JSON en el ring** (128 APs × ~80 B ≈ 10 KB/slot → ~85 KB total).
- Al terminar el escaneo: encolar en slot libre; si no hay, descartar el más antiguo e incrementar `lost_before`.
- Serialización a JSON **solo en el momento del push**.

### 3.7 Protocolo de push **[CONGELADO]**

- `POST http://<gateway_ip>:<CONFIG_WARDRIVE_LISTENER_PORT>/scans`
- Headers: `Content-Type: application/x-ndjson`, `X-Session-Id`, `X-First-Scan-Id`, `X-Last-Scan-Id`
- Body: JSONL — una línea por escaneo pendiente, **máx. 4 por POST**; si hay más pendientes, POSTs secuenciales hasta drenar el ring o hasta un fallo.
- Body con `Content-Length` conocido (no chunked — el listener es `http.server` estándar).
- Respuesta de éxito: HTTP 200 y body `{"accepted": N, "last_scan_id": M}`.
  - Si `N == líneas enviadas` → drenar esos escaneos del ring.
  - Si `accepted < enviados` (línea malformada) → la respuesta incluye `first_bad_scan_id`; drenar solo el prefijo contiguo con `scan_id < first_bad_scan_id`.
  - Si error HTTP/timeout/body no parseable → tratar como fallo, NO drenar (el dedup del listener hace segura la retransmisión).
- Timeout: 8 s. Reintentos por ciclo: +1 s, +4 s, +8 s. Si no hay conexión WiFi: **no gastar timeouts** — saltar el push directamente.
- El push nunca bloquea el siguiente escaneo: el ciclo de 15 s manda.
- Los escaneos vacíos (0 redes) se envían igualmente (prueba de vida + continuidad de `scan_id`).
- Log por ciclo: `Escaneo #N: X redes (Y s) | push: OK/RETRY/FALLA (Z bytes, K pendientes)`.

### 3.8 Casos borde (comportamiento obligatorio)

| Situación | Comportamiento |
|---|---|
| Push falla 3 reintentos | El escaneo queda en el ring (`buffered=true` en futuros envíos); se sigue escaneando |
| Ring lleno (corte largo) | Descartar el más antiguo + incrementar `lost_before` |
| Listener caído y reiniciado | Backfill automático del ring completo en lotes de 4 |
| Retransmisión tras fallo ambiguo | El listener deduplica por `(session_id, scan_id)` — reenviar es seguro |
| Sesión con `session_id` nuevo | El listener abre fichero nuevo |
| Escaneo con 0 redes | Se envía igualmente |
| Sin WiFi durante todo el ciclo | Escaneo continúa, push se salta sin timeouts, ring acumula |

### 3.9 Listener móvil (Python, Termux) — `tools/listener.py`

- **Solo stdlib** (`http.server.ThreadingHTTPServer`). Puerto `47471` (argumento `--port`, default 47471).
- `POST /scans`: leer body por `Content-Length`, dividir en líneas, parsear y validar cada una.
- **Deduplicación**: set en memoria de `(session_id, scan_id)`. Líneas repetidas → ignoradas (contadas como aceptadas).
- **Envelope** (el móvil envuelve, no modifica): `{"recv_at": "<ISO8601 con offset y ms>", "gps": {...}|null, "payload": {<línea original intacta>}}`.
  - `--gps`: opcional; llamar a `termux-location -p network` con caché mínima de 10 s; si falla → `"gps": null`. Estructura gps: `{"lat","lon","acc_m","src":"phone"}`.
- **Fichero**: `wardrive_YYYYMMDD_HHMM.jsonl` en el directorio `--out` (default `./wardrive_data`); abrir fichero nuevo cuando cambia `session_id` o al arrancar. Append-only, `\n` por línea.
- Respuestas: `200 {"accepted": N, "last_scan_id": M}`; si alguna línea es inválida: `400 {"error": "...", "first_bad_scan_id": X}` (se aplican solo las líneas anteriores a la mala).
- `GET /health`: `{"status":"ok","sessions":[...],"lines": N}` para verificación manual.
- Instrucciones de uso en cabecera del script: `termux-setup-storage` (una vez), `termux-wake-lock`, `python listener.py`.

### 3.10 Validador — `tools/validate.py`

- Solo stdlib. Recibe rutas de `.jsonl` y verifica: JSON válido por línea, campos obligatorios presentes, por sesión `scan_id` ascendente sin huecos no justificados (comprobar coherencia con `lost_before`), sin duplicados `(session_id, scan_id)`, rangos sanos (rssi −100..0, canal 1–14...).
- Salida: resumen por sesión + `exit code` 0/1. Es el verificador oficial de los gates.

---

## 4. Fases de desarrollo

> Regla de oro: **no se avanza de fase sin gate superado.** Los gates requieren validación física que solo el usuario puede hacer (el agente compila y prepara, el usuario verifica en hardware y confirma).

### Fase 0 — Preparación del móvil (SOLO usuario, sin código)

- [ ] Hotspot Android: oculto + WPA2 + clave. Canal fijo si el fabricante lo permite.
- [ ] Termux instalado: `pkg install python`; eximir Termux de optimización de batería.
- [ ] `termux-setup-storage` ejecutado una vez.
- [ ] `termux-location` probado y devolviendo coordenadas.
- [ ] Anotados: SSID, clave, subnet observada del hotspot.

**Gate 0:** otro dispositivo se asocia al hotspot oculto y `termux-location` devuelve coordenadas en Termux.

### Fase 1 — Conectividad STA sigilosa

- [x] `Kconfig.projbuild` con todas las entradas (3.1) + `sdkconfig` en `.gitignore` + `sdkconfig.defaults` sin secretos.
- [x] Refactor de la inicialización: STA puro, conexión al hotspot oculto, eventos (3.2).
- [x] MAC aleatoria por sesión + log al arranque.
- [x] TX mínimo tras `esp_wifi_start`.
- [x] Log de conexión: IP, gateway, ping al gateway OK.
- [x] Eliminar el guardado en NVS y el bucle antiguo SOLO cuando la nueva base compile y conecte (puede coexistir temporalmente tras un flag). *(Guardado NVS eliminado; bucle activo conservado tras `WARDRIVE_DEBUG_LEGACY_SCAN`, por defecto apagado, para el A/B del Gate 2.)*

**Gate 1:** el ESP32 se asocia al hotspot oculto, loggea IP+gateway y responde al ping al gateway. Usuario lo confirma.

### Fase 2 — Escaneo pasivo y payload v1

- [x] Task dedicado de escaneo (`xTaskCreate`), ciclo de 15 s entre inicios (3.3).
- [x] Escaneo pasivo con dwell ~300 ms/canal; medición real de `scan_duration_ms`.
- [x] Filtrado del BSSID asociado.
- [x] `jsonl_writer`: payload v1 completo (3.5) con sanitización SSID y todos los mapeos. *(Nota v6.1: `WIFI_AUTH_WPA3_EXT_PSK`/`_MIXED_MODE` ya no existen y `WIFI_AUTH_ENTERPRISE` es alias de `WIFI_AUTH_WPA2_ENTERPRISE`; mapeados todos los enums reales de v6.1 + fallback `UNKNOWN_%d`.)*
- [x] Log de resumen por escaneo (sin volcar el JSON entero por serial salvo con flag DEBUG). *(flag `WARDRIVE_DEBUG_DUMP_JSON`, default apagado.)*

**Gate 2:** A/B en el mismo punto contra el escaneo activo del baseline (recuento comparable, sin probes propios verificables) y JSON validado con `validate.py` o jq.

### Fase 3 — Ring buffer y push

- [ ] `scan_store`: ring crudo de 8 slots (3.6) + `lost_before` + flag `buffered`.
- [ ] `pusher`: cliente HTTP al gateway:47471 según contrato (3.7), lotes ≤4, acuse, drenaje, reintentos.
- [ ] Reconexión WiFi con rate-limit (3.2), push saltado sin timeouts si no hay conexión.
- [ ] `app_main` queda como init + espera; todo el trabajo en el task de escaneo.

**Gate 3:** simulación de corte (apagar hotspot 2–3 min) → al volver, backfill completo; `validate.py` sin duplicados ni huecos no justificados; `lost_before` coherente si el corte fue >2 min.

### Fase 4 — Listener y ficheros en el móvil

- [ ] `tools/listener.py` completo (3.9).
- [ ] `tools/validate.py` completo (3.10).
- [ ] Pruebas locales del listener (curl con líneas de prueba, retransmisión, líneas malas).

**Gate 4:** sesión real breve (10 min) → fichero `.jsonl` válido, envelope con `recv_at` (+gps si se activó), dedup verificado con retransmisión manual.

### Fase 5 — Endurecimiento y prueba de campo

- [ ] Watchdog del ciclo (task_wdt o vigilante del progreso de `scan_id`).
- [ ] Log de heap libre por ciclo (detección de fugas).
- [ ] Revisión de errores de `esp_http_client` y de memoria en todos los caminos.
- [ ] Prueba de campo guiada 30–60 min con el móvil alimentando por USB-C.

**Gate 5 (criterios globales):** (a) `scan_id` continuo salvo `lost_before` justificados; (b) 0 duplicados; (c) JSONL 100 % válido; (d) heap estable sin fuga; (e) reconexión automática tras volver a rango; (f) consumo de batería del móvil anotado.

---

## 5. Estructura de módulos sugerida

```
main/
  main.c            (init + arranque de task)
  wifi_mgr.{c,h}    (STA, MAC aleatoria, eventos, gateway)
  scanner.{c,h}     (escaneo pasivo, ciclo, filtrado BSSID propio)
  scan_store.{c,h}  (ring buffer crudo, lost_before)
  jsonl_writer.{c,h}(payload v1, sanitización, mapeos)
  pusher.{c,h}      (HTTP push, acuse, reintentos)
  Kconfig.projbuild
tools/
  listener.py
  validate.py
ROADMAP.md          (este documento, con checkboxes actualizados)
```

El agente puede adaptar nombres/estructura, pero manteniendo la separación de responsabilidades.

---

## 6. Fuera de alcance (fases futuras — NO implementar ahora)

- Modo promiscuo (`"mode": "promisc"`).
- Export a WiGLE / CSV.
- Transporte USB, BLE o dual-ESP32.
- Dashboard web, servidor HTTP en el ESP32.
- GPS externo conectado al ESP32.
- Cualquier persistencia de datos en flash del ESP32.
