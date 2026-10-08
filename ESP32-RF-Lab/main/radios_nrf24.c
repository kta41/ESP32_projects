#include "radios_nrf24.h"
#include "hw_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "NRF24";

static spi_device_handle_t spi1_handle = NULL;
static spi_device_handle_t spi2_handle = NULL;

static bool radio1_ok = false;
static bool radio2_ok = false;

// ========== REGISTROS ==========
#define NRF_CONFIG      0x00
#define NRF_EN_AA       0x01
#define NRF_EN_RXADDR   0x02
#define NRF_SETUP_AW    0x03
#define NRF_SETUP_RETR  0x04
#define NRF_RF_CH       0x05
#define NRF_RF_SETUP    0x06
#define NRF_STATUS      0x07
#define NRF_OBSERVE_TX  0x08
#define NRF_RPD         0x09
#define NRF_RX_ADDR_P0  0x0A
#define NRF_RX_ADDR_P1  0x0B
#define NRF_RX_ADDR_P2  0x0C
#define NRF_RX_ADDR_P3  0x0D
#define NRF_RX_ADDR_P4  0x0E
#define NRF_RX_ADDR_P5  0x0F
#define NRF_TX_ADDR     0x10
#define NRF_RX_PW_P0    0x11
#define NRF_RX_PW_P1    0x12
#define NRF_RX_PW_P2    0x13
#define NRF_RX_PW_P3    0x14
#define NRF_RX_PW_P4    0x15
#define NRF_RX_PW_P5    0x16
#define NRF_FIFO_STATUS 0x17

#define NRF_CMD_W_REG   0x20
#define NRF_CMD_R_REG   0x00
#define NRF_CMD_W_TX_PAYLOAD 0xA0
#define NRF_CMD_FLUSH_TX 0xE1
#define NRF_CMD_FLUSH_RX 0xE2
#define NRF_CMD_REUSE_TX_PL 0xE3
#define NRF_CMD_ACTIVATE 0x50

#define RF_SETUP_PLL_LOCK (1 << 4)

// ========== FUNCIONES SPI ==========
static uint8_t nrf24_write_reg(spi_device_handle_t spi, uint8_t reg, uint8_t value) {
    uint8_t tx_buf[2] = { NRF_CMD_W_REG | (reg & 0x1F), value };
    uint8_t rx_buf[2];
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    esp_err_t ret = spi_device_transmit(spi, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI write error: %d", ret);
        return 0xFF;
    }
    return rx_buf[1];
}

static uint8_t nrf24_read_reg(spi_device_handle_t spi, uint8_t reg) {
    uint8_t tx_buf[2] = { NRF_CMD_R_REG | (reg & 0x1F), 0xFF };
    uint8_t rx_buf[2];
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    esp_err_t ret = spi_device_transmit(spi, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI read error: %d", ret);
        return 0xFF;
    }
    return rx_buf[1];
}

static void nrf24_flush_tx(spi_device_handle_t spi) {
    uint8_t tx_buf = NRF_CMD_FLUSH_TX;
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &tx_buf,
    };
    spi_device_transmit(spi, &trans);
}

static void nrf24_flush_rx(spi_device_handle_t spi) {
    uint8_t tx_buf = NRF_CMD_FLUSH_RX;
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &tx_buf,
    };
    spi_device_transmit(spi, &trans);
}

