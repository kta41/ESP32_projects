#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "hw_config.h"
#include "display_st7789.h"
#include "radios_nrf24.h"
#include "rf_survey_engine.h"
#include "nrf24_diagnostics.h"



static const char *TAG = "MAIN";

QueueHandle_t mode_queue;   // Cola de selección de modo (UI -> motor de medición)

// ========== INICIALIZACIÓN DE BOTONES ==========
void init_buttons(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_UP) | (1ULL << BTN_DOWN) | 
                         (1ULL << BTN_OK) | (1ULL << BTN_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

// ========== LECTURA DE BOTONES PARA LVGL ==========
static void keypad_read(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    static uint32_t last_key = 0;
    if (gpio_get_level(BTN_UP) == 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_UP;
    } else if (gpio_get_level(BTN_DOWN) == 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_DOWN;
    } else if (gpio_get_level(BTN_OK) == 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_ENTER;
    } else if (gpio_get_level(BTN_BACK) == 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_ESC;
        survey_stop();
        ESP_LOGI("UI", "STOP! (modo seguro)");
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->key = last_key;
}

// ========== CALLBACK DEL ROLLER ==========
static void roller_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) {
            uint16_t sel_idx = lv_roller_get_selected(obj);
            xQueueSend(mode_queue, &sel_idx, portMAX_DELAY);
            char buf[32];
            lv_roller_get_selected_str(obj, buf, sizeof(buf));
            ESP_LOGI("UI", "Seleccionado: %s (idx=%d)", buf, sel_idx);
        }
    }
}

// ========== APP MAIN ==========
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32-S3 RF Survey Lab ===");
    ESP_LOGI(TAG, "Analisis pasivo del espectro 2.4 GHz (solo RX)");

    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Configurar pines CSN y SD_CS en HIGH
    gpio_set_direction(SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(SD_CS, 1);
    gpio_set_direction(CSN_1, GPIO_MODE_OUTPUT);
    gpio_set_level(CSN_1, 1);
    gpio_set_direction(CSN_2, GPIO_MODE_OUTPUT);
    gpio_set_level(CSN_2, 1);

    // 3. Inicializar botones
    init_buttons();

    // 4. Inicializar bus SPI (UNA SOLA VEZ)
    spi_bus_config_t buscfg = {
        .sclk_io_num = SPI_SCK,
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 5. Inicializar pantalla (PRIMERO)
    ESP_LOGI(TAG, "Inicializando pantalla...");
    init_display();

    // 6. Inicializar radios (DESPUÉS)
    ESP_LOGI(TAG, "Inicializando NRF24...");
    init_radios();

    if (radio1_is_connected()) {
        radio1_set_pa_level(0x03);   // PA_MAX
        radio1_set_data_rate(1);
        radio1_set_crc_length(2);
        radio1_set_auto_ack(false);
        radio1_set_retries(0, 0);
        ESP_LOGI(TAG, "Radio1 configurado");
    } else {
        ESP_LOGW(TAG, "Radio1 NO CONECTADO");
    }

    if (radio2_is_connected()) {
        radio2_set_pa_level(0x03);
        radio2_set_data_rate(1);
        radio2_set_crc_length(2);
        radio2_set_auto_ack(false);
        radio2_set_retries(0, 0);
        ESP_LOGI(TAG, "Radio2 configurado");
    } else {
        ESP_LOGW(TAG, "Radio2 NO CONECTADO");
    }

   // 7. DIAGNÓSTICO: Batería de pruebas de hardware
    ESP_LOGI(TAG, "Iniciando pruebas de diagnóstico NRF24...");
    bool test_ok = run_nrf24_diagnostics();

    if (test_ok) {
        ESP_LOGI(TAG, "✅ Diagnóstico OK: Módulos configurados y comunicando en ambos sentidos.");
    } else {
        ESP_LOGW(TAG, "❌ Diagnóstico FALLIDO: Revisa los logs de la etiqueta DIAG arriba.");
        ESP_LOGW(TAG, "   Posibles causas detectadas por las pruebas:");
        ESP_LOGW(TAG, "   - Fallo SPI: Cables desconectados o pines incorrectos.");
        ESP_LOGW(TAG, "   - Fallo TX/RX: Módulo defectuoso (clon sin PA), falta condensador, o antenas sueltas.");
    }

    // 8. Crear cola de comunicación (ESTRICTAMENTE ANTES del engine)
    mode_queue = xQueueCreate(10, sizeof(uint16_t));
    if (mode_queue == NULL) {
        ESP_LOGE(TAG, "Error fatal: No se pudo crear la cola mode_queue");
        abort(); // Detiene el microcontrolador de forma segura en lugar de crashear después
    }

    // 9. Inicializar survey engine (RF Task en Core 1)
    ESP_LOGI(TAG, "Inicializando survey engine...");
    init_survey_engine();

   

    // 10. Interfaz gráfica LVGL
    lvgl_port_lock(0);

    lv_indev_t *indev_keypad = lv_indev_create();
    lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev_keypad, keypad_read);

    lv_group_t *g = lv_group_create();
    lv_indev_set_group(indev_keypad, g);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "RF SURVEY LAB");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF0000), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Select mode + press OK");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 35);

    lv_obj_t *roller = lv_roller_create(scr);
    lv_roller_set_options(roller, 
        "SWEEP\nFAST SWEEP\nADV+SWEEP\nINTERLEAVED\nBLE ALL\nBLE ADV\nBLE RANDOM\nLINK/PER", 
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 4);
    lv_obj_set_width(roller, 200);
    lv_obj_align(roller, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(roller, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_color(roller, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0xFF0000), LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);

    lv_obj_add_event_cb(roller, roller_event_cb, LV_EVENT_KEY, NULL);

    lv_group_add_obj(g, roller);
    lv_group_set_editing(g, true);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Sistema listo. Usa botones UP/DOWN/OK, BACK para detener.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}