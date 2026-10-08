#ifndef DISPLAY_ST7789_H
#define DISPLAY_ST7789_H

#include "esp_lvgl_port.h"

/**
 * @brief Inicializa la pantalla ST7789 y la integra con LVGL.
 * @return Puntero al display de LVGL.
 */
lv_disp_t* init_display(void);

#endif