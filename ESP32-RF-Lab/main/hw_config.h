#ifndef HW_CONFIG_H
#define HW_CONFIG_H

// --- PINES SPI COMPARTIDOS ---
#define SPI_SCK   12   // Reloj SPI
#define SPI_MOSI  11   // Master Out Slave In
#define SPI_MISO  13   // Master In Slave Out

// --- PINES PANTALLA ---
#define TFT_CS    15   // Chip Select de la pantalla
#define TFT_DC    17   // Data/Command
#define TFT_RST   16   // Reset
#define LCD_H_RES 240  // Resolución horizontal
#define LCD_V_RES 240  // Resolución vertical

// --- PINES ANTENAS Y SD ---
#define CE_1      9    // Chip Enable radio 1
#define CSN_1     10   // Chip Select radio 1
#define CE_2      47   // Chip Enable radio 2
#define CSN_2     21   // Chip Select radio 2
#define SD_CS     14   // Chip Select de la tarjeta SD (desactivado)

// --- PINES BOTONES ---
#define BTN_UP    1    // Botón arriba (para menú)
#define BTN_DOWN  2    // Botón abajo
#define BTN_OK    19   // Botón OK / Enter
#define BTN_BACK  20   // Botón atrás / ESC

#endif