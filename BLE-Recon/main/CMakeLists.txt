idf_component_register(SRCS "main.c" "ble_scanner.c" "ble_parser.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_timer)
