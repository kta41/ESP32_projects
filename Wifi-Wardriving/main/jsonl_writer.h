#pragma once

/*
 * jsonl_writer — serialización de escaneos a JSON (evolución del baseline).
 *
 * Fase 1: traslado LITERAL de authmode_to_str / bssid_to_str /
 *         build_scan_json desde main.c (no se reescriben, se evolucionan).
 * Fase 2: evolución al payload JSONL v1 congelado (ROADMAP §3.5):
 *         cabecera completa, sanitización de SSID y mapeos a texto.
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_wifi.h"
#include "scanner.h" /* scan_result_t */

/** @brief Mapea wifi_auth_mode_t a texto (todos los enums de IDF v6.1, §3.5). */
const char *authmode_to_str(wifi_auth_mode_t authmode);

/** @brief Mapea wifi_cipher_type_t a texto (§3.5). */
const char *cipher_to_str(wifi_cipher_type_t cipher);

/** @brief Formatea un BSSID como "AA:BB:CC:DD:EE:FF" en out (mín. 18 bytes). */
void bssid_to_str(const uint8_t bssid[6], char *out, size_t out_len);

/**
 * @brief Construye el JSON del escaneo (heap, a liberar con free()).
 * @return cadena JSON o NULL si falta memoria.
 */
char *build_scan_json(const wifi_ap_record_t *records, uint16_t count, uint16_t total_detected);

/**
 * @brief Serializa un scan_result_t como línea del payload JSONL v1 (§3.5,
 *        [CONGELADO]). Sin '\n' final (lo añade el emisor).
 *
 * @param res         registro crudo del escaneo (propietario: scanner).
 * @param session_id  identidad de sesión (§3.4).
 * @return cadena JSON en heap (liberar con free()) o NULL sin memoria.
 */
char *build_scan_json_v1(const scan_result_t *res, const char *session_id);
