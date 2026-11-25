#pragma once

#include <stdbool.h>
#include <stdint.h>

// Include sensitive credentials (Wi-Fi, Google Home IP, API URLs)
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "secrets.h not found! Please copy 'main/secrets.example.h' to 'main/secrets.h' and fill in your credentials."
#endif

// Wi-Fi Operational Settings
#define CONFIG_WIFI_MAXIMUM_RETRY   5

// NTP Configuration
#define CONFIG_NTP_SERVER           "pool.ntp.org"
#define CONFIG_TIMEZONE_JST         "JST-9"

// Google Home Port
#define CONFIG_GOOGLE_HOME_PORT     8009

// Local HTTP Server Port (for serving MP3 audio)
#define CONFIG_LOCAL_HTTP_PORT      80

// Weather / Outdoor Data Polling
#define CONFIG_DATA_POLL_INTERVAL_SEC   300   // 5 minutes
#define CONFIG_HISTORY_BUFFER_SIZE      6     // 6 samples * 5min = 30min trend

// Cooldown & Time of Day Restrictions
#define CONFIG_ANNOUNCE_COOLDOWN_SEC    3600  // 1 hour cooldown between announcements
#define CONFIG_QUIET_HOUR_START         22    // 22:00 ~ 07:00 quiet time
#define CONFIG_QUIET_HOUR_END           7

// Light Level (Lux) Thresholds (Based on actual environment: <=150 lx is dim)
#define CONFIG_LUX_DIM_THRESHOLD            150.0f // 室内が薄暗くなる基準照度
#define CONFIG_LUX_WINTER_OPEN_MIN          180.0f // 冬の朝: 採光が有効になる照度
#define CONFIG_LUX_WINTER_CLOSE_THRESHOLD   150.0f // 冬の夕方: 薄暗くなり断熱が必要になる照度
#define CONFIG_LUX_SUMMER_HOT_CLOSE         1000.0f// 夏の昼: 強い直射日光（遮熱）
#define CONFIG_LUX_SUMMER_COOL_OPEN         150.0f // 夏の夕〜夜: 日射消失（排熱）

// Temperature Thresholds
#define CONFIG_TEMP_WINTER_SEASON_MAX       18.0f  // 18℃未満は暖房/冬プロファイル
#define CONFIG_TEMP_SUMMER_SEASON_MIN       24.0f  // 24℃以上は冷房/夏プロファイル

// Midday Peak Ventilation Settings (冬〜春 日中換気: 11:00〜14:00, 照度>=500lx, 外気温>=20℃, 1日1回限定)
#define CONFIG_VENT_HOUR_START              11     // 11:00
#define CONFIG_VENT_HOUR_END                14     // 14:00
#define CONFIG_LUX_MIDDAY_VENT_MIN          500.0f // 日照基準 (500 lx以上)
#define CONFIG_TEMP_MIDDAY_VENT_MIN         20.0f  // 外気温基準 (20.0℃以上)

// Onboard LED Settings (ESP32-S3 SuperMini has blue LED on GPIO 8, Active-Low: 1 = OFF)
#define CONFIG_ONBOARD_LED_GPIO             8
#define CONFIG_ONBOARD_LED_OFF_LEVEL        1

// Audio file paths (served at http://<IP>/<filename>.mp3)
#define AUDIO_WINTER_LIGHT_OPEN_PATH    "/win_open.mp3"
#define AUDIO_WINTER_COLD_CLOSE_PATH    "/win_close.mp3"
#define AUDIO_SUMMER_HOT_CLOSE_PATH     "/sum_close.mp3"
#define AUDIO_SUMMER_COOL_OPEN_PATH     "/sum_open.mp3"
#define AUDIO_MIDDAY_VENT_PATH          "/mid_vent.mp3"
#define AUDIO_TEST_PATH                 "/test.mp3"

