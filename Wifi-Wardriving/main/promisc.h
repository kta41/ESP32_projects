#pragma once

/*
 * promisc — captura pasiva promiscua 802.11 (Probe Requests, estaciones
 *           clientes asociadas y contadores de tramas deauth/disassoc).
 *
 * Opera de forma no intrusiva sobre el receptor WiFi del ESP32-S3 durante
 * el escaneo pasivo y los tiempos de escucha, filtrando siempre la MAC
 * propia del ESP32 y el BSSID/SSID del hotspot móvil.
 */

#include "esp_err.h"
#include "scanner.h"

/** @brief Inicializa y activa el sniffer promiscuo 802.11 (MGMT + DATA). */
esp_err_t promisc_init(void);

/**
 * @brief Volcado atómico y puesta a cero de las métricas promiscuas del ciclo,
 *        aplicando exclusión de identidad del propio sensor y del hotspot.
 */
void promisc_collect_and_reset(promisc_summary_t *out);