// ========== INICIALIZACIÓN ==========
void init_radios(void) {
    // 1. Pines CE a 0 (Standby)
    gpio_set_direction(CE_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(CE_2, GPIO_MODE_OUTPUT);
    gpio_set_level(CE_1, 0);
    gpio_set_level(CE_2, 0);
    
    // 2. Pines CSN a 1 (Inactivos)
    gpio_set_direction(CSN_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(CSN_2, GPIO_MODE_OUTPUT);
    gpio_set_level(CSN_1, 1);
    gpio_set_level(CSN_2, 1);

    // 3. Configurar dispositivos SPI
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = 2 * 1000 * 1000,  // 2 MHz
        .spics_io_num = CSN_1,
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi1_handle));
    devcfg.spics_io_num = CSN_2;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi2_handle));

    vTaskDelay(pdMS_TO_TICKS(10));

    // --- Radio 1 (Modo Neutro) ---
    nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x0F); 
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t cfg1 = nrf24_read_reg(spi1_handle, NRF_CONFIG);
    radio1_ok = (cfg1 == 0x0F);
    
    ESP_LOGI(TAG, "Radio1 CONFIG esperado=0x0F, leído=0x%02X %s", cfg1, radio1_ok ? "OK" : "FALLO");
    if (radio1_ok) {
        nrf24_write_reg(spi1_handle, NRF_EN_AA, 0x00);
        nrf24_write_reg(spi1_handle, NRF_EN_RXADDR, 0x01);
        nrf24_write_reg(spi1_handle, NRF_SETUP_AW, 0x03);
        nrf24_write_reg(spi1_handle, NRF_SETUP_RETR, 0x00);
        nrf24_write_reg(spi1_handle, NRF_RF_CH, 2);
        nrf24_write_reg(spi1_handle, NRF_RX_PW_P0, 32);
        nrf24_write_reg(spi1_handle, NRF_RF_SETUP, 0x01); // 1Mbps, 0dBm (Seguro)
        nrf24_flush_tx(spi1_handle);
        nrf24_flush_rx(spi1_handle);
        nrf24_write_reg(spi1_handle, NRF_STATUS, 0x70);
        ESP_LOGI(TAG, "Radio1 configurado en Standby");
    }

    // --- Radio 2 (Modo Neutro) ---
    nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x0F);
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t cfg2 = nrf24_read_reg(spi2_handle, NRF_CONFIG);
    radio2_ok = (cfg2 == 0x0F);
    
    ESP_LOGI(TAG, "Radio2 CONFIG esperado=0x0F, leído=0x%02X %s", cfg2, radio2_ok ? "OK" : "FALLO");
    if (radio2_ok) {
        nrf24_write_reg(spi2_handle, NRF_EN_AA, 0x00);
        nrf24_write_reg(spi2_handle, NRF_EN_RXADDR, 0x01);
        nrf24_write_reg(spi2_handle, NRF_SETUP_AW, 0x03);
        nrf24_write_reg(spi2_handle, NRF_SETUP_RETR, 0x00);
        nrf24_write_reg(spi2_handle, NRF_RF_CH, 2);
        nrf24_write_reg(spi2_handle, NRF_RX_PW_P0, 32);
        nrf24_write_reg(spi2_handle, NRF_RF_SETUP, 0x01);
        nrf24_flush_tx(spi2_handle);
        nrf24_flush_rx(spi2_handle);
        nrf24_write_reg(spi2_handle, NRF_STATUS, 0x70);
        ESP_LOGI(TAG, "Radio2 configurado en Standby");
    }
}

// ========== MODO RECEPCIÓN CONTINUA (ANÁLISIS) ==========
// Ambas radios en RX con CE activo: habilita el muestreo del RPD y la
// recepción de tramas válidas para los modos de análisis de espectro.
void radios_start_rx(void) {
    if (!radio1_ok && !radio2_ok) {
        ESP_LOGW(TAG, "No hay radios disponibles para RX");
        return;
    }

    if (radio1_ok) {
        gpio_set_level(CE_1, 0);
        nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x0F); // Power up, PRIM_RX, CRC 2 bytes
        nrf24_flush_rx(spi1_handle);
        nrf24_write_reg(spi1_handle, NRF_STATUS, 0x70);
        gpio_set_level(CE_1, 1);
    }
    if (radio2_ok) {
        gpio_set_level(CE_2, 0);
        nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x0F);
        nrf24_flush_rx(spi2_handle);
        nrf24_write_reg(spi2_handle, NRF_STATUS, 0x70);
        gpio_set_level(CE_2, 1);
    }
    esp_rom_delay_us(130);   // Estabilización del modo RX
    ESP_LOGI(TAG, "Radios en modo RX continuo");
}

