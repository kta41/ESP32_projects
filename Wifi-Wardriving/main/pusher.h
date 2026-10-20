#pragma once

/*
 * pusher — envío HTTP POST de escaneos JSONL al gateway (ROADMAP §3.7).
 *
 * - Endpoint: POST http://<gateway_ip>:<CONFIG_WARDRIVE_LISTENER_PORT>/scans
 * - Lotes de hasta 4 escaneos por POST, con Content-Length fijo.
 * - Timeout de 8 s; reintentos en el ciclo a +1 s, +4 s, +8 s.
 * - Si no hay enlace WiFi, salta inmediatamente sin gastar timeouts.
 */

#include <stddef.h>

/** Estado resultante del intento de push de un ciclo. */
typedef enum {
    PUSH_STATUS_OK = 0,    /**< Drenado al menos un lote en el primer intento. */
    PUSH_STATUS_RETRY = 1, /**< Drenado tras requerir al menos un reintento. */
    PUSH_STATUS_FAIL = 2,  /**< Sin conexión o fallo tras agotar reintentos. */
} push_status_t;

/** Resumen del push ejecutado al final de un ciclo de escaneo. */
typedef struct {
    push_status_t status;
    size_t bytes_sent;
    size_t pending_after;
} push_report_t;

/** @brief Drena el ring buffer hacia el listener del gateway según §3.7. */
push_report_t pusher_flush_pending(void);

/** @brief Etiqueta textual para el log de ciclo ("OK", "RETRY", "FALLA"). */
const char *pusher_status_str(push_status_t status);
