/*
 * ble_fusion — Escáner BLE pasivo coexistente con acumulador por ciclo.
 *
 * Implementación real (NimBLE) si CONFIG_WARDRIVE_BLE_FUSION=y; stub si no.
 * El callback GAP corre en la task del host NimBLE; el volcado atómico del
 * resumen corre en la task de escaneo WiFi -> sección crítica con spinlock.
 */

#include "ble_fusion.h"

#if CONFIG_WARDRIVE_BLE_FUSION

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "ble_parser.h"

static const char *TAG = "BLE_FUSION";

#define BLE_SCAN_WINDOW_MS 60
#define BLE_SCAN_ITVL_MS  60
#define MS_TO_SCAN_UNITS(ms) ((uint16_t)(((ms) * 1000) / 625))

/* ------------------ Estado del ciclo (spinlock) ------------------ */

static portMUX_TYPE s_ble_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    ble_fusion_summary_t summary;                /* contadores + tabla top-N */
    uint8_t cache_addr_type[BLE_FUSION_CYCLE_CACHE];
    uint8_t cache_mac[BLE_FUSION_CYCLE_CACHE][6]; /* dedup de únicos (orden TX) */
    uint16_t cache_len;
} ble_cycle_t;

static ble_cycle_t s_cycle;

/* ------------------ Texto para JSONL ------------------ */

const char *ble_fusion_type_str(uint8_t dev_type)
{
    switch ((ble_dev_type_t)dev_type) {
        case BLE_DEV_IBEACON:        return "IBEACON";
        case BLE_DEV_EDDYSTONE_UID:  return "EDDYSTONE_UID";
        case BLE_DEV_EDDYSTONE_URL:  return "EDDYSTONE_URL";
        case BLE_DEV_EDDYSTONE_TLM:  return "EDDYSTONE_TLM";
        case BLE_DEV_FINDMY:         return "FINDMY_CANDIDATE";
        case BLE_DEV_APPLE_OTHER:    return "APPLE_OTHER";
        case BLE_DEV_NAMED:          return "NAMED";
        default:                     return "UNKNOWN";
    }
}

const char *ble_fusion_addr_type_str(uint8_t addr_type)
{
    switch (addr_type) {
        case BLE_ADDR_PUBLIC:    return "PUBLIC";
        case BLE_ADDR_RANDOM:    return "RANDOM";
        case BLE_ADDR_PUBLIC_ID: return "PUBLIC_ID";
        case BLE_ADDR_RANDOM_ID: return "RANDOM_ID";
        default:                 return "UNKNOWN";
    }
}

/* ------------------ Escaneo continuo ------------------ */

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = MS_TO_SCAN_UNITS(BLE_SCAN_ITVL_MS),
        .window = MS_TO_SCAN_UNITS(BLE_SCAN_WINDOW_MS),
        .filter_policy = 0,
        .limited = 0,
        .passive = 1, /* doctrina receive-only: NUNCA emitimos SCAN_REQ */
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Escaneo BLE pasivo continuo iniciado (coexistencia WiFi+BLE)");
    }
}

