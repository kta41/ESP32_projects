idf_component_register(SRCS "nrf24_diagnostics.c" "main.c" "radios_nrf24.c" "rf_survey_engine.c" "display_st7789.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver nvs_flash)