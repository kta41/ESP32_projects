#pragma once

/*
 * session — identidad de sesión (ROADMAP §3.4).
 *
 * - session_id: 8 caracteres hex a partir de esp_random(), una vez por
 *   arranque (la MAC cambia por sesión; esta es la identidad estable).
 * - scan_id: uint32_t monótono que empieza en 1 por arranque.
 *
 * Nota de concurrencia: en las fases actuales solo el task de escaneo
 * incrementa scan_id; session_id es inmutable tras el arranque.
 */

#include <stdbool.h>
#include <stdint.h>

/** @brief Genera el session_id de este arranque (llamar una vez en app_main). */
void session_init(void);

/** @brief session_id hex de 8 caracteres (ej. "9f3a21c4"). */
const char *session_get_id(void);

/** @brief Devuelve el siguiente scan_id (1, 2, 3...). Solo tras escaneo válido. */
uint32_t session_next_scan_id(void);
