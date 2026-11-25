#include "ntp_client.h"
#include <string.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "NTP_CLIENT";
static bool s_time_synced = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    s_time_synced = true;
}

esp_err_t ntp_client_init(void)
{
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Set timezone to JST
    setenv("TZ", CONFIG_TIMEZONE_JST, 1);
    tzset();

    return ESP_OK;
}

bool ntp_client_is_time_set(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? (greater than Year 2020)
    return (timeinfo.tm_year > (2020 - 1900));
}

int ntp_client_get_current_hour(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_hour;
}

time_t ntp_client_get_now(void)
{
    time_t now;
    time(&now);
    return now;
}
