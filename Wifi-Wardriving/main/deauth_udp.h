#pragma once

/*
 * deauth_udp — Canal de control UDP :47472 para el módulo deauth_tx.
 *
 * Protocolo (JSON en un datagrama, respuesta en un datagrama):
 *   {"cmd":"ping"}                       -> {"type":"pong","session":"XXXX"}
 *   {"cmd":"deauth","token":"T","bssid":"AA:..","sta":"FF:..","channel":6,
 *    "reason":1,"count":4}               -> {"type":"queued","count":4}
 *
 * TODO (incluido el ping) requiere el token válido; sin token configurado el
 * puerto no responde nada (stealth). Solo activo con WARDRIVE_DEAUTH_ENABLE.
 */

#include "esp_err.h"

/** @brief Arranca el listener UDP de control (no-op si el módulo está desactivado). */
esp_err_t deauth_udp_start(void);
