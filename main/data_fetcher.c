#include "data_fetcher.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "app_config.h"

static const char *TAG = "DATA_FETCHER";

esp_err_t data_fetcher_init(void)
{
    ESP_LOGI(TAG, "Data fetcher initialized with URL: %s", CONFIG_WEATHER_API_URL);
    return ESP_OK;
}

esp_err_t data_fetcher_poll(weather_data_t *out_data)
{
    if (!out_data) return ESP_ERR_INVALID_ARG;
    memset(out_data, 0, sizeof(weather_data_t));

    char response_buffer[1024] = {0};
    int total_read = 0;

    esp_http_client_config_t config = {
        .url = CONFIG_WEATHER_API_URL,
        .timeout_ms = 5000,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            total_read = esp_http_client_read_response(client, response_buffer, sizeof(response_buffer) - 1);
            if (total_read > 0) {
                response_buffer[total_read] = '\0';
                cJSON *root = cJSON_Parse(response_buffer);
                if (root) {
                    cJSON *item_lux       = cJSON_GetObjectItem(root, "lux");
                    cJSON *item_temp      = cJSON_GetObjectItem(root, "temp_celsius");
                    cJSON *item_out_temp  = cJSON_GetObjectItem(root, "outdoor_temp_celsius");
                    cJSON *item_out_valid = cJSON_GetObjectItem(root, "outdoor_valid");

                    if (cJSON_IsNumber(item_lux)) {
                        out_data->lux = (float)item_lux->valuedouble;
                    }
                    if (cJSON_IsNumber(item_temp)) {
                        out_data->indoor_temp = (float)item_temp->valuedouble;
                    }
                    if (cJSON_IsNumber(item_out_temp)) {
                        out_data->outdoor_temp = (float)item_out_temp->valuedouble;
                    }
                    if (cJSON_IsBool(item_out_valid)) {
                        out_data->outdoor_valid = cJSON_IsTrue(item_out_valid);
                    } else {
                        out_data->outdoor_valid = true;
                    }

                    out_data->valid = true;

                    ESP_LOGI(TAG, "Sensor API fetch success: Room=%.1f C, Lux=%.1f, Outdoor=%.1f C (valid=%d)",
                             out_data->indoor_temp, out_data->lux, out_data->outdoor_temp, out_data->outdoor_valid);

                    cJSON_Delete(root);
                    esp_http_client_close(client);
                    esp_http_client_cleanup(client);
                    return ESP_OK;
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON response");
                }
            }
        } else {
            ESP_LOGW(TAG, "Sensor API HTTP status code: %d", status_code);
        }
        esp_http_client_close(client);
    } else {
        ESP_LOGW(TAG, "Failed to connect to Sensor API (%s): %s", CONFIG_WEATHER_API_URL, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}
