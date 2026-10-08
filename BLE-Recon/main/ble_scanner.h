#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <stdint.h>
#include <stdbool.h>

// Registra los callbacks del host NimBLE (llamar tras nimble_port_init()).
// El escaneo arranca automaticamente cuando el host sincroniza con el controlador.
void ble_scanner_init(void);

#endif // BLE_SCANNER_H
