#pragma once

/*
 * ble_fusion — Fusión BLE-Recon en el sensor Wifi-Wardriving (P2).
 *
 * Escáner NimBLE continuo coexistente (CONFIG_SW_COEXIST_ENABLE) que acumula
 * por ciclo de escaneo WiFi (~15 s) un resumen BLE: dispositivos únicos,
 * clasificación (iBeacon / Eddystone / FindMy-candidato / otros), contadores
 * de tramas ADV y la tabla de los dispositivos más fuertes.
 *
 * - 100 % PASIVO en BLE (passive=1): el sensor NUNCA emite SCAN_REQ, igual
 *   que no emite Probe Requests en WiFi (doctrina receive-only del sensor).
 *   Los nombres vía SCAN_RSP no estarán disponibles; iBeacon/Eddystone/FindMy
 *   viven en el payload ADV y se capturan igual.
 * - El resumen viaja en el mismo payload JSONL v1 (bloque opcional "ble") y
 *   por el mismo uplink HTTP: un sensor, una batería, un session_id y reloj.
 * - Si CONFIG_WARDRIVE_BLE_FUSION está desactivado, el módulo compila como
 *   stub (enabled=false) y el payload sale sin bloque "ble".
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CONFIG_WARDRIVE_MAX_BLE_DEVICES
#define CONFIG_WARDRIVE_MAX_BLE_DEVICES 24
#endif

/* Máximo de direcciones recordadas por ciclo para el conteo de únicos. */
#define BLE_FUSION_CYCLE_CACHE 96

/** Máscara para el tipo de dirección BLE (valor crudo HCI 0..3). */
typedef uint8_t ble_addr_type_raw_t;

/** Dispositivo BLE observado durante el ciclo (MAC en orden de transmisión). */
typedef struct {
    uint8_t mac[6];            /**< MAC en orden de transmisión (para BSSID_RE del validador). */
    ble_addr_type_raw_t addr_type; /**< PUBLIC/RANDOM/... (valor crudo NimBLE). */
    int8_t rssi;               /**< Mejor RSSI observado en el ciclo. */
    uint8_t dev_type;          /**< ble_dev_type_t (ver ble_parser.h). */
    char name[24];             /**< Nombre ADV truncado (vacío si no se anunció). */
    uint16_t count;            /**< Tramas ADV del dispositivo en el ciclo. */
} ble_fusion_device_t;

/** Resumen BLE del ciclo, embebido en scan_result_t (§3.5, bloque "ble"). */
typedef struct {
    bool enabled;              /**< false si la fusión está desactivada (stub). */
    uint16_t unique_devices;   /**< Direcciones BLE distintas vistas en el ciclo. */
    uint16_t ibeacon;          /**< Únicos clasificados como iBeacon. */
    uint16_t eddystone;        /**< Únicos Eddystone (UID/URL/TLM). */
    uint16_t findmy_candidates;/**< Únicos candidatos FindMy (heurística rotativa). */
    uint16_t other;            /**< Únicos sin formato conocido. */
    uint32_t adv_frames;       /**< Tramas ADV totales del ciclo. */
    uint16_t num_devices;      /**< Elementos válidos en devices[]. */
    ble_fusion_device_t devices[CONFIG_WARDRIVE_MAX_BLE_DEVICES];
} ble_fusion_summary_t;

/** @brief Inicializa el stack NimBLE y arranca el escaneo BLE pasivo continuo. */
int ble_fusion_init(void);

/** @brief Nombre corto del tipo de dispositivo ("IBEACON", "EDDYSTONE_URL", ...). */
const char *ble_fusion_type_str(uint8_t dev_type);

/** @brief Nombre del tipo de dirección BLE ("PUBLIC", "RANDOM", ...). */
const char *ble_fusion_addr_type_str(uint8_t addr_type);

/**
 * @brief Volcado atómico y reset del resumen BLE del ciclo actual.
 *
 * Llamado desde el task de escaneo justo después de promisc_collect_and_reset().
 * Si la fusión está desactivada, solo pone out->enabled = false.
 */
void ble_fusion_collect_and_reset(ble_fusion_summary_t *out);
