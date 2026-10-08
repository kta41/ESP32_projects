#include "rf_survey_engine.h"
#include "radios_nrf24.h"
#include "hw_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>

static const char *TAG = "SURVEY";

// ========== CONFIGURACIÓN ==========
#define PLL_SETTLE_US 200   // Asentado del sintetizador tras cambiar de canal
#define DWELL_US      220   // Tiempo de escucha por canal antes de muestrear el RPD
#define MAX_CHANNEL   79    // 2400-2479 MHz (80 canales de 1 MHz)

// Modos de medición (índices alineados con el roller de la UI)
enum {
    MODE_SWEEP = 0,       // Barrido aleatorio de banda completa
    MODE_FAST_SWEEP,      // R1 barrido rápido, R2 barrido con asentado
    MODE_ADV_SWEEP,       // R1 canales advertising BLE, R2 banda completa
    MODE_INTERLEAVED,     // Alternancia advertising / aleatorio
    MODE_BLE_ALL,         // Los 40 canales BLE en secuencia
    MODE_BLE_ADV,         // Solo canales advertising (2, 26, 80)
    MODE_BLE_RANDOM,      // Aleatorio dentro de la tabla BLE
    MODE_LINK_PER,        // Test de tasa de error de paquetes R1 -> R2
    MODE_IDLE = 99
};

static const char *mode_names[] = {
    "SWEEP", "FAST SWEEP", "ADV+SWEEP", "INTERLEAVED",
    "BLE ALL", "BLE ADV", "BLE RANDOM", "LINK/PER"
};

extern QueueHandle_t mode_queue;

// ========== ESTADO GLOBAL ==========
volatile bool s_survey_active = false;
volatile uint16_t s_current_mode = MODE_IDLE;

// ========== CONTADORES DE OCUPACIÓN ==========
#define CHANNELS 80
static uint16_t occ1[CHANNELS];    // Muestras con RPD=1 (señal > -64 dBm) por canal, radio 1
static uint16_t occ2[CHANNELS];    // Ídem, radio 2
static uint32_t sample_count = 0;  // Muestras de ocupación totales
static uint32_t frames_seen = 0;   // Tramas válidas observadas (CRC correcto)

// Estado de barrido
static uint8_t ble_idx1 = 0;
static uint8_t ble_idx2 = 20;
static uint8_t ble_adv_idx = 0;
static uint8_t tracking_cycle = 0;

// Estado del test PER
static uint32_t per_sent = 0;
static uint32_t per_received = 0;
static uint16_t per_seq = 0;

// ========== XORSHIFT ==========
static uint32_t xor_state = 0xDEADBEEF;
static inline uint32_t xorshift32(void) {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}
static inline uint8_t rand_channel(void) {
    return (uint8_t)(xorshift32() % (MAX_CHANNEL + 1));
}

// ========== TABLAS DE CANALES ==========
static const uint8_t ble_adv_channels[] = { 2, 26, 80 };

static const uint8_t ble_all_channels[] = {
     2,  4,  6,  8, 10, 12, 14, 16, 18, 20,
    22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
    42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
    62, 64, 66, 68, 70, 72, 74, 76, 78, 80,
};
#define BLE_ALL_CH_COUNT (sizeof(ble_all_channels) / sizeof(ble_all_channels[0]))

// ========== MUESTREO ==========
// RPD (Received Power Detector, registro 0x09): se activa si la potencia
// recibida supera ~-64 dBm con la radio en RX. El latch se reinicia al
// cambiar de canal, por lo que cada lectura es una muestra independiente
// del canal sintonizado en ese momento.
static void sample_channel(uint8_t ch1, uint8_t ch2) {
    if (ch1 < CHANNELS && radio1_rpd()) occ1[ch1]++;
    if (ch2 < CHANNELS && radio2_rpd()) occ2[ch2]++;

    // Tramas válidas observadas en el entorno (tráfico real, CRC OK)
    if (radio1_data_ready()) {
        uint8_t buf[32];
        radio1_get_payload(buf);
        frames_seen++;
    }
    if (radio2_data_ready()) {
        uint8_t buf[32];
        radio2_get_payload(buf);
        frames_seen++;
    }
    sample_count++;
}

static void scan_ch1(uint8_t channel) {
    radio1_set_channel(channel);
    esp_rom_delay_us(PLL_SETTLE_US + DWELL_US);
}

