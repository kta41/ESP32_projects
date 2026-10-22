#pragma once

/*
 * wifi_mgr — gestión del STA «sigiloso» (ROADMAP §3.2).
 *
 * Fase 1: STA puro con credenciales de Kconfig, MAC aleatoria por sesión,
 * potencia TX mínima, eventos de conexión y reintento rate-limited fuera
 * del handler. El gateway del hotspot es el objetivo del push (Fase 3).
 */

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

/**
 * @brief Inicializa el STA: netif STA, MAC aleatoria de sesión (logueada),
 *        config con credenciales de Kconfig, eventos y TX mínimo.
 *
 * @note  Requiere esp_netif_init() y esp_event_loop_create_default() previos.
 * @return ESP_OK, ESP_ERR_INVALID_STATE (credenciales sin configurar) u otro
 *         error del driver WiFi.
 */
esp_err_t wifi_mgr_init(void);

/** @brief ¿Está el STA asociado y con IP? (flag limpiado al desconectar). */
bool wifi_mgr_is_connected(void);

/** @brief Espera hasta timeout_ms a que el STA obtenga IP (o termine el primer intento). */
bool wifi_mgr_wait_initial_connect(uint32_t timeout_ms);

/** @brief Pausa reintentos y aborta cualquier asociación en curso antes de escanear. */
void wifi_mgr_pause_connect_for_scan(void);

/** @brief Reanuda la reconexión si el STA sigue desconectado tras el escaneo. */
void wifi_mgr_resume_connect_after_scan(void);

/** @brief Copia la IP del gateway (el móvil) a out. false si no conectado. */
bool wifi_mgr_get_gateway_ip(esp_ip4_addr_t *out_gateway);

/** @brief Copia la IP propia del STA a out. false si no conectado. */
bool wifi_mgr_get_own_ip(esp_ip4_addr_t *out_ip);

/**
 * @brief Copia el último BSSID conocido del hotspot propio (persiste tras desconexiones).
 * @return true si ya nos hemos asociado al menos una vez al hotspot en esta sesión.
 */
bool wifi_mgr_get_hotspot_bssid(uint8_t out_bssid[6]);
