#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "ble_scanner.h"

static const char *TAG = "MAIN";

// Tarea del host NimBLE: ejecuta el stack hasta que se detenga
static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();              // Bloquea mientras el host este activo
    nimble_port_freertos_deinit();  // Limpieza al detener
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 BLE-Recon ===");
    ESP_LOGI(TAG, "Escaneo BLE continuo con NimBLE (M1: log serie)");

    // 1. NVS (requerido por el controlador Bluetooth)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Inicializar el stack NimBLE (controlador + host)
    ESP_ERROR_CHECK(nimble_port_init());

    // 3. Registrar callbacks del scanner (sync -> autostart del escaneo)
    ble_scanner_init();

    // 4. Lanzar la tarea del host (core por defecto)
    nimble_port_freertos_init(ble_host_task);

    // 5. Idle
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
