/*
 * jsonl_writer — Fase 1: traslado literal del baseline (main.c original).
 * Fase 2 lo evolucionará al payload JSONL v1 (ROADMAP §3.5).
 */

#include "jsonl_writer.h"

#include <stdio.h>

#include "cJSON.h"
#include "esp_timer.h"

const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        default:
            return "UNKNOWN";
    }
}

void bssid_to_str(const uint8_t bssid[6], char *out, size_t out_len)
{
    if (out_len < 18) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

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
