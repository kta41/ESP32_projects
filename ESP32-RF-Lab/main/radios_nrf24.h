#ifndef RADIOS_NRF24_H
#define RADIOS_NRF24_H

#include <stdint.h>
#include <stdbool.h>

// ========== INICIALIZACIÓN Y ESTADO ==========
void init_radios(void);
bool radio1_is_connected(void);
bool radio2_is_connected(void);

// ========== MODO RECEPCIÓN CONTINUA (ANÁLISIS) ==========
void radios_start_rx(void);
void radios_stop(void);

// ========== DETECTOR DE POTENCIA RECIBIDA (RPD) ==========
bool radio1_rpd(void);
bool radio2_rpd(void);

// ========== CONTROL DE ESTADO TX/RX ==========
void radio1_tx_mode(void);
void radio2_tx_mode(void);
void radio1_rx_mode(void);
void radio2_rx_mode(void);

// ========== ENVÍO Y RECEPCIÓN DE PAYLOADS ==========
void radio1_transmit(uint8_t *payload, uint8_t len);
void radio2_transmit(uint8_t *payload, uint8_t len);
bool radio1_data_ready(void);
bool radio2_data_ready(void);
void radio1_get_payload(uint8_t *buf);
void radio2_get_payload(uint8_t *buf);

// ========== CONFIGURACIÓN DE CANALES ==========
void radio1_set_channel(uint8_t ch);
void radio2_set_channel(uint8_t ch);
uint8_t radio1_get_channel(void);
uint8_t radio2_get_channel(void);

// ========== CONFIGURACIÓN AVANZADA DE RF ==========
void radio1_set_pa_level(uint8_t level);
void radio2_set_pa_level(uint8_t level);
void radio1_set_data_rate(uint8_t rate);
void radio2_set_data_rate(uint8_t rate);
void radio1_set_crc_length(uint8_t length);
void radio2_set_crc_length(uint8_t length);
void radio1_set_auto_ack(bool enable);
void radio2_set_auto_ack(bool enable);
void radio1_set_retries(uint8_t delay, uint8_t count);
void radio2_set_retries(uint8_t delay, uint8_t count);

#endif // RADIOS_NRF24_H