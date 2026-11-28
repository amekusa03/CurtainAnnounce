#include "web_reporter.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "app_config.h"
#include "ntp_client.h"

static const char *TAG = "WEB_REPORTER";

esp_err_t web_reporter_init(void)
{
    ESP_LOGI(TAG, "Web reporter initialized. Target URL: %s", CONFIG_CURTAIN_REPORT_URL);
    return ESP_OK;
}

static const char* curtain_state_str(curtain_state_t state)
{
    switch (state) {
        case CURTAIN_STATE_OPEN:   return "OPEN";
        case CURTAIN_STATE_CLOSED: return "CLOSED";
        default:                   return "UNKNOWN";
    }
}

static const char* window_state_str(window_state_t state)
{
    switch (state) {
        case WINDOW_STATE_OPEN:   return "OPEN";
        case WINDOW_STATE_CLOSED: return "CLOSED";
        default:                  return "UNKNOWN";
    }
}

esp_err_t web_reporter_send(float outdoor_temp,
                            float indoor_temp,
                            float lux,
                            curtain_state_t curtain_state,
                            window_state_t window_state,
                            const char *last_action,
                            const char *action_time)
{
    time_t now = ntp_client_get_now();
    if (now == 0) {
        now = time(NULL);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "timestamp", (double)now);
    cJSON_AddNumberToObject(root, "outdoor_temp", outdoor_temp);
    cJSON_AddNumberToObject(root, "indoor_temp", indoor_temp);
    cJSON_AddNumberToObject(root, "lux", lux);
    cJSON_AddStringToObject(root, "curtain_state", curtain_state_str(curtain_state));
    cJSON_AddStringToObject(root, "window_state", window_state_str(window_state));
    cJSON_AddStringToObject(root, "last_action", last_action ? last_action : "なし");
    cJSON_AddStringToObject(root, "action_time", action_time ? action_time : "");

    char *json_payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_payload) {
        ESP_LOGE(TAG, "Failed to format JSON payload");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reporting to %s: %s", CONFIG_CURTAIN_REPORT_URL, json_payload);

    esp_http_client_config_t config = {
        .url = CONFIG_CURTAIN_REPORT_URL,
        .timeout_ms = 8000,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(json_payload);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
#if defined(CONFIG_CURTAIN_API_KEY)
    if (strlen(CONFIG_CURTAIN_API_KEY) > 0) {
        esp_http_client_set_header(client, "x-api-key", CONFIG_CURTAIN_API_KEY);
    }
#endif

    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI(TAG, "Cloud report delivered successfully. HTTP Status: %d", status_code);
        } else {
            ESP_LOGW(TAG, "Cloud report responded with HTTP %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to send HTTPS POST: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_payload);

    return err;
}
