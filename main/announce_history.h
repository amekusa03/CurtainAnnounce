#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#define ANNOUNCE_HISTORY_MAX_ENTRIES 20

typedef struct {
    time_t timestamp;
    char time_str[32];      // "2026/11/24 23:15:30"
    char action_name[48];   // "冬・断熱（閉）", "テスト再生", etc.
    char detail[128];       // "照度 0.1lx / 外気 9.5℃ / 室温 24.2℃"
    float outdoor_temp;
    float indoor_temp;
    float lux;
    bool success;
} announce_history_item_t;

void announce_history_init(void);
void announce_history_add(const char *action_name, const char *detail,
                          float outdoor_temp, float indoor_temp, float lux, bool success);
int announce_history_get_count(void);
int announce_history_get_items(announce_history_item_t *out_items, int max_count);
