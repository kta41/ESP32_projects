idf_component_register(SRCS "pcap_store.c" "promisc.c" "scan_store.c" "pusher.c" "main.c" "wifi_mgr.c" "jsonl_writer.c" "scanner.c" "session.c" "ble_fusion.c" "ble_parser.c" "deauth_tx.c" "deauth_udp.c"
                           
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif
                                     esp_timer esp_hw_support log freertos lwip
                                     esp_http_client bt)
