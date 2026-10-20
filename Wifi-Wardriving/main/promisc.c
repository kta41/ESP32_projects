/*
 * promisc — sniffer pasivo 802.11 para captura de Probe Requests, estaciones
 *           clientes (Data ToDS/FromDS) y tramas de desautenticación.
 */

#include "promisc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "pcap_store.h"
#include "wifi_mgr.h"

static const char *TAG = "PROMISC";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static promisc_summary_t s_acc;
static uint8_t s_own_mac[6] = {0};

static inline bool is_multicast_or_zero(const uint8_t mac[6])
{
    if ((mac[0] & 0x01) != 0) {
        return true; /* broadcast / multicast */
    }
    return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0;
}

static void record_probe_locked(const uint8_t sta[6], const uint8_t *ssid, uint8_t ssid_len,
                                int8_t rssi, uint8_t ch)
{
    if (ssid_len > 32) {
        ssid_len = 32;
    }

    for (uint16_t i = 0; i < s_acc.num_probes; i++) {
        promisc_probe_t *p = &s_acc.probes[i];
        if (memcmp(p->sta_mac, sta, 6) == 0 &&
            strlen((const char *)p->ssid) == ssid_len &&
            memcmp(p->ssid, ssid, ssid_len) == 0) {
            p->count++;
            if (rssi > p->rssi) {
                p->rssi = rssi;
                p->channel = ch;
            }
            return;
        }
    }

    if (s_acc.num_probes < WARDRIVE_MAX_PROBES) {
        promisc_probe_t *p = &s_acc.probes[s_acc.num_probes++];
        memcpy(p->sta_mac, sta, 6);
        memset(p->ssid, 0, sizeof(p->ssid));
        if (ssid_len > 0) {
            memcpy(p->ssid, ssid, ssid_len);
        }
        p->rssi = rssi;
        p->channel = ch;
        p->count = 1;
    }
}

static void record_client_locked(const uint8_t sta[6], const uint8_t bssid[6],
                                 int8_t rssi, uint8_t ch)
{
    for (uint16_t i = 0; i < s_acc.num_clients; i++) {
        promisc_client_t *c = &s_acc.clients[i];
        if (memcmp(c->sta_mac, sta, 6) == 0 && memcmp(c->bssid, bssid, 6) == 0) {
            c->frames++;
            if (rssi > c->rssi) {
                c->rssi = rssi;
                c->channel = ch;
            }
            return;
        }
    }

    if (s_acc.num_clients < WARDRIVE_MAX_CLIENTS) {
        promisc_client_t *c = &s_acc.clients[s_acc.num_clients++];
        memcpy(c->sta_mac, sta, 6);
        memcpy(c->bssid, bssid, 6);
        c->rssi = rssi;
        c->channel = ch;
        c->frames = 1;
    }
}

static void record_ap_pcap_locked(const uint8_t bssid[6], uint16_t trace_flag)
{
    if (trace_flag == 0 || is_multicast_or_zero(bssid)) {
        return;
    }
    for (uint16_t i = 0; i < s_acc.num_ap_pcap; i++) {
        if (memcmp(s_acc.ap_pcap[i].bssid, bssid, 6) == 0) {
            s_acc.ap_pcap[i].pcap_flags |= trace_flag;
            s_acc.ap_pcap[i].pcap_frames++;
            return;
        }
    }
    if (s_acc.num_ap_pcap < WARDRIVE_MAX_AP_PCAP) {
        promisc_ap_pcap_t *ap = &s_acc.ap_pcap[s_acc.num_ap_pcap++];
        memcpy(ap->bssid, bssid, 6);
        ap->pcap_flags = trace_flag;
        ap->pcap_frames = 1;
    }
}

static void record_sta_pcap_locked(const uint8_t sta[6], uint16_t trace_flag)
{
    if (trace_flag == 0 || is_multicast_or_zero(sta)) {
        return;
    }
    for (uint16_t i = 0; i < s_acc.num_clients; i++) {
        if (memcmp(s_acc.clients[i].sta_mac, sta, 6) == 0) {
            s_acc.clients[i].pcap_flags |= trace_flag;
        }
    }
    for (uint16_t i = 0; i < s_acc.num_probes; i++) {
        if (memcmp(s_acc.probes[i].sta_mac, sta, 6) == 0) {
            s_acc.probes[i].pcap_flags |= trace_flag;
        }
    }
}

