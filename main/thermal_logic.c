#include "thermal_logic.h"
#include <math.h>
#include "esp_log.h"
#include "app_config.h"
#include "ntp_client.h"

static const char *TAG = "THERMAL_LOGIC";

static climate_sample_t s_history[CONFIG_HISTORY_BUFFER_SIZE];
static int s_sample_count = 0;
static int s_head_idx = 0;
static curtain_state_t s_curtain_state = CURTAIN_STATE_UNKNOWN;
static time_t s_last_announce_time = 0;

void thermal_logic_init(void)
{
    s_sample_count = 0;
    s_head_idx = 0;
    s_curtain_state = CURTAIN_STATE_UNKNOWN;
    s_last_announce_time = 0;
}

void thermal_logic_add_sample(float outdoor_temp, float lux, float indoor_temp)
{
    s_history[s_head_idx].outdoor_temp = outdoor_temp;
    s_history[s_head_idx].lux = lux;
    s_history[s_head_idx].indoor_temp = indoor_temp;
    s_history[s_head_idx].timestamp = ntp_client_get_now();

    s_head_idx = (s_head_idx + 1) % CONFIG_HISTORY_BUFFER_SIZE;
    if (s_sample_count < CONFIG_HISTORY_BUFFER_SIZE) {
        s_sample_count++;
    }

    ESP_LOGI(TAG, "Sample added: Temp=%.1f C, Lux=%.1f lx, Indoor=%.1f C (Total %d samples)",
             outdoor_temp, lux, indoor_temp, s_sample_count);
}

bool thermal_logic_get_latest_sample(climate_sample_t *out_sample)
{
    if (s_sample_count == 0 || !out_sample) return false;
    int latest_idx = (s_head_idx - 1 + CONFIG_HISTORY_BUFFER_SIZE) % CONFIG_HISTORY_BUFFER_SIZE;
    *out_sample = s_history[latest_idx];
    return true;
}

// Simple linear slope calculation (units per minute or sample step)
static void calculate_slopes(float *temp_slope, float *lux_slope, float *latest_temp, float *latest_lux)
{
    if (s_sample_count < 2) {
        *temp_slope = 0.0f;
        *lux_slope = 0.0f;
        return;
    }

    int oldest_idx = (s_sample_count < CONFIG_HISTORY_BUFFER_SIZE) ? 0 : s_head_idx;
    int latest_idx = (s_head_idx - 1 + CONFIG_HISTORY_BUFFER_SIZE) % CONFIG_HISTORY_BUFFER_SIZE;

    *latest_temp = s_history[latest_idx].outdoor_temp;
    *latest_lux = s_history[latest_idx].lux;

    float delta_temp = s_history[latest_idx].outdoor_temp - s_history[oldest_idx].outdoor_temp;
    float delta_lux = s_history[latest_idx].lux - s_history[oldest_idx].lux;

    *temp_slope = delta_temp / (float)(s_sample_count - 1);
    *lux_slope = delta_lux / (float)(s_sample_count - 1);
}

