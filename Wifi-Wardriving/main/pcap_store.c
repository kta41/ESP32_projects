/*
 * pcap_store — Buffer en RAM de registros PCAP + Radiotap (DLT 127) para
 *              tramas 802.11 de Gestión (MGMT) y Handshakes WPA (EAPOL).
 */

#include "pcap_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "scanner.h"
#include "sdkconfig.h"

#ifndef CONFIG_WARDRIVE_PCAP_ENABLE
#define CONFIG_WARDRIVE_PCAP_ENABLE 1
#endif

#ifndef CONFIG_WARDRIVE_PCAP_BUF_KB
#define CONFIG_WARDRIVE_PCAP_BUF_KB 16
#endif

#ifndef CONFIG_WARDRIVE_PCAP_SNAPLEN
#define CONFIG_WARDRIVE_PCAP_SNAPLEN 512
#endif

#define PCAP_BUF_BYTES        ((size_t)CONFIG_WARDRIVE_PCAP_BUF_KB * 1024U)
#define MAX_BEACON_DEDUP      64

/* Cabecera de registro PCAP estándar (16 bytes, little-endian). */
typedef struct __attribute__((packed)) {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_rec_hdr_t;

/*
 * Cabecera Radiotap compacta (12 bytes, little-endian, DLT 127):
 *   - it_present = (1 << 3) [CHANNEL] | (1 << 5) [DBM_ANTSIGNAL] = 0x00000028
 *   - Offset 8 : uint16_t chan_freq (MHz, alineado a 2 bytes)
 *   - Offset 10: uint16_t chan_flags (alineado a 2 bytes)
 *   - Offset 12: int8_t   antenna_signal (dBm)
 *   - Offset 13: uint8_t  padding (para cerrar en 14 bytes, múltiplo de 2)
 */
typedef struct __attribute__((packed)) {
    uint8_t  it_version;     /* 0 */
    uint8_t  it_pad;         /* 0 */
    uint16_t it_len;         /* sizeof(radiotap_hdr_t) == 14 */
    uint32_t it_present;     /* 0x00000028: CHANNEL (bit 3) | DBM_ANTSIGNAL (bit 5) */
    uint16_t chan_freq;      /* MHz (ej. 2412 para ch 1) */
    uint16_t chan_flags;     /* 0x00a0 = CCK+2GHz, 0x0140 = OFDM+2GHz */
    int8_t   dbm_antsignal;  /* RSSI en dBm */
    uint8_t  pad_end;        /* 0 */
} radiotap_hdr_t;

static portMUX_TYPE s_pcap_lock = portMUX_INITIALIZER_UNLOCKED;

/* Buffer estático en BSS para cero fragmentación de heap en el callback WiFi. */
static uint8_t s_pcap_buf[PCAP_BUF_BYTES];
static size_t s_pcap_used = 0;
static uint32_t s_pcap_pkts = 0;

/* Deduplicación de Beacons por BSSID dentro de un mismo ciclo de escaneo. */
static uint8_t s_beacon_bssids[MAX_BEACON_DEDUP][6];
static uint16_t s_beacon_count = 0;

/* Estadísticas del ciclo actual. */
static pcap_cycle_stats_t s_cycle_stats;

static uint16_t channel_to_freq_mhz(uint8_t ch)
{
    if (ch >= 1 && ch <= 13) {
        return (uint16_t)(2407 + ch * 5);
    }
    if (ch == 14) {
        return 2484;
    }
    return 2412;
}

/**
 * @brief Detecta si una trama de datos 802.11 contiene un paquete EAPOL
 *        (LLC/SNAP AA-AA-03 + OUI 00-00-00 + EtherType 0x888E).
 */
static bool is_eapol_data_frame(const uint8_t *hdr, uint16_t len)
{
    if (len < 32) {
        return false;
    }
    uint8_t fc0 = hdr[0];
    uint8_t fc1 = hdr[1];
    uint8_t type = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    if (type != 0x02) {
        return false; /* No es WIFI_PKT_DATA */
    }
    /* Subtipos sin payload de datos (Null data = 0x04, QoS Null = 0x0C, etc.). */
    if ((subtype & 0x04) != 0) {
        return false;
    }

    /* Trama protegida a nivel MAC: solo si ya está cifrada no veremos LLC en claro,
     * pero los mensajes M1 y M2 del 4-Way Handshake inicial viajan sin cifrar
     * (Protected bit = 0) y M3/M4 en rekeying o ciertas pilas pueden variar;
     * comprobamos siempre la firma LLC/SNAP EAPOL. */
    size_t mac_hdr_len = 24;
    bool to_ds = (fc1 & 0x01) != 0;
    bool from_ds = (fc1 & 0x02) != 0;
    if (to_ds && from_ds) {
        mac_hdr_len += 6; /* WDS 4 direcciones */
    }
    if ((subtype & 0x08) != 0) {
        mac_hdr_len += 2; /* QoS Control */
        if ((fc1 & 0x80) != 0) {
            mac_hdr_len += 4; /* +HTC */
        }
    }

    if (len < mac_hdr_len + 8) {
        return false;
    }

    const uint8_t *llc = hdr + mac_hdr_len;
    /* LLC SNAP: DSAP=0xAA, SSAP=0xAA, Control=0x03, OUI=00:00:00, Type=0x888E */
    return (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
            llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00 &&
            llc[6] == 0x88 && llc[7] == 0x8E);
}

static bool should_keep_mgmt_locked(uint8_t subtype, const uint8_t *hdr)
{
    switch (subtype) {
        case 0x08: { /* Beacon: guardar como máximo 1 por BSSID (Addr3) en cada ciclo */
            const uint8_t *bssid = hdr + 16;
            for (uint16_t i = 0; i < s_beacon_count; i++) {
                if (memcmp(s_beacon_bssids[i], bssid, 6) == 0) {
                    return false;
                }
            }
            if (s_beacon_count < MAX_BEACON_DEDUP) {
                memcpy(s_beacon_bssids[s_beacon_count++], bssid, 6);
                return true;
            }
            return false;
        }
        case 0x00: /* Association Request */
        case 0x01: /* Association Response */
        case 0x02: /* Reassociation Request */
        case 0x03: /* Reassociation Response */
        case 0x04: /* Probe Request */
        case 0x05: /* Probe Response */
        case 0x0A: /* Disassociation */
        case 0x0B: /* Authentication */
        case 0x0C: /* Deauthentication */
        case 0x0D: /* Action */
            return true;
        default:
            return false;
    }
}

static uint16_t classify_mgmt_trace_flag(uint8_t subtype)
{
    switch (subtype) {
        case 0x08:
            return PCAP_TRACE_BEACON;
        case 0x04:
            return PCAP_TRACE_PROBE_REQ;
        case 0x05:
            return PCAP_TRACE_PROBE_RESP;
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            return PCAP_TRACE_ASSOC;
        case 0x0B:
            return PCAP_TRACE_AUTH;
        case 0x0A:
        case 0x0C:
            return PCAP_TRACE_DEAUTH;
        case 0x0D:
            return PCAP_TRACE_ACTION;
        default:
            return 0;
    }
}

void pcap_store_init(void)
{
    portENTER_CRITICAL(&s_pcap_lock);
    s_pcap_used = 0;
    s_pcap_pkts = 0;
    s_beacon_count = 0;
    memset(&s_cycle_stats, 0, sizeof(s_cycle_stats));
    portEXIT_CRITICAL(&s_pcap_lock);
}

bool pcap_store_record_frame(const uint8_t *payload, uint16_t len,
                             int8_t rssi, uint8_t channel, uint16_t *out_trace_flag)
{
    if (out_trace_flag != NULL) {
        *out_trace_flag = 0;
    }
#if !CONFIG_WARDRIVE_PCAP_ENABLE
    return false;
#endif
    if (payload == NULL || len < 24) {
        return false;
    }

    uint8_t fc0 = payload[0];
    uint8_t type = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    bool is_eapol = false;
    uint16_t trace_flag = 0;
    if (type == 0x02) {
        is_eapol = is_eapol_data_frame(payload, len);
        if (!is_eapol) {
            return false; /* Opción A: de DATA solo capturamos handshakes EAPOL */
        }
        trace_flag = PCAP_TRACE_EAPOL;
    } else if (type == 0x00) {
        trace_flag = classify_mgmt_trace_flag(subtype);
        if (trace_flag == 0) {
            return false;
        }
    } else {
        return false;
    }

    uint16_t snap_len = (len > CONFIG_WARDRIVE_PCAP_SNAPLEN)
                            ? (uint16_t)CONFIG_WARDRIVE_PCAP_SNAPLEN
                            : len;
    size_t total_rec_bytes = sizeof(pcap_rec_hdr_t) + sizeof(radiotap_hdr_t) + snap_len;

    int64_t now_us = esp_timer_get_time();
    uint32_t ts_sec = (uint32_t)(now_us / 1000000LL);
    uint32_t ts_usec = (uint32_t)(now_us % 1000000LL);

    radiotap_hdr_t rtap = {
        .it_version = 0,
        .it_pad = 0,
        .it_len = sizeof(radiotap_hdr_t),
        .it_present = 0x00000028U, /* CHANNEL | DBM_ANTSIGNAL */
        .chan_freq = channel_to_freq_mhz(channel),
        .chan_flags = 0x0080U,     /* 2 GHz spectrum */
        .dbm_antsignal = rssi,
        .pad_end = 0,
    };

    pcap_rec_hdr_t rec = {
        .ts_sec = ts_sec,
        .ts_usec = ts_usec,
        .incl_len = (uint32_t)(sizeof(radiotap_hdr_t) + snap_len),
        .orig_len = (uint32_t)(sizeof(radiotap_hdr_t) + len),
    };

    portENTER_CRITICAL(&s_pcap_lock);

    if (type == 0x00 && !should_keep_mgmt_locked(subtype, payload)) {
        portEXIT_CRITICAL(&s_pcap_lock);
        return false;
    }

    if (is_eapol) {
        s_cycle_stats.eapol_frames++;
    }

    if (s_pcap_used + total_rec_bytes > PCAP_BUF_BYTES) {
        s_cycle_stats.dropped_frames++;
        portEXIT_CRITICAL(&s_pcap_lock);
        return false;
    }

    memcpy(s_pcap_buf + s_pcap_used, &rec, sizeof(rec));
    s_pcap_used += sizeof(rec);
    memcpy(s_pcap_buf + s_pcap_used, &rtap, sizeof(rtap));
    s_pcap_used += sizeof(rtap);
    memcpy(s_pcap_buf + s_pcap_used, payload, snap_len);
    s_pcap_used += snap_len;

    s_pcap_pkts++;
    s_cycle_stats.captured_frames++;
    s_cycle_stats.captured_bytes += (uint32_t)total_rec_bytes;

    portEXIT_CRITICAL(&s_pcap_lock);
    if (out_trace_flag != NULL) {
        *out_trace_flag = trace_flag;
    }
    return true;
}

void pcap_store_collect_cycle_stats(pcap_cycle_stats_t *out)
{
    portENTER_CRITICAL(&s_pcap_lock);
    if (out != NULL) {
        *out = s_cycle_stats;
    }
    memset(&s_cycle_stats, 0, sizeof(s_cycle_stats));
    s_beacon_count = 0; /* permitir 1 Beacon por BSSID en el nuevo ciclo */
    portEXIT_CRITICAL(&s_pcap_lock);
}

size_t pcap_store_pending_bytes(void)
{
    portENTER_CRITICAL(&s_pcap_lock);
    size_t used = s_pcap_used;
    portEXIT_CRITICAL(&s_pcap_lock);
    return used;
}

uint32_t pcap_store_pending_packets(void)
{
    portENTER_CRITICAL(&s_pcap_lock);
    uint32_t pkts = s_pcap_pkts;
    portEXIT_CRITICAL(&s_pcap_lock);
    return pkts;
}

bool pcap_store_snapshot(uint8_t **out_buf, size_t *out_len, uint32_t *out_pkts)
{
    if (out_buf == NULL || out_len == NULL || out_pkts == NULL) {
        return false;
    }
    *out_buf = NULL;
    *out_len = 0;
    *out_pkts = 0;

    size_t need = pcap_store_pending_bytes();
    if (need == 0) {
        return true;
    }

    /* Reservar exactamente los bytes pendientes + un pequeño margen de 1 KB
     * por si entra una trama entre la lectura y la sección crítica. */
    size_t alloc_sz = need + 1024;
    if (alloc_sz > PCAP_BUF_BYTES) {
        alloc_sz = PCAP_BUF_BYTES;
    }
    uint8_t *tmp = malloc(alloc_sz);
    if (tmp == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_pcap_lock);
    size_t copy_len = (s_pcap_used <= alloc_sz) ? s_pcap_used : need;
    uint32_t copy_pkts = s_pcap_pkts;
    if (copy_len > 0) {
        memcpy(tmp, s_pcap_buf, copy_len);
    }
    portEXIT_CRITICAL(&s_pcap_lock);

    if (copy_len == 0) {
        free(tmp);
        return true;
    }

    *out_buf = tmp;
    *out_len = copy_len;
    *out_pkts = copy_pkts;
    return true;
}

void pcap_store_consume(size_t bytes, uint32_t pkts)
{
    if (bytes == 0) {
        return;
    }
    portENTER_CRITICAL(&s_pcap_lock);
    if (bytes >= s_pcap_used) {
        s_pcap_used = 0;
        s_pcap_pkts = 0;
    } else {
        memmove(s_pcap_buf, s_pcap_buf + bytes, s_pcap_used - bytes);
        s_pcap_used -= bytes;
        s_pcap_pkts = (s_pcap_pkts > pkts) ? (s_pcap_pkts - pkts) : 0;
    }
    portEXIT_CRITICAL(&s_pcap_lock);
}
