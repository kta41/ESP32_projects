/*
 * scanner — task de escaneo PASIVO (ROADMAP §3.3).
 *
 * Ciclo de CONFIG_WARDRIVE_SCAN_INTERVAL_MS entre INICIOS de escaneo
 * (vTaskDelayUntil: el escaneo de ~4-5 s nunca desplaza el siguiente).
 * Escaneo bloqueante dentro del task (no hay servidor HTTP que atender).
 */

#include "scanner.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jsonl_writer.h"
#include "deauth_tx.h"
#include "promisc.h"
#include "pusher.h"
#include "scan_store.h"
#include "session.h"
#include "wifi_mgr.h"

static const char *TAG = "SCANNER";

/* Dwell pasivo por canal [CONGELADO: ~300 ms/canal → ≈4-5 s por escaneo]. */
#define DWELL_PASIVO_MS 300

/* Buffer crudo reutilizado cada ciclo (BSS, ~11 KB: §3.6 sin strings JSON). */
static scan_result_t s_result;

/**
 * @brief Ejecuta un escaneo pasivo bloqueante y rellena res.
 *
 * Notas de semántica (§3.5): total_detected cuenta los APs vistos con el
 * BSSID propio YA excluido, así dropped refleja únicamente el recorte por
 * CONFIG_WARDRIVE_MAX_APS. scan_id solo se consume si el escaneo produjo
 * datos (un ciclo fallido no genera huecos injustificados).
 */
static bool escanear_pasivo(scan_result_t *res)
{
    wifi_scan_config_t cfg = {
        .channel = 0, /* todos los canales (§3.3) */
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE, /* 0 transmisiones propias */
#if CONFIG_WARDRIVE_BLE_FUSION
        /* Con BT/BLE activo, ESP-IDF exige scan_time.passive = 0 (default coex). */
        .scan_time.passive = 0,
#else
        .scan_time.passive = DWELL_PASIVO_MS,
#endif
    };

    int64_t t_inicio = esp_timer_get_time();
    wifi_mgr_pause_connect_for_scan();
    esp_err_t err = esp_wifi_scan_start(&cfg, true); /* bloqueante (§3.3) */
    wifi_mgr_resume_connect_after_scan();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Escaneo no iniciado (scan_start: %s)", esp_err_to_name(err));
        return false;
    }
    res->duration_ms = (uint32_t)((esp_timer_get_time() - t_inicio) / 1000);
    res->ts_us = esp_timer_get_time(); /* al completar el escaneo (§3.5) */

    uint16_t ap_num = 0;
    if (esp_wifi_scan_get_ap_num(&ap_num) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo obtener el número de APs detectados");
        return false;
    }

    /* Filtrado total del hotspot propio (§3.3): ni su BSSID ni su SSID aparecen jamás. */
    wifi_ap_record_t ap_propio;
    bool hay_propio_actual = (esp_wifi_sta_get_ap_info(&ap_propio) == ESP_OK);
    uint8_t bssid_recordado[6] = {0};
    bool hay_propio_recordado = wifi_mgr_get_hotspot_bssid(bssid_recordado);
    const char *ssid_propio = CONFIG_WARDRIVE_WIFI_SSID;

    uint16_t almacenados = 0;
    uint16_t propios_filtrados = 0;
    for (uint16_t i = 0; i < ap_num; i++) {
        wifi_ap_record_t rec;
        if (esp_wifi_scan_get_ap_record(&rec) != ESP_OK) {
            break;
        }
        bool es_propio = false;
        if (hay_propio_actual && memcmp(rec.bssid, ap_propio.bssid, 6) == 0) {
            es_propio = true;
        } else if (hay_propio_recordado && memcmp(rec.bssid, bssid_recordado, 6) == 0) {
            es_propio = true;
        } else if (ssid_propio[0] != '\0' &&
                   strncmp((const char *)rec.ssid, ssid_propio, 32) == 0) {
            es_propio = true;
        }
        if (es_propio) {
            propios_filtrados++;
            continue;
        }
        if (almacenados < CONFIG_WARDRIVE_MAX_APS) {
            wardrive_ap_record_t *dst = &res->records[almacenados++];
            memcpy(dst->bssid, rec.bssid, 6);
            memcpy(dst->ssid, rec.ssid, 33);
            dst->rssi = rec.rssi;
            dst->primary = rec.primary;
            dst->second = (uint8_t)rec.second;
            dst->authmode = (uint8_t)rec.authmode;
            dst->pairwise_cipher = (uint8_t)rec.pairwise_cipher;
            dst->group_cipher = (uint8_t)rec.group_cipher;
            dst->phy_11b = rec.phy_11b;
            dst->phy_11g = rec.phy_11g;
            dst->phy_11n = rec.phy_11n;
            dst->phy_lr = rec.phy_lr;
            dst->wps = rec.wps;
        }
    }
    esp_wifi_clear_ap_list();

    res->total_detected = (ap_num >= propios_filtrados) ? (ap_num - propios_filtrados) : 0;
    res->num_records = almacenados;
    res->stored = almacenados;
    res->dropped = (uint16_t)(res->total_detected > almacenados
                                  ? res->total_detected - almacenados
                                  : 0);
    res->lost_before = 0;   /* Fase 3: ring buffer */
    res->buffered = false;  /* Fase 3: push */
    promisc_collect_and_reset(&res->promisc);
    ble_fusion_collect_and_reset(&res->ble);
    deauth_tx_service(&res->deauth_tx);

    res->scan_id = session_next_scan_id();
    return true;
}

