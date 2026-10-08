#ifndef RF_SURVEY_ENGINE_H
#define RF_SURVEY_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Inicializa el motor de medición (tarea RF de análisis en Core 1)
void init_survey_engine(void);

// Detiene la medición en curso y pone ambas radios en reposo
void survey_stop(void);

// Variables de estado global
extern volatile bool s_survey_active;
extern volatile uint16_t s_current_mode;

#endif // RF_SURVEY_ENGINE_H
