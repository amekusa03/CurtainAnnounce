#pragma once

#include <stdbool.h>
#include <time.h>

typedef enum {
    CURTAIN_STATE_UNKNOWN = 0,
    CURTAIN_STATE_OPEN,
    CURTAIN_STATE_CLOSED
} curtain_state_t;

typedef enum {
    ANNOUNCE_NONE = 0,
    ANNOUNCE_WINTER_LIGHT_OPEN,   // 冬・採光（開）
    ANNOUNCE_WINTER_COLD_CLOSE,   // 冬・断熱（閉）
    ANNOUNCE_SUMMER_HOT_CLOSE,    // 夏・遮熱（閉）
    ANNOUNCE_SUMMER_COOL_OPEN     // 夏・排熱（開）
} announce_action_t;

typedef struct {
    float outdoor_temp; // 外気温 [℃]
    float lux;          // 光量 [lux]
    float indoor_temp;  // 室内温度 [℃]（センサー未搭載時は負の値などの無効値）
    time_t timestamp;
} climate_sample_t;

void thermal_logic_init(void);
void thermal_logic_add_sample(float outdoor_temp, float lux, float indoor_temp);
bool thermal_logic_get_latest_sample(climate_sample_t *out_sample);
announce_action_t thermal_logic_evaluate(void);
curtain_state_t thermal_logic_get_curtain_state(void);
void thermal_logic_confirm_action(announce_action_t action);
const char* thermal_logic_action_to_str(announce_action_t action);
const char* thermal_logic_action_to_title(announce_action_t action);
const char* thermal_logic_action_to_audio_path(announce_action_t action);
