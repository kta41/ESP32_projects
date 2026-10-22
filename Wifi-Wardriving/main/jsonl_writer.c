/*
 * jsonl_writer — payload JSONL v1 (ROADMAP §3.5) [CONGELADO].
 *
 * Evolución de authmode_to_str / bssid_to_str / build_scan_json del
 * baseline: los mapeos se extienden a TODOS los enums de IDF v6.1 y se
 * añade build_scan_json_v1, la serialización oficial del sensor.
 *
 * Nota: los nombres de cipher de v6.1 difieren de los históricos (hay
 * SMS4/GCMP/GCMP256/AES_GMAC128/AES_GMAC256); se sigue la convención de
 * §3.5 ("AES_CMAC_128"...) y el fallback UNKNOWN_%d cubre enums futuros.
 */

#include "jsonl_writer.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} jbuf_t;

static void jbuf_raw(jbuf_t *jb, const char *s, size_t n)
{
    if (jb->buf != NULL && jb->len + n < jb->cap) {
        memcpy(jb->buf + jb->len, s, n);
        jb->buf[jb->len + n] = '\0';
    }
    jb->len += n;
}

static void jbuf_printf(jbuf_t *jb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void jbuf_printf(jbuf_t *jb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t avail = (jb->buf != NULL && jb->len < jb->cap) ? (jb->cap - jb->len) : 0;
    int n = vsnprintf(avail > 0 ? jb->buf + jb->len : NULL, avail, fmt, ap);
    va_end(ap);
    if (n > 0) {
        jb->len += (size_t)n;
    }
}

static void jbuf_str(jbuf_t *jb, const char *s)
{
    jbuf_raw(jb, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            char esc[2] = {'\\', (char)c};
            jbuf_raw(jb, esc, 2);
        } else if (c < 0x20) {
            jbuf_printf(jb, "\\u%04x", (unsigned)c);
        } else {
            char ch = (char)c;
            jbuf_raw(jb, &ch, 1);
        }
    }
    jbuf_raw(jb, "\"", 1);
}

/* ------------------------- Mapeos a texto (§3.5) ------------------------- */

const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA_ENTERPRISE:
            return "WPA_ENTERPRISE";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
            return "WPA2_WPA3_ENTERPRISE";
        case WIFI_AUTH_WPA3_ENTERPRISE:
            return "WPA3_ENTERPRISE";
        case WIFI_AUTH_WPA3_ENT_192:
            return "WPA3_ENT_192";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        case WIFI_AUTH_DPP:
            return "DPP";
        case WIFI_AUTH_OWE:
            return "OWE";
        case WIFI_AUTH_UNKNOWN:
            return "UNKNOWN";
        default:
            break;
    }
    /* Fallback §3.5: UNKNOWN_<valor>. Buffer estático: la serialización
     * solo ocurre desde el task de escaneo/push (un hilo a la vez). */
    static char desconocido[24];
    snprintf(desconocido, sizeof(desconocido), "UNKNOWN_%d", (int)authmode);
    return desconocido;
}

const char *cipher_to_str(wifi_cipher_type_t cipher)
{
    switch (cipher) {
        case WIFI_CIPHER_TYPE_NONE:
            return "NONE";
        case WIFI_CIPHER_TYPE_WEP40:
            return "WEP40";
        case WIFI_CIPHER_TYPE_WEP104:
            return "WEP104";
        case WIFI_CIPHER_TYPE_TKIP:
            return "TKIP";
        case WIFI_CIPHER_TYPE_CCMP:
            return "CCMP";
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            return "TKIP_CCMP";
        case WIFI_CIPHER_TYPE_AES_CMAC128:
            return "AES_CMAC_128";
        case WIFI_CIPHER_TYPE_SMS4:
            return "SMS4";
        case WIFI_CIPHER_TYPE_GCMP:
            return "GCMP";
        case WIFI_CIPHER_TYPE_GCMP256:
            return "GCMP_256";
        case WIFI_CIPHER_TYPE_AES_GMAC128:
            return "AES_GMAC_128";
        case WIFI_CIPHER_TYPE_AES_GMAC256:
            return "AES_GMAC_256";
        case WIFI_CIPHER_TYPE_UNKNOWN:
            return "UNKNOWN";
        default:
            break;
    }
    static char desconocido[24];
    snprintf(desconocido, sizeof(desconocido), "UNKNOWN_%d", (int)cipher);
    return desconocido;
}