announce_action_t thermal_logic_evaluate(void)
{
    if (s_sample_count < 2) {
        ESP_LOGD(TAG, "Not enough samples yet to evaluate trend");
        return ANNOUNCE_NONE;
    }

    // Time-of-day quiet mode check (e.g. 22:00 to 07:00)
    int hour = ntp_client_get_current_hour();
    if (hour >= CONFIG_QUIET_HOUR_START || hour < CONFIG_QUIET_HOUR_END) {
        ESP_LOGD(TAG, "Quiet hours active (current hour: %d). Skipping announcement.", hour);
        return ANNOUNCE_NONE;
    }

    // Cooldown check
    time_t now = ntp_client_get_now();
    if (s_last_announce_time > 0 && (now - s_last_announce_time) < CONFIG_ANNOUNCE_COOLDOWN_SEC) {
        ESP_LOGD(TAG, "Cooldown active (%ld / %d sec remaining).",
                 (long)(CONFIG_ANNOUNCE_COOLDOWN_SEC - (now - s_last_announce_time)),
                 CONFIG_ANNOUNCE_COOLDOWN_SEC);
        return ANNOUNCE_NONE;
    }

    float temp_slope = 0.0f, lux_slope = 0.0f;
    float current_temp = 0.0f, current_lux = 0.0f;
    calculate_slopes(&temp_slope, &lux_slope, &current_temp, &current_lux);

    ESP_LOGI(TAG, "Trend: Temp=%.1f C (slope: %+.2f), Lux=%.1f lx (slope: %+.2f)",
             current_temp, temp_slope, current_lux, lux_slope);

    // Season heuristic: simplified based on outdoor temp (or month via NTP)
    // < 18 deg C treated as heating/winter profile, >= 24 deg C treated as cooling/summer profile
    bool is_cold_season = (current_temp < CONFIG_TEMP_WINTER_SEASON_MAX);
    bool is_warm_season = (current_temp >= CONFIG_TEMP_SUMMER_SEASON_MIN);

    announce_action_t action = ANNOUNCE_NONE;

    if (is_cold_season) {
        // 冬・採光（開）: 室内が明るくなり始め(180lx以上)、光量上昇 ＋ 気温上昇傾向
        if (current_lux >= CONFIG_LUX_WINTER_OPEN_MIN && lux_slope > 10.0f && temp_slope >= -0.1f) {
            if (s_curtain_state != CURTAIN_STATE_OPEN) {
                action = ANNOUNCE_WINTER_LIGHT_OPEN;
            }
        }
        // 冬・断熱（閉）: 室内が薄暗くなる(150lx以下) または日没傾向(下降傾き) ＋ 気温低下
        else if ((current_lux <= CONFIG_LUX_WINTER_CLOSE_THRESHOLD || lux_slope < -20.0f) && temp_slope <= 0.1f) {
            // 既に閉まっている場合はアナウンスしない
            if (s_curtain_state != CURTAIN_STATE_CLOSED) {
                action = ANNOUNCE_WINTER_COLD_CLOSE;
            }
        }
    } else if (is_warm_season) {
        // 夏・遮熱（閉）: 光量直射急増(1000lx超)
        if (current_lux >= CONFIG_LUX_SUMMER_HOT_CLOSE && lux_slope > 30.0f) {
            if (s_curtain_state != CURTAIN_STATE_CLOSED) {
                action = ANNOUNCE_SUMMER_HOT_CLOSE;
            }
        }
        // 夏・排熱（開）: 外気温低下 ＋ 薄暗く日射消失(150lx以下)
        else if (current_lux <= CONFIG_LUX_SUMMER_COOL_OPEN && temp_slope < -0.2f) {
            if (s_curtain_state != CURTAIN_STATE_OPEN) {
                action = ANNOUNCE_SUMMER_COOL_OPEN;
            }
        }
    }

    return action;
}

curtain_state_t thermal_logic_get_curtain_state(void)
{
    return s_curtain_state;
}

void thermal_logic_confirm_action(announce_action_t action)
{
    if (action == ANNOUNCE_NONE) return;

    s_last_announce_time = ntp_client_get_now();
    if (action == ANNOUNCE_WINTER_LIGHT_OPEN || action == ANNOUNCE_SUMMER_COOL_OPEN) {
        s_curtain_state = CURTAIN_STATE_OPEN;
    } else if (action == ANNOUNCE_WINTER_COLD_CLOSE || action == ANNOUNCE_SUMMER_HOT_CLOSE) {
        s_curtain_state = CURTAIN_STATE_CLOSED;
    }
}

const char* thermal_logic_action_to_str(announce_action_t action)
{
    switch (action) {
        case ANNOUNCE_WINTER_LIGHT_OPEN: return "冬・採光（開）: 日差しが入ってきました。カーテンを開けて部屋を暖めましょう。";
        case ANNOUNCE_WINTER_COLD_CLOSE: return "冬・断熱（閉）: 日が落ちて冷え込んできました。カーテンを閉めて暖気を逃がさないようにしましょう。";
        case ANNOUNCE_SUMMER_HOT_CLOSE:  return "夏・遮熱（閉）: 日差しが強くなっています。カーテンを閉めて室温の上昇を防ぎましょう。";
        case ANNOUNCE_SUMMER_COOL_OPEN:  return "夏・排熱（開）: 外が涼しくなりました。カーテンを開けて熱気を逃がしましょう。";
        default:                         return "アナウンスなし";
    }
}

const char* thermal_logic_action_to_title(announce_action_t action)
{
    switch (action) {
        case ANNOUNCE_WINTER_LIGHT_OPEN: return "冬・採光（開）";
        case ANNOUNCE_WINTER_COLD_CLOSE: return "冬・断熱（閉）";
        case ANNOUNCE_SUMMER_HOT_CLOSE:  return "夏・遮熱（閉）";
        case ANNOUNCE_SUMMER_COOL_OPEN:  return "夏・排熱（開）";
        default:                         return "アナウンスなし";
    }
}

const char* thermal_logic_action_to_audio_path(announce_action_t action)
{
    switch (action) {
        case ANNOUNCE_WINTER_LIGHT_OPEN: return AUDIO_WINTER_LIGHT_OPEN_PATH;
        case ANNOUNCE_WINTER_COLD_CLOSE: return AUDIO_WINTER_COLD_CLOSE_PATH;
        case ANNOUNCE_SUMMER_HOT_CLOSE:  return AUDIO_SUMMER_HOT_CLOSE_PATH;
        case ANNOUNCE_SUMMER_COOL_OPEN:  return AUDIO_SUMMER_COOL_OPEN_PATH;
        default:                         return AUDIO_TEST_PATH;
    }
}
