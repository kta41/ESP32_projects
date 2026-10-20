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

#define WARDRIVE_MAX_PROBES  48
#define WARDRIVE_MAX_CLIENTS 48

/** Probe request capturada en modo promiscuo (móvil/portátil buscando red). */
typedef struct {
    uint8_t sta_mac[6]; /**< MAC del cliente que emite el Probe Request. */
    uint8_t ssid[33];   /**< SSID buscado (vacío = wildcard broadcast). */
    int8_t rssi;        /**< Mejor RSSI observado. */
    uint8_t channel;    /**< Canal donde se escuchó. */
    uint16_t count;     /**< Tramas recibidas en el ciclo. */
} promisc_probe_t;

/** Cliente (station) asociado a un BSSID observado en tramas de datos/gestión. */
typedef struct {
    uint8_t sta_mac[6]; /**< MAC de la estación cliente. */
    uint8_t bssid[6];   /**< BSSID del punto de acceso. */
    int8_t rssi;        /**< Mejor RSSI observado. */
    uint8_t channel;    /**< Canal donde se escuchó. */
    uint16_t frames;    /**< Número de tramas observadas en el ciclo. */
} promisc_client_t;

/** Resumen de captura promiscua 802.11 durante el ciclo. */
typedef struct {
    bool enabled;
    uint32_t mgmt_frames;
    uint32_t data_frames;
    uint32_t probe_req_frames;
    uint32_t deauth_frames;
    uint16_t num_probes;
    promisc_probe_t probes[WARDRIVE_MAX_PROBES];
    uint16_t num_clients;
    promisc_client_t clients[WARDRIVE_MAX_CLIENTS];
} promisc_summary_t;

/** Resultado crudo de un escaneo (metadatos + registros de AP + captura promiscua). */
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
    promisc_summary_t promisc; /**< Telemetría promiscua 802.11 del ciclo. */
} scan_result_t;

/** @brief Crea el task de escaneo pasivo (llamar una vez desde app_main). */
esp_err_t scanner_start(void);