static void scan_ch2(uint8_t channel) {
    radio2_set_channel(channel);
    esp_rom_delay_us(PLL_SETTLE_US + DWELL_US);
}

// ========== MODOS DE MEDICIÓN ==========

// Modo 0: SWEEP - Ambos radios muestrean aleatoriamente toda la banda
static void mode_sweep(void) {
    uint8_t c1 = rand_channel();
    uint8_t c2 = rand_channel();
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 1: FAST SWEEP - R1 salta sin asentado (energía de banda ancha), R2 mide estable
static void mode_fast_sweep(void) {
    uint8_t c1 = rand_channel();
    radio1_set_channel(c1);
    esp_rom_delay_us(10);
    uint8_t c2 = rand_channel();
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 2: ADV+SWEEP - R1 vigila advertising BLE, R2 recorre toda la banda
static void mode_adv_sweep(void) {
    uint8_t c1 = ble_adv_channels[ble_adv_idx];
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    uint8_t c2 = rand_channel();
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 3: INTERLEAVED - Alterna foco advertising y muestreo aleatorio
static void mode_interleaved(void) {
    uint8_t c1, c2;
    if (tracking_cycle & 1) {
        c1 = rand_channel();
        c2 = ble_adv_channels[ble_adv_idx];
    } else {
        c1 = ble_adv_channels[ble_adv_idx];
        c2 = rand_channel();
    }
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    tracking_cycle++;
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 4: BLE ALL - Ambos radios recorren los 40 canales BLE
static void mode_ble_all(void) {
    uint8_t c1 = ble_all_channels[ble_idx1];
    uint8_t c2 = ble_all_channels[ble_idx2];
    ble_idx1 = (ble_idx1 + 1) % BLE_ALL_CH_COUNT;
    ble_idx2 = (ble_idx2 + 1) % BLE_ALL_CH_COUNT;
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 5: BLE ADV - Ambos radios monitorizan solo advertising
static void mode_ble_adv(void) {
    uint8_t c1 = ble_adv_channels[ble_adv_idx];
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    uint8_t c2 = ble_adv_channels[ble_adv_idx];
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 6: BLE RANDOM - Muestreo aleatorio dentro de la tabla BLE
static void mode_ble_random(void) {
    uint8_t c1 = ble_all_channels[xorshift32() % BLE_ALL_CH_COUNT];
    uint8_t c2 = ble_all_channels[xorshift32() % BLE_ALL_CH_COUNT];
    scan_ch1(c1);
    scan_ch2(c2);
    sample_channel(c1, c2);
}

// Modo 7: LINK/PER - Test de enlace punto a punto entre las DOS radios
// del propio dispositivo (R1 transmite, R2 recibe). Es tráfico normal del
// estándar entre dos módulos propios: no se genera interferencia alguna.
static void mode_link_per(void) {
    uint8_t payload[32];
    memset(payload, 0, sizeof(payload));
    payload[0] = 'P';
    payload[1] = 'E';
    payload[2] = 'R';
    payload[4] = (per_seq >> 8) & 0xFF;
    payload[5] = per_seq & 0xFF;
    per_seq++;

    radio1_transmit(payload, sizeof(payload));
    per_sent++;
    esp_rom_delay_us(250);   // Tiempo de vuelo + procesado en RX

    if (radio2_data_ready()) {
        uint8_t rx[32];
        radio2_get_payload(rx);
        per_received++;
    }

    if (per_sent % 200 == 0) {
        float per = 100.0f * (1.0f - ((float)per_received / (float)per_sent));
        ESP_LOGI(TAG, "[PER] ch=%d enviados=%lu recibidos=%lu PER=%.1f%%",
                 radio1_get_channel(),
                 (unsigned long)per_sent,
                 (unsigned long)per_received,
                 per);

        // Rotar canal cada bloque para caracterizar toda la banda
        static uint8_t per_ch = 45;
        per_ch = (per_ch >= 78) ? 2 : (uint8_t)(per_ch + 4);
        radio1_set_channel(per_ch);
        radio2_set_channel(per_ch);
    }

    vTaskDelay(1);   // ~1 paquete por tick; cede CPU
}

// ========== INFORME DE OCUPACIÓN ==========
static void log_occupancy(void) {
    if (sample_count == 0) return;

    uint8_t top1 = 0, top2 = 0;
    for (int i = 1; i < CHANNELS; i++) {
        if (occ1[i] > occ1[top1]) top1 = (uint8_t)i;
        if (occ2[i] > occ2[top2]) top2 = (uint8_t)i;
    }

    float p1 = 100.0f * occ1[top1] / (float)sample_count;
    float p2 = 100.0f * occ2[top2] / (float)sample_count;

    ESP_LOGI(TAG, "Ocupacion: muestras=%lu | R1 pico ch=%d (%.1f%%) | R2 pico ch=%d (%.1f%%) | tramas validas=%lu",
             (unsigned long)sample_count, top1, p1, top2, p2,
             (unsigned long)frames_seen);
}

// ========== FUNCIÓN DE PARADA ==========
void survey_stop(void) {
    s_survey_active = false;
    s_current_mode = MODE_IDLE;
    radios_stop();
    ESP_LOGI(TAG, "STOP! (modo seguro)");
}

// ========== TAREA RF (CORE 1) ==========
static void rf_task(void *pvParameters) {
    uint16_t current_mode = MODE_IDLE;
    uint16_t received_mode;
    uint32_t status_count = 0;
    uint32_t loop_count = 0;

    ESP_LOGI(TAG, "Survey task iniciada en Core 1");

    while (1) {
        TickType_t wait_ticks = (current_mode == MODE_IDLE) ? portMAX_DELAY : 0;

        if (xQueueReceive(mode_queue, &received_mode, wait_ticks) == pdTRUE) {
            if (current_mode == received_mode && received_mode != MODE_IDLE) {
                // Mismo modo otra vez -> toggle de parada
                current_mode = MODE_IDLE;
                radios_stop();
                s_survey_active = false;
                ESP_LOGW(TAG, "Medición DETENIDA (toggle)");
            } else {
                current_mode = received_mode;
                s_current_mode = current_mode;
                if (current_mode != MODE_IDLE) {
                    if (current_mode == MODE_LINK_PER) {
                        // Enlace interno entre las dos radios propias
                        radio2_rx_mode();
                        radio1_tx_mode();
                        per_sent = 0;
                        per_received = 0;
                        per_seq = 0;
                    } else {
                        radios_start_rx();
                        memset(occ1, 0, sizeof(occ1));
                        memset(occ2, 0, sizeof(occ2));
                        sample_count = 0;
                        frames_seen = 0;
                    }
                    s_survey_active = true;
                    ESP_LOGI(TAG, "Medición INICIADA - Modo: %d (%s)",
                             current_mode,
                             (current_mode < 8) ? mode_names[current_mode] : "?");
                    // Resetear variables de barrido
                    ble_idx1 = 0;
                    ble_idx2 = 20;
                    ble_adv_idx = 0;
                    tracking_cycle = 0;
                } else {
                    radios_stop();
                    s_survey_active = false;
                }
            }
            status_count = 0;
            loop_count = 0;
        }

        if (current_mode != MODE_IDLE && s_survey_active) {
            switch (current_mode) {
                case MODE_SWEEP:       mode_sweep(); break;
                case MODE_FAST_SWEEP:  mode_fast_sweep(); break;
                case MODE_ADV_SWEEP:   mode_adv_sweep(); break;
                case MODE_INTERLEAVED: mode_interleaved(); break;
                case MODE_BLE_ALL:     mode_ble_all(); break;
                case MODE_BLE_ADV:     mode_ble_adv(); break;
                case MODE_BLE_RANDOM:  mode_ble_random(); break;
                case MODE_LINK_PER:    mode_link_per(); break;
                default: break;
            }

            // Informe periódico de ocupación (modos de escaneo)
            if (++status_count >= 10000) {
                status_count = 0;
                if (current_mode != MODE_LINK_PER) {
                    log_occupancy();
                }
            }

            // Ceder CPU cada 10 iteraciones
            if (++loop_count >= 10) {
                loop_count = 0;
                vTaskDelay(1);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
            status_count = 0;
            loop_count = 0;
        }
    }
}

// ========== INICIALIZACIÓN ==========
void init_survey_engine(void) {
    xTaskCreatePinnedToCore(rf_task, "rf_task", 8192, NULL, configMAX_PRIORITIES - 1, NULL, 1);
    ESP_LOGI(TAG, "Survey engine inicializado (análisis RF pasivo en Core 1)");
}
