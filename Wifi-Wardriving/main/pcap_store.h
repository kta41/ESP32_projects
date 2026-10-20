#pragma once

/*
 * pcap_store — Captura selectiva de tramas 802.11 en formato PCAP + Radiotap
 *              (DLT_IEEE802_11_RADIO = 127) en memoria RAM (Opción A: MGMT + EAPOL).
 *
 * - Captura tramas de gestión valiosas (Beacon deduplicado por BSSID/ciclo,
 *   Probe Req/Resp, Assoc/Reassoc, Auth, Deauth/Disassoc, Action) y tramas
 *   de datos de negociación de claves WPA/WPA2/WPA3 (EAPOL EtherType 0x888E).
 * - Antepone una cabecera Radiotap compacta de 12 bytes (Channel freq + flags +
 *   dBm signal) y el Packet Record Header PCAP de 16 bytes.
 * - Almacena los registros en un buffer lineal estático protegido por spinlock,
 *   listo para enviarse por HTTP POST /pcap al final de cada ciclo de escaneo.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Estadísticas de captura PCAP acumuladas durante un ciclo de escaneo. */
typedef struct {
    uint32_t captured_frames; /**< Paquetes escritos en el buffer PCAP en el ciclo. */
    uint32_t dropped_frames;  /**< Paquetes descartados por buffer lleno en el ciclo. */
    uint32_t eapol_frames;    /**< Tramas EAPOL (handshakes WPA) capturadas en el ciclo. */
    uint32_t captured_bytes;  /**< Bytes acumulados en el buffer PCAP en el ciclo. */
} pcap_cycle_stats_t;

/** @brief Inicializa el subsistema de almacenamiento PCAP en RAM. */
void pcap_store_init(void);

/**
 * @brief Registra una trama 802.11 cruda si cumple el filtro MGMT + EAPOL.
 *
 * Debe invocarse únicamente después de haber excluido la identidad propia
 * (MAC del ESP32 y BSSID/SSID del hotspot móvil).
 *
 * @param payload        Puntero al inicio de la cabecera MAC 802.11 (sin FCS).
 * @param len            Longitud real del payload 802.11 en bytes (sin los 4B de FCS).
 * @param rssi           RSSI en dBm reportado por rx_ctrl.
 * @param channel        Canal primario (1..14) reportado por rx_ctrl.
 * @param out_trace_flag Recibe el bit PCAP_TRACE_* si la trama fue almacenada (0 si no).
 * @return true si la trama fue almacenada en el buffer PCAP.
 */
bool pcap_store_record_frame(const uint8_t *payload, uint16_t len,
                             int8_t rssi, uint8_t channel, uint16_t *out_trace_flag);

/**
 * @brief Recoge y pone a cero los contadores del ciclo actual (y limpia la
 *        tabla de deduplicación de Beacons para el siguiente ciclo).
 */
void pcap_store_collect_cycle_stats(pcap_cycle_stats_t *out);

/** @brief Número de bytes pendientes de envío en el buffer PCAP. */
size_t pcap_store_pending_bytes(void);

/** @brief Número de paquetes pendientes de envío en el buffer PCAP. */
uint32_t pcap_store_pending_packets(void);

/**
 * @brief Copia los bytes pendientes a un buffer dinámico para su envío HTTP.
 *
 * El llamador debe liberar `*out_buf` con `free()`. Si el envío HTTP tiene
 * éxito, debe invocarse `pcap_store_consume(copied_len, copied_pkts)` para
 * drenar los datos ya confirmados.
 */
bool pcap_store_snapshot(uint8_t **out_buf, size_t *out_len, uint32_t *out_pkts);

/** @brief Drena los primeros `bytes` (y `pkts`) ya confirmados por el listener. */
void pcap_store_consume(size_t bytes, uint32_t pkts);