void radios_stop(void) {
    gpio_set_level(CE_1, 0);
    gpio_set_level(CE_2, 0);
    // Limpiar el bit PLL_LOCK y volver a modo RX para ahorrar energía
    if (radio1_ok) {
        uint8_t rf_setup = nrf24_read_reg(spi1_handle, NRF_RF_SETUP);
        rf_setup &= ~RF_SETUP_PLL_LOCK;
        nrf24_write_reg(spi1_handle, NRF_RF_SETUP, rf_setup);
        nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x02); // Power down
    }
    if (radio2_ok) {
        uint8_t rf_setup = nrf24_read_reg(spi2_handle, NRF_RF_SETUP);
        rf_setup &= ~RF_SETUP_PLL_LOCK;
        nrf24_write_reg(spi2_handle, NRF_RF_SETUP, rf_setup);
        nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x02);
    }
    ESP_LOGI(TAG, "Radios detenidas (power-down)");
}

// ========== CANALES ==========
void radio1_set_channel(uint8_t ch) {
    if (!radio1_ok) return;
    nrf24_write_reg(spi1_handle, NRF_RF_CH, ch & 0x7F);
    static uint32_t count1 = 0;
    if (++count1 % 10 == 0) {
        ESP_LOGI(TAG, "[RF1] Canal -> %d", ch);
    }
}

void radio2_set_channel(uint8_t ch) {
    if (!radio2_ok) return;
    nrf24_write_reg(spi2_handle, NRF_RF_CH, ch & 0x7F);
    static uint32_t count2 = 0;
    if (++count2 % 10 == 0) {
        ESP_LOGI(TAG, "[RF2] Canal -> %d", ch);
    }
}

// ========== LECTURA DE CANAL ==========
uint8_t radio1_get_channel(void) {
    if (!radio1_ok) return 0xFF;
    return nrf24_read_reg(spi1_handle, NRF_RF_CH);
}
uint8_t radio2_get_channel(void) {
    if (!radio2_ok) return 0xFF;
    return nrf24_read_reg(spi2_handle, NRF_RF_CH);
}

// ========== ESTADO ==========
bool radio1_is_connected(void) { return radio1_ok; }
bool radio2_is_connected(void) { return radio2_ok; }

// ========== DETECTOR DE POTENCIA RECIBIDA (RPD) ==========
// El bit RPD (reg 0x09) se activa cuando la potencia recibida supera ~-64 dBm
// con la radio en modo RX. Se usa como indicador de ocupación del canal.
bool radio1_rpd(void) {
    if (!radio1_ok) return false;
    return (nrf24_read_reg(spi1_handle, NRF_RPD) & 0x01) != 0;
}

bool radio2_rpd(void) {
    if (!radio2_ok) return false;
    return (nrf24_read_reg(spi2_handle, NRF_RPD) & 0x01) != 0;
}

