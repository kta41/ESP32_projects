#pragma once

/*
 * deauth_tx — Envío de tramas Deauth/Disassoc CONTROLADAS (módulo opt-in Red Team).
 *
 * ⚠️ SOLO para evaluaciones de seguridad AUTORIZADAS (ROE firmadas). El módulo
 * está DESACTIVADO por defecto (CONFIG_WARDRIVE_DEAUTH_ENABLE=n): el sensor
 * público permanece receive-only salvo decisión explícita del operador.
 *
 * Salvaguardas integradas:
 *  - Token compartido (CONFIG_WARDRIVE_DEAUTH_TOKEN): sin token válido el
 *    canal de control UDP rechaza TODO (incluido el ping de descubrimiento).
 *  - Rate limiting: ráfaga máxima, período mínimo entre ráfagas y total
 *    máximo de tramas por arranque (Kconfig).
 *  - Ejecución diferida: las peticiones UDP se encolan y se ejecutan desde el
 *    task de escaneo ENTRE ciclos (sin conflicto con esp_wifi_scan_start).
 *  - Auditoría: cada trama enviada incrementa contadores que viajan en el
 *    payload JSONL (bloque "deauth_tx") -> trazabilidad en SIGINT-Console.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CONFIG_WARDRIVE_DEAUTH_MAX_BURST
#define CONFIG_WARDRIVE_DEAUTH_MAX_BURST 16
#endif

#ifndef CONFIG_WARDRIVE_DEAUTH_MIN_PERIOD_S
#define CONFIG_WARDRIVE_DEAUTH_MIN_PERIOD_S 10
#endif

#ifndef CONFIG_WARDRIVE_DEAUTH_MAX_TOTAL
#define CONFIG_WARDRIVE_DEAUTH_MAX_TOTAL 256
#endif

#define DEAUTH_QUEUE_DEPTH 4
#define DEAUTH_MAX_COUNT   64

/** Petición de ráfaga deauth validada (en cola para el task de escaneo). */
typedef struct {
    uint8_t bssid[6];
    uint8_t sta[6];    /**< Destino (FF:FF:FF:FF:FF:FF = broadcast a todos los clientes). */
    uint8_t channel;   /**< Canal primario del objetivo (1..14). */
    uint16_t reason;   /**< Código de razón IEEE 802.11 (1 = no especificado). */
    uint8_t count;     /**< Tramas de la ráfaga (ya limitado a MAX_BURST). */
} deauth_request_t;

/** Contadores auditables de la sesión (exportados al JSONL). */
typedef struct {
    uint32_t frames;   /**< Tramas deauth/disassoc transmitidas en el ciclo. */
    uint32_t bursts;   /**< Ráfagas ejecutadas en el ciclo. */
    uint32_t rejected; /**< Peticiones rechazadas por rate-limit/cuota en el ciclo. */
} deauth_cycle_stats_t;

/** @brief Inicializa el subsistema (cola + contadores). Llamar desde app_main. */
void deauth_tx_init(void);

/**
 * @brief Encola una ráfaga (llamado por el canal de control UDP tras validar
 *        token y rate-limit). Devuelve false si la cola está llena.
 */
bool deauth_tx_enqueue(const deauth_request_t *req);

/**
 * @brief Ejecuta las ráfagas encoladas y devuelve los contadores del ciclo.
 *
 * Llamar SOLO desde el task de escaneo, después del push HTTP y antes del
 * vTaskDelayUntil del siguiente ciclo (la radio está libre entre ciclos).
 */
void deauth_tx_service(deauth_cycle_stats_t *out);

/** @brief ¿Queda cuota disponible (total por arranque no agotado)? */
bool deauth_tx_quota_available(void);
