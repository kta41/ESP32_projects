/*
 * scan_store — ring buffer de escaneos crudos en RAM (ROADMAP §3.6).
 */

#include "scan_store.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "SCAN_STORE";

static scan_result_t s_slots[CONFIG_WARDRIVE_RING_SLOTS];
static size_t s_head = 0;  /* índice del más antiguo */
static size_t s_count = 0; /* elementos ocupados */
static uint32_t s_lost_before = 0;

void scan_store_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_head = 0;
    s_count = 0;
    s_lost_before = 0;
}

void scan_store_push(const scan_result_t *scan)
{
    if (scan == NULL) {
        return;
    }

    if (s_count == CONFIG_WARDRIVE_RING_SLOTS) {
        uint32_t descartado = s_slots[s_head].scan_id;
        s_head = (s_head + 1) % CONFIG_WARDRIVE_RING_SLOTS;
        s_count--;
        s_lost_before++;
        ESP_LOGW(TAG, "Ring lleno (%d slots): descartado scan_id=#%" PRIu32
                      " (lost_before=%" PRIu32 ")",
                 CONFIG_WARDRIVE_RING_SLOTS, descartado, s_lost_before);
    }

    size_t tail = (s_head + s_count) % CONFIG_WARDRIVE_RING_SLOTS;
    s_slots[tail] = *scan;
    s_slots[tail].lost_before = s_lost_before;
    s_count++;
}

size_t scan_store_count(void)
{
    return s_count;
}

uint32_t scan_store_get_lost_before(void)
{
    return s_lost_before;
}

size_t scan_store_peek_batch(const scan_result_t **out_items, size_t max_items)
{
    if (out_items == NULL || max_items == 0) {
        return 0;
    }

    size_t n = (s_count < max_items) ? s_count : max_items;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (s_head + i) % CONFIG_WARDRIVE_RING_SLOTS;
        /* Mantener lost_before sincronizado con el acumulado actual. */
        s_slots[idx].lost_before = s_lost_before;
        out_items[i] = &s_slots[idx];
    }
    return n;
}

void scan_store_drop_n(size_t count)
{
    if (count > s_count) {
        count = s_count;
    }
    s_head = (s_head + count) % CONFIG_WARDRIVE_RING_SLOTS;
    s_count -= count;
}

size_t scan_store_drop_before_id(uint32_t first_bad_scan_id)
{
    size_t drained = 0;
    while (s_count > 0) {
        if (s_slots[s_head].scan_id >= first_bad_scan_id) {
            break;
        }
        s_head = (s_head + 1) % CONFIG_WARDRIVE_RING_SLOTS;
        s_count--;
        drained++;
    }
    return drained;
}

void scan_store_mark_buffered(size_t count)
{
    if (count > s_count) {
        count = s_count;
    }
    for (size_t i = 0; i < count; i++) {
        size_t idx = (s_head + i) % CONFIG_WARDRIVE_RING_SLOTS;
        s_slots[idx].buffered = true;
    }
}
