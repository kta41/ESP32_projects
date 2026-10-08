idf_component_register(SRCS "main.c" "ble_scanner.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_timer)
