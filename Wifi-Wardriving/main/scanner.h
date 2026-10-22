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

#include "ble_fusion.h"
#include "esp_err.h"
#include "esp_wifi.h"

#define WARDRIVE_MAX_PROBES  48
#define WARDRIVE_MAX_CLIENTS 48
#define WARDRIVE_MAX_AP_PCAP 64

/* Máscara de bits de tipos de tramas capturadas en PCAP por entidad (AP o STA). */
#define PCAP_TRACE_BEACON      (1U << 0)
#define PCAP_TRACE_PROBE_REQ   (1U << 1)
#define PCAP_TRACE_PROBE_RESP  (1U << 2)
#define PCAP_TRACE_ASSOC       (1U << 3)
#define PCAP_TRACE_AUTH        (1U << 4)
#define PCAP_TRACE_DEAUTH      (1U << 5)
#define PCAP_TRACE_ACTION      (1U << 6)
#define PCAP_TRACE_EAPOL       (1U << 7)
#define PCAP_TRACE_DATA        (1U << 8)

/** Probe request capturada en modo promiscuo (móvil/portátil buscando red). */
typedef struct {
    uint8_t sta_mac[6]; /**< MAC del cliente que emite el Probe Request. */
    uint8_t ssid[33];   /**< SSID buscado (vacío = wildcard broadcast). */
    int8_t rssi;        /**< Mejor RSSI observado. */
    uint8_t channel;    /**< Canal donde se escuchó. */
    uint16_t count;     /**< Tramas recibidas en el ciclo. */
    uint16_t pcap_flags;/**< Máscara PCAP_TRACE_* capturada en PCAP para esta STA. */
} promisc_probe_t;

/** Cliente (station) asociado a un BSSID observado en tramas de datos/gestión. */
typedef struct {
    uint8_t sta_mac[6]; /**< MAC de la estación cliente. */
    uint8_t bssid[6];   /**< BSSID del punto de acceso. */
    int8_t rssi;        /**< Mejor RSSI observado. */
    uint8_t channel;    /**< Canal donde se escuchó. */
    uint16_t frames;    /**< Número de tramas observadas en el ciclo. */
    uint16_t pcap_flags;/**< Máscara PCAP_TRACE_* capturada en PCAP para esta STA. */
} promisc_client_t;

/** Trazas PCAP registradas para un BSSID específico durante el ciclo. */
typedef struct {
    uint8_t bssid[6];
    uint16_t pcap_flags;
    uint16_t pcap_frames;
} promisc_ap_pcap_t;

/** Resumen de captura promiscua 802.11 durante el ciclo. */
typedef struct {
    bool enabled;
    uint32_t mgmt_frames;
    uint32_t data_frames;
    uint32_t probe_req_frames;
    uint32_t deauth_frames;
    uint32_t eapol_frames;
    uint32_t pcap_captured_frames;
    uint32_t pcap_dropped_frames;
    uint32_t pcap_captured_bytes;
    uint16_t num_probes;
    promisc_probe_t probes[WARDRIVE_MAX_PROBES];
    uint16_t num_clients;
    promisc_client_t clients[WARDRIVE_MAX_CLIENTS];
    uint16_t num_ap_pcap;
    promisc_ap_pcap_t ap_pcap[WARDRIVE_MAX_AP_PCAP];
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
    ble_fusion_summary_t ble;  /**< Resumen BLE del ciclo (fusión BLE-Recon, P2). */
} scan_result_t;

/** @brief Crea el task de escaneo pasivo (llamar una vez desde app_main). */
esp_err_t scanner_start(void);