static void on_sync(void)
{
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Host NimBLE reseteado (razon=%d)", reason);
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* NimBLE almacena la MAC little-endian; el JSONL usa orden de transmisión. */
static void mac_hci_to_tx(const uint8_t val[6], uint8_t out[6])
{
    for (int i = 0; i < 6; i++) {
        out[i] = val[5 - i];
    }
}

static void classify_unique_locked(uint8_t dev_type)
{
    switch ((ble_dev_type_t)dev_type) {
        case BLE_DEV_IBEACON:
            s_cycle.summary.ibeacon++;
            break;
        case BLE_DEV_EDDYSTONE_UID:
        case BLE_DEV_EDDYSTONE_URL:
        case BLE_DEV_EDDYSTONE_TLM:
            s_cycle.summary.eddystone++;
            break;
        case BLE_DEV_FINDMY:
            s_cycle.summary.findmy_candidates++;
            break;
        default:
            s_cycle.summary.other++;
            break;
    }
}

static void upsert_device_locked(const uint8_t mac_tx[6], uint8_t addr_type,
                                 const ble_adv_info_t *info, int8_t rssi)
{
    ble_fusion_summary_t *s = &s_cycle.summary;

    for (uint16_t i = 0; i < s->num_devices; i++) {
        if (memcmp(s->devices[i].mac, mac_tx, 6) == 0) {
            s->devices[i].count++;
            if (rssi > s->devices[i].rssi) {
                s->devices[i].rssi = rssi;
            }
            return;
        }
    }

    if (s->num_devices < CONFIG_WARDRIVE_MAX_BLE_DEVICES) {
        ble_fusion_device_t *d = &s->devices[s->num_devices++];
        memcpy(d->mac, mac_tx, 6);
        d->addr_type = addr_type;
        d->rssi = rssi;
        d->dev_type = (uint8_t)info->type;
        d->count = 1;
        if (info->has_name) {
            strlcpy(d->name, info->name, sizeof(d->name));
        } else {
            d->name[0] = '\0';
        }
        return;
    }

    /* Tabla llena: desplazar al más débil si el nuevo es claramente más fuerte. */
    uint16_t debil = 0;
    for (uint16_t i = 1; i < s->num_devices; i++) {
        if (s->devices[i].rssi < s->devices[debil].rssi) {
            debil = i;
        }
    }
    if (rssi > s->devices[debil].rssi + 6) {
        ble_fusion_device_t *d = &s->devices[debil];
        memcpy(d->mac, mac_tx, 6);
        d->addr_type = addr_type;
        d->rssi = rssi;
        d->dev_type = (uint8_t)info->type;
        d->count = 1;
        if (info->has_name) {
            strlcpy(d->name, info->name, sizeof(d->name));
        } else {
            d->name[0] = '\0';
        }
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGW(TAG, "Discovery completado (razon=%d); reiniciando", event->disc_complete.reason);
        start_scan();
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const struct ble_gap_disc_desc *disc = &event->disc;
    ble_adv_info_t info;
    ble_parser_decode(disc->data, disc->length_data, &info);

    uint8_t mac_tx[6];
    mac_hci_to_tx(disc->addr.val, mac_tx);

    portENTER_CRITICAL(&s_ble_lock);
    s_cycle.summary.adv_frames++;

    bool es_nuevo = true;
    for (uint16_t i = 0; i < s_cycle.cache_len; i++) {
        if (s_cycle.cache_addr_type[i] == disc->addr.type &&
            memcmp(s_cycle.cache_mac[i], mac_tx, 6) == 0) {
            es_nuevo = false;
            break;
        }
    }
    if (es_nuevo) {
        if (s_cycle.cache_len < BLE_FUSION_CYCLE_CACHE) {
            s_cycle.cache_addr_type[s_cycle.cache_len] = disc->addr.type;
            memcpy(s_cycle.cache_mac[s_cycle.cache_len], mac_tx, 6);
            s_cycle.cache_len++;
        }
        s_cycle.summary.unique_devices++;
        classify_unique_locked((uint8_t)info.type);
    }

    upsert_device_locked(mac_tx, disc->addr.type, &info, disc->rssi);
    portEXIT_CRITICAL(&s_ble_lock);
    return 0;
}

/* ------------------ API pública ------------------ */

int ble_fusion_init(void)
{
    memset(&s_cycle, 0, sizeof(s_cycle));

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init rc=%d (fusión BLE desactivada)", rc);
        return rc;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "Fusión BLE activa: NimBLE + WiFi en coexistencia");
    return 0;
}

void ble_fusion_collect_and_reset(ble_fusion_summary_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_ble_lock);
    out->enabled = true;
    out->unique_devices = s_cycle.summary.unique_devices;
    out->ibeacon = s_cycle.summary.ibeacon;
    out->eddystone = s_cycle.summary.eddystone;
    out->findmy_candidates = s_cycle.summary.findmy_candidates;
    out->other = s_cycle.summary.other;
    out->adv_frames = s_cycle.summary.adv_frames;
    out->num_devices = s_cycle.summary.num_devices;
    memcpy(out->devices, s_cycle.summary.devices,
           s_cycle.summary.num_devices * sizeof(ble_fusion_device_t));
    memset(&s_cycle, 0, sizeof(s_cycle));
    portEXIT_CRITICAL(&s_ble_lock);
}

#else /* !CONFIG_WARDRIVE_BLE_FUSION: stubs */

#include <string.h>

const char *ble_fusion_type_str(uint8_t dev_type)
{
    (void)dev_type;
    return "UNKNOWN";
}

const char *ble_fusion_addr_type_str(uint8_t addr_type)
{
    (void)addr_type;
    return "UNKNOWN";
}

int ble_fusion_init(void)
{
    return -1;
}

void ble_fusion_collect_and_reset(ble_fusion_summary_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

#endif /* CONFIG_WARDRIVE_BLE_FUSION */

