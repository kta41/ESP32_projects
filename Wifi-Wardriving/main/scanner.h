#pragma once

/*
 * scanner — task de escaneo PASIVO (ROADMAP §3.3) con ciclo entre inicios.
 *
 * Aquí se define scan_result_t: el registro CRUDO de un escaneo (§3.6,
 * sin strings JSON) que consumen jsonl_writer (serialización, §3.5) y,
 * en la Fase 3, scan_store (ring buffer).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

/** Resultado crudo de un escaneo (metadatos + registros de AP). */
typedef struct {
    uint32_t scan_id;        /**< Contador monótono por sesión (§3.4). */
    int64_t ts_us;           /**< esp_timer_get_time() al completar el escaneo. */
    uint32_t duration_ms;    /**< Duración real del escaneo (auditoría). */
    uint16_t total_detected; /**< APs vistos (BSSID propio excluido) antes del tope. */
    uint16_t stored;         /**< APs incluidos en records[]. */
    uint16_t dropped;        /**< total_detected - stored (recorte por tope 128). */
    uint32_t lost_before;    /**< Escaneos enteros descartados por ring lleno (Fase 3). */
    bool buffered;           /**< Sufrió un push fallido antes de enviarse (Fase 3). */
    uint16_t num_records;    /**< == stored. */
    wifi_ap_record_t records[CONFIG_WARDRIVE_MAX_APS];
} scan_result_t;

/** @brief Crea el task de escaneo pasivo (llamar una vez desde app_main). */
esp_err_t scanner_start(void);
