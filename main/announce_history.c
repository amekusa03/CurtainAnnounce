#include "announce_history.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "ntp_client.h"

static const char *TAG = "ANNOUNCE_HIST";

static announce_history_item_t s_history[ANNOUNCE_HISTORY_MAX_ENTRIES];
static int s_count = 0;
static int s_head = 0;
static SemaphoreHandle_t s_mutex = NULL;

void announce_history_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    s_count = 0;
    s_head = 0;
    memset(s_history, 0, sizeof(s_history));
}

void announce_history_add(const char *action_name, const char *detail,
                          float outdoor_temp, float indoor_temp, float lux, bool success)
{
    if (!s_mutex) {
        announce_history_init();
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire history mutex");
        return;
    }

    announce_history_item_t *item = &s_history[s_head];
    item->timestamp = ntp_client_get_now();

    struct tm timeinfo;
    localtime_r(&item->timestamp, &timeinfo);
    if (timeinfo.tm_year > (2020 - 1900)) {
        strftime(item->time_str, sizeof(item->time_str), "%Y/%m/%d %H:%M:%S", &timeinfo);
    } else {
        snprintf(item->time_str, sizeof(item->time_str), "NTP未同期");
    }

    strncpy(item->action_name, action_name ? action_name : "不明", sizeof(item->action_name) - 1);
    item->action_name[sizeof(item->action_name) - 1] = '\0';

    strncpy(item->detail, detail ? detail : "", sizeof(item->detail) - 1);
    item->detail[sizeof(item->detail) - 1] = '\0';

    item->outdoor_temp = outdoor_temp;
    item->indoor_temp = indoor_temp;
    item->lux = lux;
    item->success = success;

    s_head = (s_head + 1) % ANNOUNCE_HISTORY_MAX_ENTRIES;
    if (s_count < ANNOUNCE_HISTORY_MAX_ENTRIES) {
        s_count++;
    }

    ESP_LOGI(TAG, "Recorded history: [%s] %s (success=%d)", item->time_str, item->action_name, success);
    xSemaphoreGive(s_mutex);
}

int announce_history_get_count(void)
{
    return s_count;
}

int announce_history_get_items(announce_history_item_t *out_items, int max_count)
{
    if (!s_mutex || !out_items || max_count <= 0) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;

    int n = (s_count < max_count) ? s_count : max_count;
    for (int i = 0; i < n; i++) {
        // Return latest items first
        int idx = (s_head - 1 - i + ANNOUNCE_HISTORY_MAX_ENTRIES) % ANNOUNCE_HISTORY_MAX_ENTRIES;
        out_items[i] = s_history[idx];
    }

    xSemaphoreGive(s_mutex);
    return n;
}
