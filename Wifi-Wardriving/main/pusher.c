/*
 * pusher — cliente HTTP hacia el listener en el gateway (ROADMAP §3.7).
 */

#include "pusher.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jsonl_writer.h"
#include "scan_store.h"
#include "session.h"
#include "wifi_mgr.h"

static const char *TAG = "PUSHER";

#define MAX_BATCH_SCANS   4
#define HTTP_TIMEOUT_MS   8000
#define RESP_BUF_SIZE     256

static const uint32_t s_retry_delays_ms[] = {1000, 4000, 8000};

typedef struct {
    char buf[RESP_BUF_SIZE];
    size_t len;
} resp_acc_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data != NULL && evt->data_len > 0) {
        resp_acc_t *acc = (resp_acc_t *)evt->user_data;
        size_t avail = (sizeof(acc->buf) - 1) - acc->len;
        size_t copy = ((size_t)evt->data_len < avail) ? (size_t)evt->data_len : avail;
        if (copy > 0) {
            memcpy(acc->buf + acc->len, evt->data, copy);
            acc->len += copy;
            acc->buf[acc->len] = '\0';
        }
    }
    return ESP_OK;
}

/**
 * @brief Construye el body JSONL (con '\n' por línea) para un lote de escaneos.
 */
static char *build_batch_body(const scan_result_t **items, size_t count, size_t *out_len)
{
    char *lines[MAX_BATCH_SCANS] = {0};
    size_t lens[MAX_BATCH_SCANS] = {0};
    size_t total_len = 0;

    for (size_t i = 0; i < count; i++) {
        lines[i] = build_scan_json_v1(items[i], session_get_id());
        if (lines[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                free(lines[j]);
            }
            return NULL;
        }
        lens[i] = strlen(lines[i]);
        total_len += lens[i] + 1; /* + '\n' */
    }

    char *body = malloc(total_len + 1);
    if (body == NULL) {
        for (size_t i = 0; i < count; i++) {
            free(lines[i]);
        }
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(body + pos, lines[i], lens[i]);
        pos += lens[i];
        body[pos++] = '\n';
        free(lines[i]);
    }
    body[pos] = '\0';
    *out_len = pos;
    return body;
}

typedef enum {
    BATCH_SENT_ALL = 0,
    BATCH_PARTIAL_BAD_LINE,
    BATCH_RETRYABLE_ERROR,
} batch_outcome_t;

/**
 * @brief Envía un único lote por HTTP POST y procesa el acuse del listener.
 */
static batch_outcome_t post_one_batch(const scan_result_t **items, size_t count,
                                      size_t *out_bytes_sent)
{
    *out_bytes_sent = 0;

    esp_ip4_addr_t gw;
    if (!wifi_mgr_is_connected() || !wifi_mgr_get_gateway_ip(&gw)) {
        return BATCH_RETRYABLE_ERROR;
    }

    size_t body_len = 0;
    char *body = build_batch_body(items, count, &body_len);
    if (body == NULL) {
        ESP_LOGE(TAG, "Sin memoria para serializar lote de %u escaneos", (unsigned)count);
        return BATCH_RETRYABLE_ERROR;
    }

    char url[80];
    snprintf(url, sizeof(url), "http://" IPSTR ":%d/scans",
             IP2STR(&gw), CONFIG_WARDRIVE_LISTENER_PORT);

    char hdr_first[16];
    char hdr_last[16];
    snprintf(hdr_first, sizeof(hdr_first), "%" PRIu32, items[0]->scan_id);
    snprintf(hdr_last, sizeof(hdr_last), "%" PRIu32, items[count - 1]->scan_id);

    resp_acc_t resp = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_cb,
        .user_data = &resp,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(body);
        return BATCH_RETRYABLE_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/x-ndjson");
    esp_http_client_set_header(client, "X-Session-Id", session_get_id());
    esp_http_client_set_header(client, "X-First-Scan-Id", hdr_first);
    esp_http_client_set_header(client, "X-Last-Scan-Id", hdr_last);
    esp_http_client_set_post_field(client, body, (int)body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s falló: %s", url, esp_err_to_name(err));
        return BATCH_RETRYABLE_ERROR;
    }

    cJSON *json = (resp.len > 0) ? cJSON_Parse(resp.buf) : NULL;
    if (json == NULL) {
        ESP_LOGW(TAG, "Respuesta HTTP %d no es JSON válido: '%s'", status, resp.buf);
        return BATCH_RETRYABLE_ERROR;
    }

    if (status == 200) {
        const cJSON *accepted = cJSON_GetObjectItemCaseSensitive(json, "accepted");
        const cJSON *first_bad = cJSON_GetObjectItemCaseSensitive(json, "first_bad_scan_id");

        if (cJSON_IsNumber(accepted) && (size_t)accepted->valueint == count) {
            scan_store_drop_n(count);
            *out_bytes_sent = body_len;
            cJSON_Delete(json);
            return BATCH_SENT_ALL;
        }
        if (cJSON_IsNumber(first_bad) && first_bad->valuedouble > 0) {
            uint32_t bad_id = (uint32_t)first_bad->valuedouble;
            size_t drained = scan_store_drop_before_id(bad_id);
            ESP_LOGW(TAG, "Aceptación parcial: first_bad_scan_id=%" PRIu32 " (drenados=%u)",
                     bad_id, (unsigned)drained);
            *out_bytes_sent = body_len;
            cJSON_Delete(json);
            return BATCH_PARTIAL_BAD_LINE;
        }
        cJSON_Delete(json);
        return BATCH_RETRYABLE_ERROR;
    }

    if (status == 400) {
        const cJSON *first_bad = cJSON_GetObjectItemCaseSensitive(json, "first_bad_scan_id");
        if (cJSON_IsNumber(first_bad) && first_bad->valuedouble > 0) {
            uint32_t bad_id = (uint32_t)first_bad->valuedouble;
            size_t drained = scan_store_drop_before_id(bad_id);
            ESP_LOGW(TAG, "HTTP 400: first_bad_scan_id=%" PRIu32 " (drenados=%u)",
                     bad_id, (unsigned)drained);
            cJSON_Delete(json);
            return BATCH_PARTIAL_BAD_LINE;
        }
    }

    ESP_LOGW(TAG, "HTTP %d inesperado: '%s'", status, resp.buf);
    cJSON_Delete(json);
    return BATCH_RETRYABLE_ERROR;
}