static void wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == NULL || (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA)) {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *hdr = pkt->payload;
    unsigned len = pkt->rx_ctrl.sig_len;
    if (len < 24) {
        return;
    }

    int8_t rssi = pkt->rx_ctrl.rssi;
    uint8_t ch = pkt->rx_ctrl.channel;
    uint8_t fc0 = hdr[0];
    uint8_t fc1 = hdr[1];
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    const uint8_t *addr1 = hdr + 4;
    const uint8_t *addr2 = hdr + 10;
    const uint8_t *addr3 = hdr + 16;

    /* Excluir tramas donde intervenga nuestra propia MAC STA aleatoria. */
    if (memcmp(addr1, s_own_mac, 6) == 0 ||
        memcmp(addr2, s_own_mac, 6) == 0 ||
        memcmp(addr3, s_own_mac, 6) == 0) {
        return;
    }

    /* Excluir tramas donde intervenga el BSSID recordado del hotspot móvil. */
    uint8_t hs_bssid[6] = {0};
    if (wifi_mgr_get_hotspot_bssid(hs_bssid)) {
        if (memcmp(addr1, hs_bssid, 6) == 0 ||
            memcmp(addr2, hs_bssid, 6) == 0 ||
            memcmp(addr3, hs_bssid, 6) == 0) {
            return;
        }
    }

    /* El driver WiFi del ESP32 incluye 4 bytes de FCS al final de sig_len. */
    uint16_t payload_no_fcs = (len > 28) ? (uint16_t)(len - 4) : (uint16_t)len;
    bool skip_pcap = false;
    const uint8_t *frame_bssid = NULL;
    const uint8_t *frame_sta = NULL;

    portENTER_CRITICAL(&s_lock);

    if (type == WIFI_PKT_MGMT) {
        s_acc.mgmt_frames++;
        if (subtype == 0x04 || subtype == 0x05 || subtype == 0x08) {
            /* Probe Req (0x04), Probe Resp (0x05) o Beacon (0x08): extraer IE SSID.
             * En Probe Req los IEs arrancan en offset 24; en Beacon/Probe Resp en 36. */
            unsigned ie_offset = (subtype == 0x04) ? 24U : 36U;
            if (subtype == 0x04) {
                s_acc.probe_req_frames++;
                frame_sta = addr2;
            } else {
                frame_bssid = addr3;
                if (subtype == 0x05) {
                    frame_sta = addr1;
                }
            }
            if (payload_no_fcs > ie_offset) {
                const uint8_t *ies = hdr + ie_offset;
                unsigned ies_len = payload_no_fcs - ie_offset;
                const uint8_t *ssid_ptr = (const uint8_t *)"";
                uint8_t ssid_len = 0;
                unsigned pos = 0;
                while (pos + 2 <= ies_len) {
                    uint8_t tag = ies[pos];
                    uint8_t tlen = ies[pos + 1];
                    if (pos + 2 + tlen > ies_len) {
                        break;
                    }
                    if (tag == 0) {
                        ssid_ptr = &ies[pos + 2];
                        ssid_len = tlen;
                        break;
                    }
                    pos += 2 + tlen;
                }
                const char *own_ssid = CONFIG_WARDRIVE_WIFI_SSID;
                size_t own_ssid_len = strlen(own_ssid);
                if (own_ssid_len > 0 && ssid_len == own_ssid_len &&
                    memcmp(ssid_ptr, own_ssid, ssid_len) == 0) {
                    skip_pcap = true;
                } else if (subtype == 0x04 && !is_multicast_or_zero(addr2)) {
                    record_probe_locked(addr2, ssid_ptr, ssid_len, rssi, ch);
                }
            }
        } else if (subtype == 0x00 || subtype == 0x02) {
            /* Association Request (0x00) / Reassociation Request (0x02):
             * Addr1 = BSSID (AP), Addr2 = STA (cliente). */
            frame_bssid = addr1;
            frame_sta = addr2;
            if (!is_multicast_or_zero(addr2) && !is_multicast_or_zero(addr1)) {
                record_client_locked(addr2, addr1, rssi, ch);
            }
        } else if (subtype == 0x01 || subtype == 0x03) {
            /* Assoc Resp (0x01) / Reassoc Resp (0x03): Addr1 = STA, Addr3 = BSSID */
            frame_bssid = addr3;
            frame_sta = addr1;
        } else if (subtype == 0x0B) {
            /* Authentication (0x0B): Addr3 = BSSID. Si Addr1 == BSSID, Addr2 es STA. */
            frame_bssid = addr3;
            if (!is_multicast_or_zero(addr3)) {
                const uint8_t *sta = (memcmp(addr1, addr3, 6) == 0) ? addr2 : addr1;
                if (!is_multicast_or_zero(sta) && memcmp(sta, addr3, 6) != 0) {
                    frame_sta = sta;
                    record_client_locked(sta, addr3, rssi, ch);
                }
            }
        } else if (subtype == 0x0C || subtype == 0x0A || subtype == 0x0D) {
            /* Deauthentication (0x0C) / Disassociation (0x0A) / Action (0x0D): Addr3 = BSSID. */
            if (subtype == 0x0C || subtype == 0x0A) {
                s_acc.deauth_frames++;
            }
            frame_bssid = addr3;
            const uint8_t *sta = (memcmp(addr1, addr3, 6) == 0) ? addr2 : addr1;
            if (!is_multicast_or_zero(sta) && memcmp(sta, addr3, 6) != 0) {
                frame_sta = sta;
            }
        }
    } else if (type == WIFI_PKT_DATA) {
        s_acc.data_frames++;
        bool to_ds = (fc1 & 0x01) != 0;
        bool from_ds = (fc1 & 0x02) != 0;
        const uint8_t *sta = NULL;
        const uint8_t *bssid = NULL;

        if (to_ds && !from_ds) {
            /* Cliente -> AP: Addr1 = BSSID, Addr2 = STA */
            bssid = addr1;
            sta = addr2;
        } else if (!to_ds && from_ds) {
            /* AP -> Cliente: Addr1 = STA, Addr2 = BSSID */
            sta = addr1;
            bssid = addr2;
        }

        if (sta != NULL && bssid != NULL &&
            !is_multicast_or_zero(sta) && !is_multicast_or_zero(bssid)) {
            frame_bssid = bssid;
            frame_sta = sta;
            record_client_locked(sta, bssid, rssi, ch);
        }
    }

    portEXIT_CRITICAL(&s_lock);

    if (!skip_pcap) {
        uint16_t trace_flag = 0;
        if (pcap_store_record_frame(hdr, payload_no_fcs, rssi, ch, &trace_flag) && trace_flag != 0) {
            portENTER_CRITICAL(&s_lock);
            if (frame_bssid != NULL) {
                record_ap_pcap_locked(frame_bssid, trace_flag);
            }
            if (frame_sta != NULL) {
                record_sta_pcap_locked(frame_sta, trace_flag);
            }
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

esp_err_t promisc_init(void)
{
    esp_wifi_get_mac(WIFI_IF_STA, s_own_mac);
    pcap_store_init();

    portENTER_CRITICAL(&s_lock);
    memset(&s_acc, 0, sizeof(s_acc));
    s_acc.enabled = true;
    portEXIT_CRITICAL(&s_lock);

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filt);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_promiscuous(true);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sniffer promiscuo 802.11 activo (ProbeReq + Clientes + Deauth)");
    }
    return err;
}

void promisc_collect_and_reset(promisc_summary_t *out)
{
    if (out == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    *out = s_acc;
    memset(&s_acc, 0, sizeof(s_acc));
    s_acc.enabled = true;
    portEXIT_CRITICAL(&s_lock);

    pcap_cycle_stats_t pcap_st = {0};
    pcap_store_collect_cycle_stats(&pcap_st);
    out->eapol_frames = pcap_st.eapol_frames;
    out->pcap_captured_frames = pcap_st.captured_frames;
    out->pcap_dropped_frames = pcap_st.dropped_frames;
    out->pcap_captured_bytes = pcap_st.captured_bytes;

    /* Filtrar cualquier referencia al hotspot propio (por SSID o por BSSID). */
    uint8_t hs_bssid[6] = {0};
    bool has_hs_bssid = wifi_mgr_get_hotspot_bssid(hs_bssid);
    wifi_ap_record_t ap_actual;
    bool has_ap_actual = (esp_wifi_sta_get_ap_info(&ap_actual) == ESP_OK);
    const char *hs_ssid = CONFIG_WARDRIVE_WIFI_SSID;

    uint16_t dst_p = 0;
    for (uint16_t i = 0; i < out->num_probes; i++) {
        if (hs_ssid[0] != '\0' &&
            strncmp((const char *)out->probes[i].ssid, hs_ssid, 32) == 0) {
            continue;
        }
        if (dst_p != i) {
            out->probes[dst_p] = out->probes[i];
        }
        dst_p++;
    }
    out->num_probes = dst_p;

    uint16_t dst_c = 0;
    for (uint16_t i = 0; i < out->num_clients; i++) {
        const uint8_t *b = out->clients[i].bssid;
        const uint8_t *s = out->clients[i].sta_mac;
        if ((has_hs_bssid && (memcmp(b, hs_bssid, 6) == 0 || memcmp(s, hs_bssid, 6) == 0)) ||
            (has_ap_actual && (memcmp(b, ap_actual.bssid, 6) == 0 || memcmp(s, ap_actual.bssid, 6) == 0))) {
            continue;
        }
        if (dst_c != i) {
            out->clients[dst_c] = out->clients[i];
        }
        dst_c++;
    }
    out->num_clients = dst_c;

    uint16_t dst_a = 0;
    for (uint16_t i = 0; i < out->num_ap_pcap; i++) {
        const uint8_t *b = out->ap_pcap[i].bssid;
        if ((has_hs_bssid && memcmp(b, hs_bssid, 6) == 0) ||
            (has_ap_actual && memcmp(b, ap_actual.bssid, 6) == 0)) {
            continue;
        }
        if (dst_a != i) {
            out->ap_pcap[dst_a] = out->ap_pcap[i];
        }
        dst_a++;
    }
    out->num_ap_pcap = dst_a;
}