// ========== AUTO-TEST MEJORADO ==========
bool nrf24_self_test(void) {
    if (!radio1_ok || !radio2_ok) {
        ESP_LOGE(TAG, "Auto-test: ambos radios deben estar OK");
        return false;
    }

    ESP_LOGI(TAG, "=== INICIO AUTO-TEST TX -> RX ===");

    /*
     * Prueba normal nRF24:
     *
     *   Radio1 = TX
     *   Radio2 = RX
     *
     * No usamos PLL_LOCK, CW, REUSE_TX_PL ni RPD.
     */

    const uint8_t test_channel = 45;

    // Payload de prueba
    const uint8_t test_payload[4] = {
        0x54, 0x45, 0x53, 0x54   // "TEST"
    };

    bool received = false;
    uint8_t tx_status = 0xFF;
    uint8_t rx_status = 0xFF;

    // =========================================================
    // 1. DETENER AMBOS RADIOS
    // =========================================================

    gpio_set_level(CE_1, 0);
    gpio_set_level(CE_2, 0);

    nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x00);
    nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x00);

    vTaskDelay(pdMS_TO_TICKS(5));

    // =========================================================
    // 2. CONFIGURACIÓN COMÚN
    // =========================================================

    // Mismo canal
    nrf24_write_reg(spi1_handle, NRF_RF_CH, test_channel);
    nrf24_write_reg(spi2_handle, NRF_RF_CH, test_channel);

    // 1 Mbps + PA máximo
    //
    // RF_SETUP:
    // bit 3 = 0 -> 1 Mbps
    // bits 2:1 = 11 -> PA_MAX
    //
    nrf24_write_reg(spi1_handle, NRF_RF_SETUP, 0x06);
    nrf24_write_reg(spi2_handle, NRF_RF_SETUP, 0x06);

    // Desactivar ACK automático.
    // Así el test es unidireccional y sencillo.
    nrf24_write_reg(spi1_handle, NRF_EN_AA, 0x00);
    nrf24_write_reg(spi2_handle, NRF_EN_AA, 0x00);

    // Habilitar pipe 0 en RX
    nrf24_write_reg(spi2_handle, NRF_EN_RXADDR, 0x01);

    // Dirección de 5 bytes idéntica en TX y RX.
    const uint8_t address[5] = {
        0x54, 0x45, 0x53, 0x54, 0x31
    };

    // Dirección RX pipe 0
    {
        uint8_t cmd = NRF_CMD_W_REG | NRF_RX_ADDR_P0;
        uint8_t tx[6] = {
            cmd,
            address[0],
            address[1],
            address[2],
            address[3],
            address[4]
        };

        spi_transaction_t trans = {
            .length = 48,
            .tx_buffer = tx
        };

        spi_device_transmit(spi2_handle, &trans);
    }

    // Dirección TX
    {
        uint8_t cmd = NRF_CMD_W_REG | NRF_TX_ADDR;
        uint8_t tx[6] = {
            cmd,
            address[0],
            address[1],
            address[2],
            address[3],
            address[4]
        };

        spi_transaction_t trans = {
            .length = 48,
            .tx_buffer = tx
        };

        spi_device_transmit(spi1_handle, &trans);
    }

    // Payload fijo de 4 bytes
    nrf24_write_reg(spi2_handle, NRF_RX_PW_P0, 4);

    // Ancho de dirección = 5 bytes
    nrf24_write_reg(spi1_handle, NRF_SETUP_AW, 0x03);
    nrf24_write_reg(spi2_handle, NRF_SETUP_AW, 0x03);

    // =========================================================
    // 3. LIMPIAR FIFO Y FLAGS
    // =========================================================

    nrf24_flush_tx(spi1_handle);
    nrf24_flush_rx(spi1_handle);

    // Limpiar TX_DS, MAX_RT, RX_DR
    nrf24_write_reg(spi1_handle, NRF_STATUS, 0x70);
    nrf24_write_reg(spi2_handle, NRF_STATUS, 0x70);

    // =========================================================
    // 4. ENCENDER RADIO2 COMO RX
    // =========================================================

    /*
     * CONFIG:
     *
     * bit 1 = PWR_UP
     * bit 0 = PRIM_RX
     *
     * 0x03 = PWR_UP + PRIM_RX
     *
     * CRC de 1 byte:
     * bit 3 = EN_CRC
     */

    nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x0B);

    vTaskDelay(pdMS_TO_TICKS(5));

    gpio_set_level(CE_2, 1);

    // Dar tiempo al receptor para estabilizarse
    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "Radio2 RX activo en canal %d", test_channel);

    // =========================================================
    // 5. CONFIGURAR RADIO1 COMO TX
    // =========================================================

    /*
     * CONFIG:
     *
     * bit 1 = PWR_UP
     * bit 0 = PRIM_RX = 0
     * bit 3 = EN_CRC
     *
     * 0x0A = PWR_UP + EN_CRC
     */

    nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x0A);

    vTaskDelay(pdMS_TO_TICKS(5));

    // =========================================================
    // 6. CARGAR PAYLOAD
    // =========================================================

    {
        uint8_t tx_buf[5];

        tx_buf[0] = NRF_CMD_W_TX_PAYLOAD;
        memcpy(&tx_buf[1], test_payload, sizeof(test_payload));

        spi_transaction_t trans = {
            .length = 40,
            .tx_buffer = tx_buf
        };

        esp_err_t ret = spi_device_transmit(spi1_handle, &trans);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error cargando TX payload: %d", ret);

            gpio_set_level(CE_2, 0);
            nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x02);
            nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x02);

            return false;
        }
    }

    ESP_LOGI(TAG, "Payload TEST cargado en Radio1");

    // =========================================================
    // 7. TRANSMITIR
    // =========================================================

    gpio_set_level(CE_1, 1);

    // CE debe permanecer alto >10 us para iniciar TX
    esp_rom_delay_us(20);

    gpio_set_level(CE_1, 0);

    ESP_LOGI(TAG, "Radio1 TX iniciado");

    // =========================================================
    // 8. ESPERAR RX_DR EN RADIO2
    // =========================================================

    const int max_attempts = 100;

    for (int i = 0; i < max_attempts; i++) {

        rx_status = nrf24_read_reg(spi2_handle, NRF_STATUS);

        if (rx_status & (1 << 6)) {
            // RX_DR = bit 6
            received = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Leer también estado del transmisor
    tx_status = nrf24_read_reg(spi1_handle, NRF_STATUS);

    ESP_LOGI(
        TAG,
        "TX STATUS=0x%02X | RX STATUS=0x%02X",
        tx_status,
        rx_status
    );

    // =========================================================
    // 9. SI RECIBIMOS, LEER PAYLOAD
    // =========================================================

    if (received) {

        uint8_t cmd = 0x61; // R_RX_PAYLOAD
        uint8_t tx_buf[5] = {
            cmd,
            0xFF,
            0xFF,
            0xFF,
            0xFF
        };

        uint8_t rx_buf[5] = {0};

        spi_transaction_t trans = {
            .length = 40,
            .tx_buffer = tx_buf,
            .rx_buffer = rx_buf
        };

        esp_err_t ret = spi_device_transmit(spi2_handle, &trans);

        if (ret == ESP_OK) {

            ESP_LOGI(
                TAG,
                "RX PAYLOAD: %02X %02X %02X %02X",
                rx_buf[1],
                rx_buf[2],
                rx_buf[3],
                rx_buf[4]
            );

            if (memcmp(&rx_buf[1], test_payload, 4) == 0) {
                ESP_LOGI(TAG, "PAYLOAD CORRECTO: TX -> RX OK");
            } else {
                ESP_LOGW(TAG, "RX recibió datos, pero payload incorrecto");
                received = false;
            }

        } else {
            ESP_LOGE(TAG, "Error leyendo RX payload: %d", ret);
            received = false;
        }

        // Limpiar RX_DR
        nrf24_write_reg(spi2_handle, NRF_STATUS, 0x40);
    }

    // =========================================================
    // 10. DIAGNÓSTICO DEL TRANSMISOR
    // =========================================================

    if (tx_status & (1 << 5)) {
        ESP_LOGI(TAG, "Radio1: TX_DS -> transmisión completada");
    }

    if (tx_status & (1 << 4)) {
        ESP_LOGW(TAG, "Radio1: MAX_RT");
        nrf24_write_reg(spi1_handle, NRF_STATUS, 0x10);
    }

    // =========================================================
    // 11. APAGAR
    // =========================================================

    gpio_set_level(CE_1, 0);
    gpio_set_level(CE_2, 0);

    nrf24_flush_tx(spi1_handle);
    nrf24_flush_rx(spi2_handle);

    nrf24_write_reg(spi1_handle, NRF_CONFIG, 0x00);
    nrf24_write_reg(spi2_handle, NRF_CONFIG, 0x00);

    ESP_LOGI(TAG, "=== FIN AUTO-TEST ===");

    if (received) {
        ESP_LOGI(TAG, "RESULTADO: ✅ RADIO1 -> RADIO2 FUNCIONA");
    } else {
        ESP_LOGW(TAG, "RESULTADO: ❌ RADIO1 -> RADIO2 NO FUNCIONA");
    }

    return received;
}

// ========== CONFIGURACIÓN AVANZADA (sin cambios) ==========
void radio1_set_pa_level(uint8_t level) {
    if (!radio1_ok) return;
    uint8_t rf_setup = nrf24_read_reg(spi1_handle, NRF_RF_SETUP);
    rf_setup = (rf_setup & 0xF8) | (level & 0x07);
    nrf24_write_reg(spi1_handle, NRF_RF_SETUP, rf_setup);
}
void radio2_set_pa_level(uint8_t level) {
    if (!radio2_ok) return;
    uint8_t rf_setup = nrf24_read_reg(spi2_handle, NRF_RF_SETUP);
    rf_setup = (rf_setup & 0xF8) | (level & 0x07);
    nrf24_write_reg(spi2_handle, NRF_RF_SETUP, rf_setup);
}
void radio1_set_data_rate(uint8_t rate) {
    if (!radio1_ok) return;
    uint8_t rf_setup = nrf24_read_reg(spi1_handle, NRF_RF_SETUP);
    rf_setup = (rf_setup & 0xF7) | ((rate & 0x01) << 3);
    nrf24_write_reg(spi1_handle, NRF_RF_SETUP, rf_setup);
}
void radio2_set_data_rate(uint8_t rate) {
    if (!radio2_ok) return;
    uint8_t rf_setup = nrf24_read_reg(spi2_handle, NRF_RF_SETUP);
    rf_setup = (rf_setup & 0xF7) | ((rate & 0x01) << 3);
    nrf24_write_reg(spi2_handle, NRF_RF_SETUP, rf_setup);
}
void radio1_set_crc_length(uint8_t length) {
    if (!radio1_ok) return;
    uint8_t config = nrf24_read_reg(spi1_handle, NRF_CONFIG);
    config = (config & 0xF3) | ((length & 0x03) << 2);
    nrf24_write_reg(spi1_handle, NRF_CONFIG, config);
}
void radio2_set_crc_length(uint8_t length) {
    if (!radio2_ok) return;
    uint8_t config = nrf24_read_reg(spi2_handle, NRF_CONFIG);
    config = (config & 0xF3) | ((length & 0x03) << 2);
    nrf24_write_reg(spi2_handle, NRF_CONFIG, config);
}
void radio1_set_auto_ack(bool enable) {
    if (!radio1_ok) return;
    nrf24_write_reg(spi1_handle, NRF_EN_AA, enable ? 0x3F : 0x00);
}
void radio2_set_auto_ack(bool enable) {
    if (!radio2_ok) return;
    nrf24_write_reg(spi2_handle, NRF_EN_AA, enable ? 0x3F : 0x00);
}
void radio1_set_retries(uint8_t delay, uint8_t count) {
    if (!radio1_ok) return;
    nrf24_write_reg(spi1_handle, NRF_SETUP_RETR, ((delay & 0x0F) << 4) | (count & 0x0F));
}
void radio2_set_retries(uint8_t delay, uint8_t count) {
    if (!radio2_ok) return;
    nrf24_write_reg(spi2_handle, NRF_SETUP_RETR, ((delay & 0x0F) << 4) | (count & 0x0F));
}

// ========== MÉTODOS DE ESTADO TX/RX ==========
void radio1_tx_mode(void) {
    if (!radio1_ok) return;
    gpio_set_level(CE_1, 0);
    uint8_t conf = nrf24_read_reg(spi1_handle, NRF_CONFIG);
    nrf24_write_reg(spi1_handle, NRF_CONFIG, conf & ~0x01); // PRIM_RX = 0
    esp_rom_delay_us(130);
}

void radio2_tx_mode(void) {
    if (!radio2_ok) return;
    gpio_set_level(CE_2, 0);
    uint8_t conf = nrf24_read_reg(spi2_handle, NRF_CONFIG);
    nrf24_write_reg(spi2_handle, NRF_CONFIG, conf & ~0x01);
    esp_rom_delay_us(130);
}
void radio1_rx_mode(void) {
    if (!radio1_ok) return;
    gpio_set_level(CE_1, 0);
    nrf24_flush_rx(spi1_handle); // Limpiar basura acumulada
    nrf24_write_reg(spi1_handle, NRF_STATUS, 0x70); // Limpiar alarmas
    uint8_t conf = nrf24_read_reg(spi1_handle, NRF_CONFIG);
    nrf24_write_reg(spi1_handle, NRF_CONFIG, conf | 0x01); // PRIM_RX = 1
    gpio_set_level(CE_1, 1);
    esp_rom_delay_us(130);
}

void radio2_rx_mode(void) {
    if (!radio2_ok) return;
    gpio_set_level(CE_2, 0);
    nrf24_flush_rx(spi2_handle); // Limpiar basura acumulada
    nrf24_write_reg(spi2_handle, NRF_STATUS, 0x70); // Limpiar alarmas
    uint8_t conf = nrf24_read_reg(spi2_handle, NRF_CONFIG);
    nrf24_write_reg(spi2_handle, NRF_CONFIG, conf | 0x01);
    gpio_set_level(CE_2, 1);
    esp_rom_delay_us(130);
}

// ========== TRANSMISIÓN Y RECEPCIÓN ==========
void radio1_transmit(uint8_t *payload, uint8_t len) {
    if (!radio1_ok) return;
    nrf24_flush_tx(spi1_handle);
    nrf24_write_reg(spi1_handle, NRF_STATUS, 0x70); 
    
    uint8_t tx_buf[33] = { NRF_CMD_W_TX_PAYLOAD };
    memcpy(&tx_buf[1], payload, len > 32 ? 32 : len);
    
    spi_transaction_t trans = { .length = (1 + (len > 32 ? 32 : len)) * 8, .tx_buffer = tx_buf };
    spi_device_transmit(spi1_handle, &trans);
    
    gpio_set_level(CE_1, 1);
    esp_rom_delay_us(15);
    gpio_set_level(CE_1, 0);
}

void radio2_transmit(uint8_t *payload, uint8_t len) {
    if (!radio2_ok) return;
    nrf24_flush_tx(spi2_handle);
    nrf24_write_reg(spi2_handle, NRF_STATUS, 0x70); 
    
    uint8_t tx_buf[33] = { NRF_CMD_W_TX_PAYLOAD };
    memcpy(&tx_buf[1], payload, len > 32 ? 32 : len);
    
    spi_transaction_t trans = { .length = (1 + (len > 32 ? 32 : len)) * 8, .tx_buffer = tx_buf };
    spi_device_transmit(spi2_handle, &trans);
    
    gpio_set_level(CE_2, 1);
    esp_rom_delay_us(15);
    gpio_set_level(CE_2, 0);
}

bool radio1_data_ready(void) {
    if (!radio1_ok) return false;
    // Comprobar el bit 0 (RX_EMPTY) del registro NRF_FIFO_STATUS
    uint8_t fifo = nrf24_read_reg(spi1_handle, 0x17); 
    return (fifo & 0x01) == 0; // Si es 0, significa que NO está vacía (hay datos)
}

bool radio2_data_ready(void) {
    if (!radio2_ok) return false;
    uint8_t fifo = nrf24_read_reg(spi2_handle, 0x17); 
    return (fifo & 0x01) == 0;
}

void radio1_get_payload(uint8_t *buf) {
    if (!radio1_ok) return;
    uint8_t tx_buf[33] = { 0x61 }; // R_RX_PAYLOAD
    uint8_t rx_buf[33] = {0};
    spi_transaction_t trans = { .length = 33 * 8, .tx_buffer = tx_buf, .rx_buffer = rx_buf };
    spi_device_transmit(spi1_handle, &trans);
    memcpy(buf, &rx_buf[1], 32);
    nrf24_write_reg(spi1_handle, NRF_STATUS, 0x40); // Limpiar RX_DR
}

void radio2_get_payload(uint8_t *buf) {
    if (!radio2_ok) return;
    uint8_t tx_buf[33] = { 0x61 };
    uint8_t rx_buf[33] = {0};
    spi_transaction_t trans = { .length = 33 * 8, .tx_buffer = tx_buf, .rx_buffer = rx_buf };
    spi_device_transmit(spi2_handle, &trans);
    memcpy(buf, &rx_buf[1], 32);
    nrf24_write_reg(spi2_handle, NRF_STATUS, 0x40);
}