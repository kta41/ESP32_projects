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

/** @brief Copia la IP del gateway (el móvil) a out. false si no conectado. */
bool wifi_mgr_get_gateway_ip(esp_ip4_addr_t *out_gateway);

/** @brief Copia la IP propia del STA a out. false si no conectado. */
bool wifi_mgr_get_own_ip(esp_ip4_addr_t *out_ip);
