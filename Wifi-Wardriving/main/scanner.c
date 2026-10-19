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

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jsonl_writer.h"
#include "session.h"

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
        .scan_time.passive = DWELL_PASIVO_MS,
    };

    int64_t t_inicio = esp_timer_get_time();
    esp_err_t err = esp_wifi_scan_start(&cfg, true); /* bloqueante (§3.3) */
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

    /* Recoger hasta CONFIG_WARDRIVE_MAX_APS registros (§3.3). */
    uint16_t leidos = (ap_num > CONFIG_WARDRIVE_MAX_APS)
                          ? (uint16_t)CONFIG_WARDRIVE_MAX_APS
                          : ap_num;
    if (leidos > 0 && esp_wifi_scan_get_ap_records(&leidos, res->records) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudieron leer los registros de AP");
        return false;
    }

    /* Filtrado del BSSID asociado (§3.3): el hotspot del móvil no aparece. */
    wifi_ap_record_t ap_propio;
    bool hay_propio = (esp_wifi_sta_get_ap_info(&ap_propio) == ESP_OK);

    uint16_t almacenados = 0;
    bool propio_filtrado = false;
    for (uint16_t i = 0; i < leidos; i++) {
        if (hay_propio && memcmp(res->records[i].bssid, ap_propio.bssid, 6) == 0) {
            propio_filtrado = true;
            continue;
        }
        if (almacenados != i) {
            res->records[almacenados] = res->records[i];
        }
        almacenados++;
    }

    res->total_detected = ap_num - (propio_filtrado ? 1 : 0);
    res->num_records = almacenados;
    res->stored = almacenados;
    res->dropped = (uint16_t)(res->total_detected > almacenados
                                  ? res->total_detected - almacenados
                                  : 0);
    res->lost_before = 0;   /* Fase 3: ring buffer */
    res->buffered = false;  /* Fase 3: push */

    res->scan_id = session_next_scan_id();
    return true;
}

static void tarea_escaneo(void *pv)
{
    ESP_LOGI(TAG, "Task de escaneo pasivo: ciclo %d ms, dwell %d ms/canal",
             CONFIG_WARDRIVE_SCAN_INTERVAL_MS, DWELL_PASIVO_MS);

    TickType_t proximo_inicio = xTaskGetTickCount();

    for (;;) {
        memset(&s_result, 0, sizeof(s_result));
        if (escanear_pasivo(&s_result)) {
            /* Log de resumen por escaneo (formato §3.7; el tramo de push
             * llega en la Fase 3). */
            ESP_LOGI(TAG, "Escaneo #%" PRIu32 ": %u redes (%" PRIu32 ".%" PRIu32 " s)",
                     s_result.scan_id, s_result.stored,
                     s_result.duration_ms / 1000, (s_result.duration_ms % 1000) / 100);

            /* Serializar cada ciclo: prueba de humo del payload v1 y del
             * heap (Gate 2). En la Fase 3 la serialización se moverá al
             * momento del push (§3.6). */
            char *linea = build_scan_json_v1(&s_result, session_get_id());
            if (linea == NULL) {
                ESP_LOGE(TAG, "Sin memoria para el JSON del escaneo #%" PRIu32,
                         s_result.scan_id);
            } else {
#if CONFIG_WARDRIVE_DEBUG_DUMP_JSON
                ESP_LOGI(TAG, "JSONL: %s", linea);
#endif
                free(linea);
            }
        }

        /* Periodo entre INICIOS (§2): absorbe los ~4-5 s del escaneo. */
        vTaskDelayUntil(&proximo_inicio,
                        pdMS_TO_TICKS(CONFIG_WARDRIVE_SCAN_INTERVAL_MS));
    }
}

esp_err_t scanner_start(void)
{
    BaseType_t ok = xTaskCreate(tarea_escaneo, "scanner", 4096, NULL, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
