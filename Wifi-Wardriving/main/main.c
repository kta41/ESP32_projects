/*
 * Escáner WiFi wardriving «Nivel 3» — sensor sigiloso con push al móvil.
 * Fase 1 (ROADMAP §4): conectividad STA sigilosa al hotspot oculto.
 *
 * - STA puro con credenciales de Kconfig (nunca hardcodeadas).
 * - MAC aleatoria por sesión y TX mínimo (wifi_mgr).
 * - Log de conexión (IP + gateway) y ping ICMP de verificación al gateway.
 * - Sin NVS de datos: el almacén pasa a ser el móvil (Fases 3-4).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"

#include "scanner.h"
#include "session.h"
#include "wifi_mgr.h"

#if CONFIG_WARDRIVE_DEBUG_LEGACY_SCAN
#include <stdlib.h>

#include "esp_wifi.h"
#include "jsonl_writer.h"
#endif

static const char *TAG = "WARDRIVE_MAIN";

/* ------------------------------------------------------------------ */
/* Ping de verificación al gateway (Gate 1)                            */
/* ------------------------------------------------------------------ */

static void ping_exito(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl = 0;
    uint32_t ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
    ESP_LOGI(TAG, "ping al gateway OK (ttl=%u, tiempo=%lu ms)", ttl, (unsigned long)ms);
}

static void ping_timeout(esp_ping_handle_t hdl, void *args)
{
    ESP_LOGW(TAG, "ping al gateway: sin respuesta (timeout)");
}

static void ping_fin(esp_ping_handle_t hdl, void *args)
{
    uint32_t enviados = 0;
    uint32_t recibidos = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &enviados, sizeof(enviados));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &recibidos, sizeof(recibidos));
    ESP_LOGI(TAG, "ping al gateway terminado: %lu/%lu respuestas",
             (unsigned long)recibidos, (unsigned long)enviados);
    /* Liberar recursos (patrón del ejemplo oficial icmp_echo). */
    esp_ping_delete_session(hdl);
}

static void lanzar_ping_al_gateway(void)
{
    esp_ip4_addr_t gw;
    if (!wifi_mgr_get_gateway_ip(&gw)) {
        return;
    }

    char gw_str[IP4ADDR_STRLEN_MAX];
    snprintf(gw_str, sizeof(gw_str), IPSTR, IP2STR(&gw));

    ip_addr_t destino;
    if (ipaddr_aton(gw_str, &destino) == 0) {
        ESP_LOGE(TAG, "Gateway no parseable: %s", gw_str);
        return;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 4;
    cfg.interval_ms = 500;
    cfg.timeout_ms = 1000;
    cfg.target_addr = destino;

    static const esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_exito,
        .on_ping_timeout = ping_timeout,
        .on_ping_end = ping_fin,
    };

    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &ping);
    if (err != ESP_OK || ping == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la sesión de ping: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Ping de verificación al gateway %s...", gw_str);
    esp_ping_start(ping);
}

/* Evento GOT_IP: log de conexión del Gate 1 + ping (una vez por arranque). */
static void manejador_got_ip(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    static bool primera_vez = true;
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP || !primera_vez) {
        return;
    }
    primera_vez = false;

    const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)datos;
    ESP_LOGI(TAG, "=== GATE 1 | Conectado al hotspot: IP=" IPSTR ", gateway=" IPSTR " ===",
             IP2STR(&evt->ip_info.ip), IP2STR(&evt->ip_info.gw));
    lanzar_ping_al_gateway();
}

#if CONFIG_WARDRIVE_DEBUG_LEGACY_SCAN
/*
 * Modo DEBUG: bucle de escaneo ACTIVO del baseline, para el A/B del Gate 2.
 * El guardado en NVS está eliminado (Fase 1): solo se loggea el resumen.
 */
static void tarea_escaneo_legacy(void *pv)
{
    ESP_LOGW(TAG, "[LEGACY] escaneo ACTIVO cada %d ms (solo para A/B del Gate 2)",
             CONFIG_WARDRIVE_SCAN_INTERVAL_MS);

    while (true) {
        wifi_scan_config_t cfg = {
            .show_hidden = true,
        };
        esp_err_t err = esp_wifi_scan_start(&cfg, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[LEGACY] scan_start: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_WARDRIVE_SCAN_INTERVAL_MS));
            continue;
        }

        uint16_t total = 0;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&total));
        uint16_t a_guardar = (total > CONFIG_WARDRIVE_MAX_APS)
                                 ? (uint16_t)CONFIG_WARDRIVE_MAX_APS
                                 : total;

        wifi_ap_record_t *registros = NULL;
        if (a_guardar > 0) {
            registros = calloc(a_guardar, sizeof(wifi_ap_record_t));
        }
        if (a_guardar > 0 && registros == NULL) {
            ESP_LOGE(TAG, "[LEGACY] sin memoria para %u registros", a_guardar);
        } else {
            if (registros != NULL) {
                ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&a_guardar, registros));
            }
            char *json = build_scan_json(registros, a_guardar, total);
            if (json != NULL) {
                ESP_LOGI(TAG, "[LEGACY] Escaneo: detectados=%u almacenados=%u bytes=%u",
                         total, a_guardar, (unsigned)strlen(json));
                ESP_LOGD(TAG, "[LEGACY] JSON: %s", json);
                free(json);
            } else {
                ESP_LOGE(TAG, "[LEGACY] no se pudo construir el JSON");
            }
            free(registros);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WARDRIVE_SCAN_INTERVAL_MS));
    }
}
#endif /* CONFIG_WARDRIVE_DEBUG_LEGACY_SCAN */

void app_main(void)
{
    /* NVS: lo requiere el driver WiFi (§3.2). Sin blobs de datos: eliminados. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Log del Gate 1 (IP + gateway) y ping de verificación al recibir IP. */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               manejador_got_ip, NULL));

    /* STA sigiloso: MAC aleatoria de sesión, TX mínimo y eventos (§3.2). */
    ESP_ERROR_CHECK(wifi_mgr_init());

    /* Identidad de sesión (§3.4): session_id aleatorio por arranque. */
    session_init();

#if CONFIG_WARDRIVE_DEBUG_LEGACY_SCAN
    xTaskCreate(tarea_escaneo_legacy, "scan_legacy", 4096, NULL, 5, NULL);
#else
    /* Task de escaneo pasivo (§3.3): todo el trabajo vive en tasks. */
    ESP_ERROR_CHECK(scanner_start());
    ESP_LOGI(TAG, "Fase 2 operativa: escaneo pasivo cada %d ms "
                  "(el push al gateway llega en la Fase 3)",
             CONFIG_WARDRIVE_SCAN_INTERVAL_MS);
#endif

    /* app_main queda como init + arranque de tasks: sin bucles aquí. */
}
