idf_component_register(SRCS "main.c" "wifi_mgr.c" "jsonl_writer.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif
                                     esp_timer esp_hw_support log freertos lwip)
