#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "radios_nrf24.h"

static const char *TAG = "DIAG";

// TEST 1: Verificar lectura y escritura SPI puros
static bool test_spi_registers(void) {
    uint8_t test_ch = 45;
    radio1_set_channel(test_ch);
    radio2_set_channel(test_ch);

    // Nota: Necesitas asegurarte de que tienes una función equivalente a get_channel()
    uint8_t read_ch1 = radio1_get_channel(); 
    uint8_t read_ch2 = radio2_get_channel();

    bool pass1 = (read_ch1 == test_ch);
    bool pass2 = (read_ch2 == test_ch);
    
    ESP_LOGI(TAG, "SPI R1: %s (leído: %d) | SPI R2: %s (leído: %d)", 
             pass1 ? "PASS" : "FAIL", read_ch1,
             pass2 ? "PASS" : "FAIL", read_ch2);
             
    return (pass1 && pass2);
}

// TEST 2: Transmisión masiva R1 -> R2
static bool test_tx_rx_r1_to_r2(void) {
    int packets_to_send = 100;
    int packets_received = 0;
    char payload[32] = "TEST_R1_R2_PAYLOAD";

    // Fijar parámetros limpios
    radio1_set_channel(10);
    radio2_set_channel(10);
    
    radio2_rx_mode(); // R2 escucha
    radio1_tx_mode(); // R1 habla

    for(int i = 0; i < packets_to_send; i++) {
        radio1_transmit((uint8_t*)payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(5)); // Pausa para que el aire se limpie
        
        // Comprobar si R2 ha recibido algo
        if (radio2_data_ready()) { 
            uint8_t rx_buffer[32];
            radio2_get_payload(rx_buffer);
            packets_received++;
        }
    }

    float loss = 100.0 - ((packets_received / (float)packets_to_send) * 100.0);
    ESP_LOGI(TAG, "R1 -> R2 | Enviados: %d | Recibidos: %d | Pérdida: %.1f%%", 
             packets_to_send, packets_received, loss);
    
    return (loss < 10.0); // Se considera PASS si la pérdida es razonable
}

// TEST 3: Transmisión masiva R2 -> R1
static bool test_tx_rx_r2_to_r1(void) {
    int packets_to_send = 100;
    int packets_received = 0;
    char payload[32] = "TEST_R2_R1_PAYLOAD";

    // Fijar parámetros limpios
    radio1_set_channel(10);
    radio2_set_channel(10);
    
    radio1_rx_mode(); // R1 escucha
    radio2_tx_mode(); // R2 habla

    for(int i = 0; i < packets_to_send; i++) {
        radio2_transmit((uint8_t*)payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(5)); // Pausa para que el aire se limpie
        
        // Comprobar si R1 ha recibido algo
        if (radio1_data_ready()) { 
            uint8_t rx_buffer[32];
            radio1_get_payload(rx_buffer);
            packets_received++;
        }
    }

    float loss = 100.0 - ((packets_received / (float)packets_to_send) * 100.0);
    ESP_LOGI(TAG, "R2 -> R1 | Enviados: %d | Recibidos: %d | Pérdida: %.1f%%", 
             packets_to_send, packets_received, loss);
    
    return (loss < 10.0); // Se considera PASS si la pérdida es menor al 10%
}

// TEST 4: Barrido de canales
static bool test_channel_sweep(void) {
    uint8_t test_channels[] = {2, 10, 45, 80};
    int num_channels = sizeof(test_channels) / sizeof(test_channels[0]);
    bool all_passed = true;
    char payload[32] = "CH_SWEEP_TEST";

    ESP_LOGI(TAG, "Iniciando barrido de canales...");

    for (int i = 0; i < num_channels; i++) {
        uint8_t ch = test_channels[i];
        radio1_set_channel(ch);
        radio2_set_channel(ch);
        
        radio2_rx_mode();
        radio1_tx_mode();
        
        bool ch_ok = false;
        
        // Mandamos hasta 10 paquetes, con que llegue 1 consideramos que el canal funciona
        for(int p = 0; p < 20; p++) {
            radio1_transmit((uint8_t*)payload, sizeof(payload));
            vTaskDelay(pdMS_TO_TICKS(20));
            
            if (radio2_data_ready()) {
                uint8_t rx_buffer[32];
                radio2_get_payload(rx_buffer);
                ch_ok = true;
                break; // El canal funciona, pasamos al siguiente
            }
        }
        
        ESP_LOGI(TAG, "Canal %d: %s", ch, ch_ok ? "PASS" : "FAIL");
        if (!ch_ok) {
            all_passed = false;
        }
    }
    
    return all_passed;
}

bool run_nrf24_diagnostics(void) {
    ESP_LOGI(TAG, "========== NRF24 DIAGNOSTICS ==========");
    bool ok = true;
    
    if (!test_spi_registers()) ok = false;
    if (!test_tx_rx_r1_to_r2()) ok = false;
    if (!test_tx_rx_r2_to_r1()) ok = false;
    if (!test_channel_sweep()) ok = false;
    
    ESP_LOGI(TAG, "=======================================");
    return ok;
}