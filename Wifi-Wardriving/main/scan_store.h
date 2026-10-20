#pragma once

/*
 * scan_store — ring buffer de escaneos crudos en RAM (ROADMAP §3.6).
 *
 * Preasigna CONFIG_WARDRIVE_RING_SLOTS slots de scan_result_t (~85 KB en BSS,
 * sin cadenas JSON). Si se llena ante un corte de enlace, descarta el más
 * antiguo e incrementa el contador acumulado lost_before de la sesión.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scanner.h"

/** @brief Inicializa o reinicia el ring buffer en RAM. */
void scan_store_init(void);

/**
 * @brief Encola un resultado crudo en el ring buffer.
 *
 * Si el ring está lleno, descarta el slot más antiguo e incrementa
 * el acumulado lost_before antes de copiar el nuevo escaneo.
 */
void scan_store_push(const scan_result_t *scan);

/** @brief Número de escaneos pendientes actualmente en el ring. */
size_t scan_store_count(void);

/** @brief Total acumulado de escaneos descartados por ring lleno en la sesión. */
uint32_t scan_store_get_lost_before(void);

/**
 * @brief Obtiene punteros a los primeros `max_items` escaneos pendientes
 *        (orden FIFO, del más antiguo al más reciente).
 *
 * @param out_items Array de punteros de tamaño >= max_items.
 * @param max_items Máximo de elementos a consultar (ej. 4 por POST).
 * @return Número de elementos rellenados en out_items.
 */
size_t scan_store_peek_batch(const scan_result_t **out_items, size_t max_items);

/**
 * @brief Elimina del ring los primeros `count` escaneos ya confirmados.
 */
void scan_store_drop_n(size_t count);

/**
 * @brief Elimina del ring el prefijo contiguo con `scan_id < first_bad_scan_id`.
 *
 * @return Número de slots drenados.
 */
size_t scan_store_drop_before_id(uint32_t first_bad_scan_id);

/**
 * @brief Marca como `buffered = true` los primeros `count` escaneos pendientes
 *        tras sufrir un ciclo de push fallido (§3.5 / §3.8).
 */
void scan_store_mark_buffered(size_t count);
