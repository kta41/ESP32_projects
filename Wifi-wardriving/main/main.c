#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "WIFI_WARDRIVE"

#define MAX_SCAN_RESULTS 64
#define SCAN_INTERVAL_MS 15000

#define NVS_NAMESPACE "wardrive"
#define NVS_KEY_LAST_SCAN "last_scan_json"

static const char *authmode_to_str(wifi_auth_mode_t authmode)
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

static void bssid_to_str(const uint8_t bssid[6], char *out, size_t out_len)
{
    if (out_len < 18) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static esp_err_t save_json_to_flash(const char *json_payload)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "No se pudo abrir NVS");

    size_t payload_len = strlen(json_payload) + 1;
    err = nvs_set_blob(nvs_handle, NVS_KEY_LAST_SCAN, json_payload, payload_len);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Error guardando JSON en NVS");
    return ESP_OK;
}

static char *build_scan_json(const wifi_ap_record_t *records, uint16_t count, uint16_t total_detected)
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

static void scan_wifi_and_store(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

    uint16_t results_to_store = ap_count > MAX_SCAN_RESULTS ? MAX_SCAN_RESULTS : ap_count;
    wifi_ap_record_t *records = NULL;

    if (results_to_store > 0) {
        records = calloc(results_to_store, sizeof(wifi_ap_record_t));
        if (records == NULL) {
            ESP_LOGE(TAG, "No hay memoria para resultados de escaneo");
            return;
        }

        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&results_to_store, records));
    }

    char *json_payload = build_scan_json(records, results_to_store, ap_count);
    if (json_payload == NULL) {
        ESP_LOGE(TAG, "No se pudo construir JSON del escaneo");
        free(records);
        return;
    }

    esp_err_t err = save_json_to_flash(json_payload);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Escaneo guardado: detectados=%u, almacenados=%u, bytes=%u",
                 ap_count, results_to_store, (unsigned)strlen(json_payload));
    } else {
        ESP_LOGE(TAG, "Fallo al guardar escaneo en flash: %s", esp_err_to_name(err));
    }

    free(json_payload);
    free(records);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Iniciando wardriving Wi-Fi (escaneo cada %d ms)", SCAN_INTERVAL_MS);

    while (true) {
        scan_wifi_and_store();
        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}