static void tarea_escaneo(void *pv)
{
    ESP_LOGI(TAG, "Task de escaneo pasivo: ciclo %d ms, dwell %d ms/canal",
             CONFIG_WARDRIVE_SCAN_INTERVAL_MS, DWELL_PASIVO_MS);

    /* Permitir que el primer intento de asociación al arranque concluya antes de barrer. */
    wifi_mgr_wait_initial_connect(8000);

    TickType_t proximo_inicio = xTaskGetTickCount();

    for (;;) {
        memset(&s_result, 0, sizeof(s_result));
        if (escanear_pasivo(&s_result)) {
            /* Encolar en el ring buffer crudo (§3.6). */
            scan_store_push(&s_result);

#if CONFIG_WARDRIVE_DEBUG_DUMP_JSON
            char *linea = build_scan_json_v1(&s_result, session_get_id());
            if (linea != NULL) {
                ESP_LOGI(TAG, "JSONL: %s", linea);
                free(linea);
            }
#endif

            /* Drenar el ring hacia el gateway en lotes de hasta 4 (§3.7). */
            push_report_t rep = pusher_flush_pending();
            pusher_flush_pcap(s_result.scan_id, &rep.pcap_bytes_sent, &rep.pcap_pkts_sent);

            ESP_LOGI(TAG, "Escaneo #%" PRIu32 ": %u redes, %u clientes, %u probes, %" PRIu32 " deauth, %" PRIu32 " eapol (%" PRIu32 ".%" PRIu32 " s) | "
                          "%u BLE unicos | push: %s (%u B jsonl, %" PRIu32 " pkts/%u B pcap, %u pend) | heap=%lu B",
                     s_result.scan_id, s_result.stored,
                     s_result.promisc.num_clients, s_result.promisc.num_probes,
                     s_result.promisc.deauth_frames, s_result.promisc.eapol_frames,
                     s_result.duration_ms / 1000, (s_result.duration_ms % 1000) / 100,
                     s_result.ble.enabled ? s_result.ble.unique_devices : 0,
                     pusher_status_str(rep.status),
                     (unsigned)rep.bytes_sent,
                     rep.pcap_pkts_sent, (unsigned)rep.pcap_bytes_sent,
                     (unsigned)rep.pending_after,
                     (unsigned long)esp_get_free_heap_size());
        }

        /* Periodo entre INICIOS (§2): absorbe el escaneo + intento de push;
         * si el ciclo superó los 15 s por reintentos, arranca el siguiente sin solapar. */
        TickType_t ahora = xTaskGetTickCount();
        TickType_t intervalo = pdMS_TO_TICKS(CONFIG_WARDRIVE_SCAN_INTERVAL_MS);
        if ((ahora - proximo_inicio) >= intervalo) {
            proximo_inicio = ahora;
        } else {
            vTaskDelayUntil(&proximo_inicio, intervalo);
        }
    }
}

esp_err_t scanner_start(void)
{
    scan_store_init();
    promisc_init();
    BaseType_t ok = xTaskCreate(tarea_escaneo, "scanner", 8192, NULL, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
