/*
 * wifi_mgr — gestión del STA «sigiloso» (ROADMAP §3.2).
 */

#include "wifi_mgr.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "WIFI_MGR";

/* Rate-limit de reconexión (§3.2): 1 reintento cada 5 s, nunca en el handler. */
#define REINTENTO_CONEXION_MS 5000

static volatile bool s_conectado = false;
static volatile bool s_asociado = false;
static volatile bool s_conectando = false;
static volatile bool s_escaneando = false;
static volatile bool s_primer_intento_resuelto = false;
static volatile bool s_hay_bssid_propio = false;
static uint8_t s_bssid_propio[6] = {0};
static esp_ip4_addr_t s_ip_propia;
static esp_ip4_addr_t s_gateway;
static TimerHandle_t s_timer_reintento = NULL;

/* Reintento diferido: lo dispara el timer, no el handler de eventos. */
static void cb_reintento(TimerHandle_t timer)
{
    if (s_escaneando || s_conectado || s_conectando || s_asociado) {
        return;
    }
    ESP_LOGI(TAG, "Reintentando asociación al hotspot...");
    s_conectando = true;
    /* El resultado (éxito o nuevo fallo) llega por eventos. */
    esp_wifi_connect();
}

static void manejador_eventos(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Interfaz STA iniciada, asociando al hotspot oculto...");
        s_conectando = true;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *evt = (const wifi_event_sta_connected_t *)datos;
        s_conectando = false;
        s_asociado = true;
        if (evt != NULL) {
            memcpy(s_bssid_propio, evt->bssid, 6);
            s_hay_bssid_propio = true;
            ESP_LOGI(TAG, "Asociado 802.11 (canal %u), esperando DHCP...", evt->channel);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *evt = (const wifi_event_sta_disconnected_t *)datos;
        s_conectado = false;
        s_asociado = false;
        s_conectando = false;
        s_primer_intento_resuelto = true;
        if (!s_escaneando) {
            ESP_LOGW(TAG, "Desconectado (razón %d); reintento en %d ms",
                     evt ? evt->reason : -1, REINTENTO_CONEXION_MS);
            if (s_timer_reintento != NULL) {
                /* One-shot de 5 s: no bloquea el handler (§3.2). */
                xTimerStart(s_timer_reintento, 0);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)datos;
        s_ip_propia = evt->ip_info.ip;
        s_gateway = evt->ip_info.gw;
        s_conectado = true;
        s_conectando = false;
        s_primer_intento_resuelto = true;
        if (s_timer_reintento != NULL) {
            xTimerStop(s_timer_reintento, 0);
        }
        ESP_LOGI(TAG, "Asociado: IP=" IPSTR ", gateway=" IPSTR,
                 IP2STR(&s_ip_propia), IP2STR(&s_gateway));
    }
}

esp_err_t wifi_mgr_init(void)
{
    const char *ssid = CONFIG_WARDRIVE_WIFI_SSID;
    const char *clave = CONFIG_WARDRIVE_WIFI_PASS;

    /* Credenciales solo vía Kconfig: validación temprana con mensaje claro. */
    if (ssid[0] == '\0' || clave[0] == '\0') {
        ESP_LOGE(TAG, "Credenciales sin configurar: `idf.py menuconfig` → "
                      "Wardrive (sensor sigiloso)");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(clave) < 8) {
        ESP_LOGE(TAG, "La clave WPA2 debe tener al menos 8 caracteres");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg_init), TAG, "Fallo en esp_wifi_init");

    /* MAC aleatoria por sesión, ANTES de esp_wifi_start() (§3.2):
     * bit0 = 0 (unicast) y bit1 = 1 (locally administered); resto aleatorio. */
    uint8_t mac[6];
    esp_fill_random(mac, sizeof(mac));
    mac[0] = (uint8_t)((esp_random() & 0xFC) | 0x02);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mac(WIFI_IF_STA, mac), TAG,
                        "No se pudo fijar la MAC aleatoria");
    ESP_LOGI(TAG, "MAC aleatoria de sesión: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, clave, sizeof(wifi_cfg.sta.password));
    /* Escaneo activo de todos los canales al asociar para encontrar SSID oculto a la primera. */
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "No se pudo fijar WIFI_MODE_STA (único modo permitido)");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG,
                        "No se pudo aplicar la configuración STA");

    /* Reintento rate-limited: timer one-shot, disparado desde el evento
     * de desconexión pero ejecutado fuera del handler (§3.2). */
    s_timer_reintento = xTimerCreate("wdr_reintento",
                                     pdMS_TO_TICKS(REINTENTO_CONEXION_MS),
                                     pdFALSE, NULL, cb_reintento);
    ESP_RETURN_ON_FALSE(s_timer_reintento != NULL, ESP_ERR_NO_MEM, TAG,
                        "No se pudo crear el timer de reintento");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   manejador_eventos, NULL),
                        TAG, "No se pudo registrar el handler de WIFI_EVENT");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   manejador_eventos, NULL),
                        TAG, "No se pudo registrar el handler de IP_EVENT");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Fallo en esp_wifi_start");

    /* Potencia TX mínima DESPUÉS de esp_wifi_start() (§3.2). */
    ESP_RETURN_ON_ERROR(esp_wifi_set_max_tx_power((int8_t)CONFIG_WARDRIVE_TX_POWER),
                        TAG, "No se pudo fijar la potencia TX mínima");
    int8_t tx = 0;
    if (esp_wifi_get_max_tx_power(&tx) == ESP_OK) {
        ESP_LOGI(TAG, "Potencia TX máx: %d (x0.25 dBm = %d.%02d dBm)",
                 tx, tx / 4, (tx % 4) * 25);
    }

    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return s_conectado;
}

bool wifi_mgr_wait_initial_connect(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step_ms = 50;
    while (!s_conectado && !s_primer_intento_resuelto && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed += step_ms;
    }
    return s_conectado;
}

void wifi_mgr_pause_connect_for_scan(void)
{
    /* Si hay una asociación o DHCP en curso, darle margen para terminar antes de abortar. */
    uint32_t wait_ms = 0;
    while ((s_conectando || (s_asociado && !s_conectado)) && wait_ms < 4500) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
    }

    s_escaneando = true;
    if (s_timer_reintento != NULL) {
        xTimerStop(s_timer_reintento, 0);
    }
    if (!s_conectado && s_conectando) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void wifi_mgr_resume_connect_after_scan(void)
{
    s_escaneando = false;
    if (!s_conectado && !s_conectando && !s_asociado) {
        /* Reintentar inmediatamente tras el barrido para aprovechar la ventana entre escaneos. */
        ESP_LOGI(TAG, "Reintentando asociación al hotspot tras escaneo...");
        s_conectando = true;
        esp_wifi_connect();
    }
}

bool wifi_mgr_get_gateway_ip(esp_ip4_addr_t *out_gateway)
{
    if (!s_conectado || out_gateway == NULL) {
        return false;
    }
    *out_gateway = s_gateway;
    return true;
}

bool wifi_mgr_get_own_ip(esp_ip4_addr_t *out_ip)
{
    if (!s_conectado || out_ip == NULL) {
        return false;
    }
    *out_ip = s_ip_propia;
    return true;
}

bool wifi_mgr_get_hotspot_bssid(uint8_t out_bssid[6])
{
    if (!s_hay_bssid_propio || out_bssid == NULL) {
        return false;
    }
    memcpy(out_bssid, s_bssid_propio, 6);
    return true;
}
