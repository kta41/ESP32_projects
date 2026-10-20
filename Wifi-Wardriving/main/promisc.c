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

    portENTER_CRITICAL(&s_lock);

    if (type == WIFI_PKT_MGMT) {
        s_acc.mgmt_frames++;
        if (subtype == 0x04) {
            /* Probe Request: Addr2 = STA emisora, IEs comienzan en offset 24. */
            s_acc.probe_req_frames++;
            if (!is_multicast_or_zero(addr2) && len >= 26) {
                const uint8_t *ies = hdr + 24;
                unsigned ies_len = (len > 28) ? (len - 28) : (len - 24);
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
                record_probe_locked(addr2, ssid_ptr, ssid_len, rssi, ch);
            }
        } else if (subtype == 0x0C || subtype == 0x0A) {
            /* Deauthentication (0x0C) / Disassociation (0x0A). */
            s_acc.deauth_frames++;
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
            record_client_locked(sta, bssid, rssi, ch);
        }
    }

    portEXIT_CRITICAL(&s_lock);
}

esp_err_t promisc_init(void)
{
    esp_wifi_get_mac(WIFI_IF_STA, s_own_mac);

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
}