void bssid_to_str(const uint8_t bssid[6], char *out, size_t out_len)
{
    if (out_len < 18) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

/*
 * Sanitización de SSID (§3.5): bytes imprimibles ASCII (32-126) se
 * conservan; el resto se sustituye por '?'. Si hubo sustitución, out_hex
 * recibe los bytes ORIGINALES en hexadecimal. El JSON resultante es
 * siempre UTF-8 válido (solo ASCII).
 *
 * @param longitud_original  longitud real del SSID (0 → red oculta).
 * @return true si se sustituyó algún byte (→ añadir ssid_hex).
 */
static bool sanitizar_ssid(const uint8_t ssid[33], char out_limpio[33],
                           char out_hex[65], size_t *longitud_original)
{
    static const char hex[] = "0123456789ABCDEF";

    size_t n = 0;
    while (n < 32 && ssid[n] != 0) {
        n++;
    }

    bool sustituido = false;
    for (size_t i = 0; i < n; i++) {
        bool imprimible = (ssid[i] >= 32 && ssid[i] <= 126);
        out_limpio[i] = imprimible ? (char)ssid[i] : '?';
        if (!imprimible) {
            sustituido = true;
        }
        out_hex[i * 2] = hex[ssid[i] >> 4];
        out_hex[i * 2 + 1] = hex[ssid[i] & 0x0F];
    }
    out_limpio[n] = '\0';
    out_hex[n * 2] = '\0';

    *longitud_original = n;
    return sustituido;
}

/* --------------------- Dispositivo (AP) → objeto JSON --------------------- */

static void emit_pcap_traces_array(jbuf_t *jb, const char *key, uint16_t flags)
{
    jbuf_printf(jb, ",\"%s\":[", key);
    static const struct {
        uint16_t bit;
        const char *name;
    } k_traces[] = {
        {PCAP_TRACE_EAPOL,      "EAPOL"},
        {PCAP_TRACE_DEAUTH,     "DEAUTH"},
        {PCAP_TRACE_ASSOC,      "ASSOC"},
        {PCAP_TRACE_AUTH,       "AUTH"},
        {PCAP_TRACE_PROBE_REQ,  "PROBE_REQ"},
        {PCAP_TRACE_PROBE_RESP, "PROBE_RESP"},
        {PCAP_TRACE_ACTION,     "ACTION"},
        {PCAP_TRACE_BEACON,     "BEACON"},
    };
    bool first = true;
    for (size_t i = 0; i < sizeof(k_traces) / sizeof(k_traces[0]); i++) {
        if ((flags & k_traces[i].bit) != 0) {
            jbuf_printf(jb, "%s\"%s\"", first ? "" : ",", k_traces[i].name);
            first = false;
        }
    }
    jbuf_raw(jb, "]", 1);
}

static void emit_device_json(jbuf_t *jb, const wardrive_ap_record_t *ap)
{
    char ssid_limpio[33];
    char ssid_hex[65];
    size_t longitud = 0;
    bool sustituido = sanitizar_ssid(ap->ssid, ssid_limpio, ssid_hex, &longitud);

    char bssid[18] = {0};
    bssid_to_str(ap->bssid, bssid, sizeof(bssid));

    char proto[4];
    size_t p = 0;
    if (ap->phy_11b) {
        proto[p++] = 'b';
    }
    if (ap->phy_11g) {
        proto[p++] = 'g';
    }
    if (ap->phy_11n) {
        proto[p++] = 'n';
    }
    proto[p] = '\0';

    jbuf_raw(jb, "{\"ssid\":", 8);
    jbuf_str(jb, ssid_limpio);
    if (sustituido) {
        jbuf_printf(jb, ",\"ssid_hex\":\"%s\"", ssid_hex);
    }
    jbuf_printf(jb,
                ",\"bssid\":\"%s\",\"rssi\":%d,\"primary_channel\":%u,\"secondary_channel\":%u,"
                "\"auth_mode\":\"%s\",\"pairwise_cipher\":\"%s\",\"group_cipher\":\"%s\","
                "\"proto\":\"%s\"",
                bssid, (int)ap->rssi, (unsigned)ap->primary, (unsigned)ap->second,
                authmode_to_str((wifi_auth_mode_t)ap->authmode),
                cipher_to_str((wifi_cipher_type_t)ap->pairwise_cipher),
                cipher_to_str((wifi_cipher_type_t)ap->group_cipher),
                proto);
    if (ap->phy_lr) {
        jbuf_raw(jb, ",\"lr\":true", 10);
    }
    jbuf_printf(jb, ",\"band_mhz\":%d,\"wps\":%s,\"hidden\":%s}",
                ap->second == WIFI_SECOND_CHAN_NONE ? 20 : 40,
                ap->wps ? "true" : "false",
                longitud == 0 ? "true" : "false");
}

/* ------------------------ Payload JSONL v1 (§3.5) ------------------------ */

static void emit_scan_json_v1(jbuf_t *jb, const scan_result_t *res, const char *session_id)
{
    jbuf_printf(jb,
                "{\"v\":1,\"mode\":\"scan\",\"scan_type\":\"passive\",\"session_id\":\"%s\","
                "\"scan_id\":%" PRIu32 ",\"device_ts_us\":%" PRId64 ",\"scan_duration_ms\":%" PRIu32 ","
                "\"total_detected\":%u,\"stored\":%u,\"dropped\":%u,"
                "\"lost_before\":%" PRIu32 ",\"buffered\":%s,\"devices\":[",
                session_id, res->scan_id, res->ts_us, res->duration_ms,
                (unsigned)res->total_detected, (unsigned)res->stored, (unsigned)res->dropped,
                res->lost_before, res->buffered ? "true" : "false");

    for (uint16_t i = 0; i < res->num_records; i++) {
        if (i > 0) {
            jbuf_raw(jb, ",", 1);
        }
        emit_device_json(jb, &res->records[i]);
    }
    jbuf_raw(jb, "]", 1);

    if (res->promisc.enabled) {
        jbuf_printf(jb,
                    ",\"promisc\":{\"mgmt_frames\":%" PRIu32 ",\"data_frames\":%" PRIu32 ","
                    "\"probe_req_frames\":%" PRIu32 ",\"deauth_frames\":%" PRIu32 ","
                    "\"eapol_frames\":%" PRIu32 ",\"pcap_frames\":%" PRIu32 ","
                    "\"pcap_dropped\":%" PRIu32 ",\"pcap_bytes\":%" PRIu32 ",\"probes\":[",
                    res->promisc.mgmt_frames, res->promisc.data_frames,
                    res->promisc.probe_req_frames, res->promisc.deauth_frames,
                    res->promisc.eapol_frames, res->promisc.pcap_captured_frames,
                    res->promisc.pcap_dropped_frames, res->promisc.pcap_captured_bytes);

        for (uint16_t i = 0; i < res->promisc.num_probes; i++) {
            if (i > 0) {
                jbuf_raw(jb, ",", 1);
            }
            const promisc_probe_t *pr = &res->promisc.probes[i];
            char mac_str[18] = {0};
            bssid_to_str(pr->sta_mac, mac_str, sizeof(mac_str));
            char ssid_limpio[33];
            char ssid_hex[65];
            size_t longitud = 0;
            bool sustituido = sanitizar_ssid(pr->ssid, ssid_limpio, ssid_hex, &longitud);

            jbuf_printf(jb, "{\"sta_mac\":\"%s\",\"ssid\":", mac_str);
            jbuf_str(jb, ssid_limpio);
            if (sustituido) {
                jbuf_printf(jb, ",\"ssid_hex\":\"%s\"", ssid_hex);
            }
            jbuf_printf(jb, ",\"rssi\":%d,\"channel\":%u,\"count\":%u",
                        (int)pr->rssi, (unsigned)pr->channel, (unsigned)pr->count);
            if (pr->pcap_flags != 0) {
                emit_pcap_traces_array(jb, "pcap_traces", pr->pcap_flags);
            }
            jbuf_raw(jb, "}", 1);
        }

        jbuf_raw(jb, "],\"clients\":[", 13);
        for (uint16_t i = 0; i < res->promisc.num_clients; i++) {
            if (i > 0) {
                jbuf_raw(jb, ",", 1);
            }
            const promisc_client_t *cl = &res->promisc.clients[i];
            char sta_str[18] = {0};
            char bssid_str[18] = {0};
            bssid_to_str(cl->sta_mac, sta_str, sizeof(sta_str));
            bssid_to_str(cl->bssid, bssid_str, sizeof(bssid_str));

            jbuf_printf(jb,
                        "{\"sta_mac\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"frames\":%u",
                        sta_str, bssid_str, (int)cl->rssi, (unsigned)cl->channel, (unsigned)cl->frames);
            if (cl->pcap_flags != 0) {
                emit_pcap_traces_array(jb, "pcap_traces", cl->pcap_flags);
            }
            jbuf_raw(jb, "}", 1);
        }

        jbuf_raw(jb, "],\"ap_pcap\":[", 13);
        for (uint16_t i = 0; i < res->promisc.num_ap_pcap; i++) {
            if (i > 0) {
                jbuf_raw(jb, ",", 1);
            }
            const promisc_ap_pcap_t *ap = &res->promisc.ap_pcap[i];
            char bssid_str[18] = {0};
            bssid_to_str(ap->bssid, bssid_str, sizeof(bssid_str));
            jbuf_printf(jb, "{\"bssid\":\"%s\",\"pcap_frames\":%u",
                        bssid_str, (unsigned)ap->pcap_frames);
            emit_pcap_traces_array(jb, "pcap_traces", ap->pcap_flags);
            jbuf_raw(jb, "}", 1);
        }
        jbuf_raw(jb, "]}", 2);
    }

    if (res->ble.enabled) {
        jbuf_printf(jb,
                    ",\"ble\":{\"unique_devices\":%u,\"ibeacon\":%u,\"eddystone\":%u,"
                    "\"findmy_candidates\":%u,\"other\":%u,\"adv_frames\":%" PRIu32 ",\"devices\":[",
                    (unsigned)res->ble.unique_devices, (unsigned)res->ble.ibeacon,
                    (unsigned)res->ble.eddystone, (unsigned)res->ble.findmy_candidates,
                    (unsigned)res->ble.other, res->ble.adv_frames);

        for (uint16_t i = 0; i < res->ble.num_devices; i++) {
            if (i > 0) {
                jbuf_raw(jb, ",", 1);
            }
            const ble_fusion_device_t *d = &res->ble.devices[i];
            char mac_str[18] = {0};
            bssid_to_str(d->mac, mac_str, sizeof(mac_str));
            jbuf_printf(jb,
                        "{\"mac\":\"%s\",\"addr_type\":\"%s\",\"type\":\"%s\",\"rssi\":%d,\"count\":%u",
                        mac_str, ble_fusion_addr_type_str(d->addr_type),
                        ble_fusion_type_str(d->dev_type), (int)d->rssi, (unsigned)d->count);
            if (d->name[0] != '\0') {
                jbuf_raw(jb, ",\"name\":", 8);
                jbuf_str(jb, d->name);
            }
            jbuf_raw(jb, "}", 1);
        }
        jbuf_raw(jb, "]}", 2);
    }

    if (res->deauth_tx.frames > 0 || res->deauth_tx.bursts > 0 || res->deauth_tx.rejected > 0) {
        jbuf_printf(jb,
                    ",\"deauth_tx\":{\"frames\":%" PRIu32 ",\"bursts\":%" PRIu32 ",\"rejected\":%" PRIu32 "}",
                    res->deauth_tx.frames, res->deauth_tx.bursts, res->deauth_tx.rejected);
    }

    jbuf_raw(jb, "}", 1);
}

char *build_scan_json_v1(const scan_result_t *res, const char *session_id)
{
    jbuf_t jb = {.buf = NULL, .len = 0, .cap = 0};
    emit_scan_json_v1(&jb, res, session_id);

    /* +2 bytes de holgura para permitir añadir '\n' in-place en pusher sin reasignar. */
    jb.cap = jb.len + 2;
    jb.buf = malloc(jb.cap);
    if (jb.buf == NULL) {
        return NULL;
    }
    jb.len = 0;
    emit_scan_json_v1(&jb, res, session_id);
    return jb.buf;
}

/* -------- build_scan_json del baseline: SOLO para el modo A/B legacy ------ */

char *build_scan_json(const wifi_ap_record_t *records, uint16_t count, uint16_t total_detected)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "timestamp_us", (double)esp_timer_get_time());
    cJSON_AddNumberToObject(root, "total_detected", total_detected);
    cJSON_AddNumberToObject(root, "stored_results", count);

    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    if (devices == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (uint16_t i = 0; i < count; i++) {
        const wifi_ap_record_t *ap = &records[i];
        cJSON *device = cJSON_CreateObject();
        if (device == NULL) {
            cJSON_Delete(root);
            return NULL;
        }

        char bssid[18] = {0};
        bssid_to_str(ap->bssid, bssid, sizeof(bssid));

        cJSON_AddStringToObject(device, "ssid", (const char *)ap->ssid);
        cJSON_AddStringToObject(device, "bssid", bssid);
        cJSON_AddNumberToObject(device, "rssi", ap->rssi);
        cJSON_AddNumberToObject(device, "primary_channel", ap->primary);
        cJSON_AddNumberToObject(device, "secondary_channel", ap->second);
        cJSON_AddStringToObject(device, "auth_mode", authmode_to_str(ap->authmode));
        cJSON_AddNumberToObject(device, "pairwise_cipher", ap->pairwise_cipher);
        cJSON_AddNumberToObject(device, "group_cipher", ap->group_cipher);
        cJSON_AddNumberToObject(device, "antenna", ap->ant);

        cJSON_AddItemToArray(devices, device);
    }

    char *json_payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_payload;
}
