#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "spiffs_storage.h"
#include "wifi_manager.h"
#include "ntp_client.h"
#include "http_server.h"
#include "data_fetcher.h"
#include "thermal_logic.h"
#include "cast_client.h"
#include "announce_history.h"
#include "web_reporter.h"

static const char *TAG = "MAIN_APP";

static char s_last_action_title[64] = "なし";
static char s_last_action_time[64] = "";

static void monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Window & Curtain Announce Monitor Loop...");

    // Wait until Wi-Fi is connected
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Wait for initial NTP sync (or max 10 seconds)
    int wait_ntp_sec = 0;
    while (!ntp_client_is_time_set() && wait_ntp_sec < 10) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_ntp_sec++;
    }

    if (ntp_client_is_time_set()) {
        ESP_LOGI(TAG, "NTP Time synchronized successfully. Current hour: %d", ntp_client_get_current_hour());
    } else {
        ESP_LOGW(TAG, "NTP not yet synchronized, proceeding with local timer.");
    }

    while (1) {
        weather_data_t weather;
        esp_err_t err = data_fetcher_poll(&weather);

        if (err == ESP_OK && weather.valid) {
            // Add outdoor & indoor samples
            thermal_logic_add_sample(weather.outdoor_temp, weather.lux, weather.indoor_temp);

            // Evaluate state machine and thermal trend
            announce_action_t action = thermal_logic_evaluate();
            if (action != ANNOUNCE_NONE) {
                ESP_LOGI(TAG, ">>> Triggered Action: %s <<<", thermal_logic_action_to_str(action));

                const char *audio_path = thermal_logic_action_to_audio_path(action);
                char media_url[256];
                // Form URL e.g. http://192.168.11.xx/spiffs/win_open.mp3
                snprintf(media_url, sizeof(media_url), "http://%s%s",
                         wifi_manager_get_ip_str(), audio_path);

                ESP_LOGI(TAG, "Casting to Google Home (%s)... URL: %s",
                         CONFIG_GOOGLE_HOME_IP, media_url);

                esp_err_t cast_res = cast_client_play_url(CONFIG_GOOGLE_HOME_IP,
                                                         CONFIG_GOOGLE_HOME_PORT,
                                                         media_url);
                bool is_ok = (cast_res == ESP_OK);
                if (is_ok) {
                    thermal_logic_confirm_action(action);
                    ESP_LOGI(TAG, "Announcement broadcast successfully delivered.");
                } else {
                    ESP_LOGE(TAG, "Failed to cast announcement to Google Home.");
                }

                char detail[128];
                snprintf(detail, sizeof(detail), "外気: %.1f℃ / 室内: %.1f℃ / 照度: %.1flx",
                         weather.outdoor_temp, weather.indoor_temp, weather.lux);

                announce_history_add(thermal_logic_action_to_title(action),
                                     detail,
                                     weather.outdoor_temp,
                                     weather.indoor_temp,
                                     weather.lux,
                                     is_ok);

                // Update last action info
                const char *title = thermal_logic_action_to_title(action);
                strncpy(s_last_action_title, title, sizeof(s_last_action_title) - 1);
                s_last_action_title[sizeof(s_last_action_title) - 1] = '\0';

                time_t now = ntp_client_get_now();
                if (now == 0) now = time(NULL);
                struct tm ti;
                localtime_r(&now, &ti);
                strftime(s_last_action_time, sizeof(s_last_action_time), "%Y-%m-%d %H:%M", &ti);

                // Immediate report on triggered action
                web_reporter_send(weather.outdoor_temp,
                                  weather.indoor_temp,
                                  weather.lux,
                                  thermal_logic_get_curtain_state(),
                                  thermal_logic_get_window_state(),
                                  s_last_action_title,
                                  s_last_action_time);
            } else {
                // Periodic 5-minute report
                web_reporter_send(weather.outdoor_temp,
                                  weather.indoor_temp,
                                  weather.lux,
                                  thermal_logic_get_curtain_state(),
                                  thermal_logic_get_window_state(),
                                  s_last_action_title,
                                  s_last_action_time);
            }
        } else {
            ESP_LOGW(TAG, "Failed to fetch weather data.");
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DATA_POLL_INTERVAL_SEC * 1000));
    }
}

static void init_onboard_led(void)
{
#if defined(CONFIG_ONBOARD_LED_GPIO) && (CONFIG_ONBOARD_LED_GPIO >= 0)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ONBOARD_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_ONBOARD_LED_GPIO, CONFIG_ONBOARD_LED_OFF_LEVEL);
    ESP_LOGI(TAG, "Onboard LED (GPIO %d) set to OFF", CONFIG_ONBOARD_LED_GPIO);
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Window & Curtain Announce System Starting ===");

    // 0. Turn off onboard LED if present
    init_onboard_led();

    // 1. Initialize NVS (required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize SPIFFS
    ESP_ERROR_CHECK(spiffs_storage_init());

    // 3. Initialize Thermal Logic & History
    thermal_logic_init();
    announce_history_init();

    // 4. Initialize Data Fetcher
    ESP_ERROR_CHECK(data_fetcher_init());

    // 5. Initialize Cast Client
    ESP_ERROR_CHECK(cast_client_init());

    // 5.1 Initialize Web Reporter
    ESP_ERROR_CHECK(web_reporter_init());

    // 6. Initialize Wi-Fi
    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    wifi_manager_init_sta();

    // 7. Start Local HTTP Server for audio delivery
    ESP_ERROR_CHECK(http_server_start());

    // 8. Start NTP sync
    ESP_ERROR_CHECK(ntp_client_init());

    // 9. Start background monitoring task (with 16KB stack for TLS cast and HTTP poll)
    xTaskCreatePinnedToCore(monitor_task, "monitor_task", 16384, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "System initialization finished.");
}
