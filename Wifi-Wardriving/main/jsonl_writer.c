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

#include <stdio.h>

#include "cJSON.h"
#include "esp_timer.h"

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

static cJSON *device_a_json(const wifi_ap_record_t *ap)
{
    cJSON *device = cJSON_CreateObject();
    if (device == NULL) {
        return NULL;
    }

    char ssid_limpio[33];
    char ssid_hex[65];
    size_t longitud = 0;
    bool sustituido = sanitizar_ssid(ap->ssid, ssid_limpio, ssid_hex, &longitud);

    cJSON_AddStringToObject(device, "ssid", ssid_limpio);
    if (sustituido) {
        cJSON_AddStringToObject(device, "ssid_hex", ssid_hex);
    }

    char bssid[18] = {0};
    bssid_to_str(ap->bssid, bssid, sizeof(bssid));

    cJSON_AddStringToObject(device, "bssid", bssid);
    cJSON_AddNumberToObject(device, "rssi", ap->rssi);
    cJSON_AddNumberToObject(device, "primary_channel", ap->primary);
    cJSON_AddNumberToObject(device, "secondary_channel", ap->second);
    cJSON_AddStringToObject(device, "auth_mode", authmode_to_str(ap->authmode));
    cJSON_AddStringToObject(device, "pairwise_cipher", cipher_to_str(ap->pairwise_cipher));
    cJSON_AddStringToObject(device, "group_cipher", cipher_to_str(ap->group_cipher));

    /* proto: flags b/g/n concatenados; "lr" aparte solo si phy_lr (§3.5). */
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
    cJSON_AddStringToObject(device, "proto", proto);
    if (ap->phy_lr) {
        cJSON_AddBoolToObject(device, "lr", true);
    }

    /* 20 MHz si no hay canal secundario, 40 en caso contrario (§3.5). */
    cJSON_AddNumberToObject(device, "band_mhz",
                            ap->second == WIFI_SECOND_CHAN_NONE ? 20 : 40);
    cJSON_AddBoolToObject(device, "wps", ap->wps ? true : false);
    cJSON_AddBoolToObject(device, "hidden", longitud == 0);

    return device;
}

/* ------------------------ Payload JSONL v1 (§3.5) ------------------------ */

char *build_scan_json_v1(const scan_result_t *res, const char *session_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    /* Cabecera: orden y tipos exactos del esquema congelado. */
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "mode", "scan");
    cJSON_AddStringToObject(root, "scan_type", "passive");
    cJSON_AddStringToObject(root, "session_id", session_id);
    cJSON_AddNumberToObject(root, "scan_id", (double)res->scan_id);
    cJSON_AddNumberToObject(root, "device_ts_us", (double)res->ts_us);
    cJSON_AddNumberToObject(root, "scan_duration_ms", (double)res->duration_ms);
    cJSON_AddNumberToObject(root, "total_detected", res->total_detected);
    cJSON_AddNumberToObject(root, "stored", res->stored);
    cJSON_AddNumberToObject(root, "dropped", res->dropped);
    cJSON_AddNumberToObject(root, "lost_before", (double)res->lost_before);
    cJSON_AddBoolToObject(root, "buffered", res->buffered);

    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    if (devices == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (uint16_t i = 0; i < res->num_records; i++) {
        cJSON *device = device_a_json(&res->records[i]);
        if (device == NULL || cJSON_AddItemToArray(devices, device) == 0) {
            cJSON_Delete(device);
            cJSON_Delete(root);
            return NULL;
        }
    }

    char *linea = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return linea;
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
