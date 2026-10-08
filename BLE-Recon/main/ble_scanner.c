#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "nimble/ble.h"       // ble_addr_t y constantes de tipo de direccion

#include "ble_scanner.h"

static const char *TAG = "BSCAN";

// ========== CONFIGURACIÓN ==========
#define SCAN_WINDOW_MS    60    // Ventana de escaneo (1 unidad BLE = 0.625 ms)
#define SCAN_ITVL_MS      60    // Intervalo = ventana -> ciclo 100% (continuo)
#define SUMMARY_PERIOD_S  30    // Periodo del informe de dispositivos unicos
#define SEEN_CACHE_SIZE   128   // Cache de direcciones ya anunciadas

#define MS_TO_SCAN_UNITS(ms) ((uint16_t)(((ms) * 1000) / 625))

// ========== CACHE DE DISPOSITIVOS VISTOS ==========
// M1: registro legible en serie -> solo se loguea la PRIMERA aparicion de
// cada direccion. En M4 esta cache evolucionara a la tabla hash de dedup.
typedef struct {
    uint8_t val[6];
    uint8_t type;
} seen_addr_t;

static seen_addr_t s_seen[SEEN_CACHE_SIZE];
static uint16_t s_seen_count = 0;

// ========== UTILIDADES ==========
static const char *addr_type_str(uint8_t type)
{
    switch (type) {
        case BLE_ADDR_PUBLIC:    return "PUBLIC";
        case BLE_ADDR_RANDOM:    return "RANDOM";
        case BLE_ADDR_PUBLIC_ID: return "PUBLIC-ID";
        case BLE_ADDR_RANDOM_ID: return "RANDOM-ID";
        default:                 return "?";
    }
}

static const char *adv_type_str(uint8_t evt)
{
    switch (evt) {
        case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:     return "ADV_IND";
        case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:     return "ADV_DIRECT_IND";
        case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:    return "SCAN_IND";
        case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND: return "ADV_NONCONN_IND";
        case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:    return "SCAN_RSP";
        default:                                 return "EXT";
    }
}

static void format_addr(char *buf, size_t len, const uint8_t val[6])
{
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             val[5], val[4], val[3], val[2], val[1], val[0]);
}

// Extrae el nombre local (AD types 0x08/0x09) de los datos de advertisement
static bool extract_local_name(const uint8_t *data, uint8_t len,
                               char *out, size_t out_len)
{
    out[0] = '\0';
    uint8_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || (uint16_t)(i + 1 + field_len) > len) {
            break;
        }
        uint8_t type = data[i + 1];
        if (type == 0x09 || type == 0x08) {   // Complete / Shortened name
            size_t name_len = (size_t)(field_len - 1);
            if (name_len >= out_len) {
                name_len = out_len - 1;
            }
            memcpy(out, &data[i + 2], name_len);
            out[name_len] = '\0';
            return true;
        }
        i += 1 + field_len;
    }
    return false;
}

static bool addr_is_new(const ble_addr_t *addr)
{
    for (uint16_t i = 0; i < s_seen_count; i++) {
        if (s_seen[i].type == addr->type &&
            memcmp(s_seen[i].val, addr->val, 6) == 0) {
            return false;
        }
    }

    if (s_seen_count < SEEN_CACHE_SIZE) {
        s_seen[s_seen_count].type = addr->type;
        memcpy(s_seen[s_seen_count].val, addr->val, 6);
        s_seen_count++;
        return true;
    }

    // Cache llena: tratar como visto para no saturar el log
    static bool warned = false;
    if (!warned) {
        warned = true;
        ESP_LOGW(TAG, "Cache de direcciones llena (%d)", SEEN_CACHE_SIZE);
    }
    return false;
}

// ========== INFORME PERIÓDICO ==========
static void maybe_log_summary(void)
{
    static uint64_t last_s = 0;
    uint64_t now_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    if (last_s == 0) {
        last_s = now_s;
        return;
    }
    if (now_s - last_s >= SUMMARY_PERIOD_S) {
        last_s = now_s;
        ESP_LOGI(TAG, "[RESUMEN] Dispositivos unicos vistos: %u", (unsigned)s_seen_count);
    }
}

// ========== ARRANQUE DE ESCANEO ==========
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = MS_TO_SCAN_UNITS(SCAN_ITVL_MS),
        .window = MS_TO_SCAN_UNITS(SCAN_WINDOW_MS),
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,          // Escaneo activo: pide SCAN_RSP para leer nombres
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error iniciando escaneo: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Escaneo continuo iniciado (ventana %d ms)", SCAN_WINDOW_MS);
    }
}

// ========== EVENTOS GAP ==========
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc *disc = &event->disc;
        char addr_str[18];
        char name[33];
        format_addr(addr_str, sizeof(addr_str), disc->addr.val);

        if (addr_is_new(&disc->addr)) {
            bool has_name = extract_local_name(disc->data, disc->length_data,
                                               name, sizeof(name));
            if (has_name) {
                ESP_LOGI(TAG, "[NUEVO] %s (%s) | %s | RSSI %d dBm | %u B adv | \"%s\"",
                         addr_str, addr_type_str(disc->addr.type),
                         adv_type_str(disc->event_type), disc->rssi,
                         (unsigned)disc->length_data, name);
            } else {
                ESP_LOGI(TAG, "[NUEVO] %s (%s) | %s | RSSI %d dBm | %u B adv | (sin nombre)",
                         addr_str, addr_type_str(disc->addr.type),
                         adv_type_str(disc->event_type), disc->rssi,
                         (unsigned)disc->length_data);
            }
        }

        // Volcado hex completo del payload solo a nivel DEBUG
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, disc->data, disc->length_data, ESP_LOG_DEBUG);

        maybe_log_summary();
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "Escaneo completado (razon=%d). Reiniciando...",
                 event->disc_complete.reason);
        start_scan();
        return 0;

    default:
        return 0;
    }
}

// ========== CALLBACKS DEL HOST ==========
static void on_sync(void)
{
    ESP_LOGI(TAG, "Host NimBLE sincronizado");
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Host NimBLE reseteado; razon=%d", reason);
}

// ========== API PÚBLICA ==========
void ble_scanner_init(void)
{
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
}
