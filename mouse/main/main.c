#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "driver/gpio.h"

#define TAG "MOUSE"

#define LED_PIN (gpio_num_t)48

// ---------------------------------------------------------
// HID REPORT DESCRIPTOR
// ---------------------------------------------------------

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1))
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const *buffer,
    uint16_t bufsize)
{
}

// ---------------------------------------------------------
// USB CONFIGURATION DESCRIPTOR
// ---------------------------------------------------------

#define TUSB_DESC_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t hid_configuration_descriptor[] = {

    TUD_CONFIG_DESCRIPTOR(
        1,                          // Configuration number
        1,                          // Number of interfaces
        0,                          // String index
        TUSB_DESC_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        100                         // 100 mA
    ),

    TUD_HID_DESCRIPTOR(
        0,                          // Interface number
        0,                          // String index
        HID_ITF_PROTOCOL_NONE,
        sizeof(hid_report_descriptor),
        0x81,                       // Endpoint IN
        16,                         // Endpoint size
        10                          // Polling interval
    )
};

// ---------------------------------------------------------
// USB CALLBACKS
// ---------------------------------------------------------

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB HOST HA ENUMERADO EL DISPOSITIVO");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB HOST HA DESMONTADO EL DISPOSITIVO");
}

// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando Mouse Jiggler");

    // LED
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(LED_PIN, 0);

    // TinyUSB
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "TinyUSB iniciado");

    while (true) {

        if (tud_mounted()) {

            uint32_t random = esp_random();

            int8_t x = (int8_t)((random % 5) - 2);
            int8_t y = (int8_t)(((random >> 4) % 5) - 2);

            if (x != 0 || y != 0) {

                ESP_LOGI(
                    TAG,
                    "Movimiento: X=%d Y=%d",
                    x,
                    y
                );

                gpio_set_level(LED_PIN, 1);

                tud_hid_mouse_report(
                    1,
                    0x00,
                    x,
                    y,
                    0,
                    0
                );

                vTaskDelay(pdMS_TO_TICKS(150));

                tud_hid_mouse_report(
                    1,
                    0x00,
                    -x,
                    -y,
                    0,
                    0
                );

                gpio_set_level(LED_PIN, 0);
            }

        } else {

            ESP_LOGW(TAG, "USB HID todavía no está montado");

        }

        // Durante las pruebas: 5 segundos
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}