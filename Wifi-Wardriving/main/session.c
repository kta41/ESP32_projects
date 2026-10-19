/*
 * session — identidad de sesión (ROADMAP §3.4).
 */

#include "session.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "SESSION";

static char s_session_id[9];   /* 8 hex + '\0' */
static uint32_t s_scan_id = 0; /* último scan_id entregado */

void session_init(void)
{
    /* 32 bits aleatorios → 8 caracteres hex (§3.4). */
    uint32_t aleatorio = esp_random();
    snprintf(s_session_id, sizeof(s_session_id), "%08" PRIx32, aleatorio);
    s_scan_id = 0;
    ESP_LOGI(TAG, "Sesión nueva: session_id=%s", s_session_id);
}

const char *session_get_id(void)
{
    return s_session_id;
}

uint32_t session_next_scan_id(void)
{
    return ++s_scan_id;
}