push_report_t pusher_flush_pending(void)
{
    push_report_t report = {
        .status = PUSH_STATUS_FAIL,
        .bytes_sent = 0,
        .pending_after = scan_store_count(),
    };

    if (report.pending_after == 0) {
        report.status = PUSH_STATUS_OK;
        return report;
    }

    /* §3.7: Si no hay conexión WiFi, no gastar timeouts — saltar directamente. */
    if (!wifi_mgr_is_connected()) {
        scan_store_mark_buffered(scan_store_count());
        report.pending_after = scan_store_count();
        return report;
    }

    bool used_retry = false;
    bool any_sent = false;

    while (scan_store_count() > 0) {
        const scan_result_t *batch[MAX_BATCH_SCANS];
        size_t count = scan_store_peek_batch(batch, MAX_BATCH_SCANS);
        if (count == 0) {
            break;
        }

        bool batch_done = false;
        const size_t max_attempts = 1 + (sizeof(s_retry_delays_ms) / sizeof(s_retry_delays_ms[0]));

        for (size_t attempt = 0; attempt < max_attempts; attempt++) {
            if (attempt > 0) {
                used_retry = true;
                if (!wifi_mgr_is_connected()) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(s_retry_delays_ms[attempt - 1]));
                if (!wifi_mgr_is_connected()) {
                    break;
                }
            }

            size_t sent_now = 0;
            batch_outcome_t out = post_one_batch(batch, count, &sent_now);
            report.bytes_sent += sent_now;

            if (out == BATCH_SENT_ALL) {
                any_sent = true;
                batch_done = true;
                break;
            }
            if (out == BATCH_PARTIAL_BAD_LINE) {
                /* Se drenó solo el prefijo válido; no ciclar infinitamente sobre la línea mala. */
                break;
            }
        }

        if (!batch_done) {
            /* Falló este lote tras los reintentos: marcar pendientes como buffered (§3.8). */
            scan_store_mark_buffered(scan_store_count());
            break;
        }
    }

    report.pending_after = scan_store_count();
    if (report.pending_after == 0 && any_sent) {
        report.status = used_retry ? PUSH_STATUS_RETRY : PUSH_STATUS_OK;
    } else if (any_sent) {
        report.status = PUSH_STATUS_RETRY;
    } else {
        report.status = PUSH_STATUS_FAIL;
    }
    return report;
}

const char *pusher_status_str(push_status_t status)
{
    switch (status) {
        case PUSH_STATUS_OK:
            return "OK";
        case PUSH_STATUS_RETRY:
            return "RETRY";
        case PUSH_STATUS_FAIL:
        default:
            return "FALLA";
    }
}
