#include "cast_client.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CAST_CLIENT";

static int encode_protobuf_string(uint8_t *buf, int field_number, const char *str)
{
    int len = strlen(str);
    int idx = 0;
    buf[idx++] = (field_number << 3) | 2; // wire type 2: length-delimited
    if (len < 128) {
        buf[idx++] = (uint8_t)len;
    } else {
        buf[idx++] = (uint8_t)((len & 0x7F) | 0x80);
        buf[idx++] = (uint8_t)(len >> 7);
    }
    memcpy(&buf[idx], str, len);
    idx += len;
    return idx;
}

static int encode_protobuf_varint(uint8_t *buf, int field_number, uint32_t val)
{
    int idx = 0;
    buf[idx++] = (field_number << 3) | 0; // wire type 0: varint
    while (val >= 0x80) {
        buf[idx++] = (uint8_t)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf[idx++] = (uint8_t)val;
    return idx;
}

static int build_cast_frame(uint8_t *frame_buf, int max_len,
                            const char *src, const char *dst,
                            const char *ns, const char *payload_json)
{
    uint8_t *pb_buf = (uint8_t *)malloc(1024);
    if (!pb_buf) return -1;

    int pb_len = 0;
    pb_len += encode_protobuf_varint(&pb_buf[pb_len], 1, 0); // protocol_version = 0
    pb_len += encode_protobuf_string(&pb_buf[pb_len], 2, src);
    pb_len += encode_protobuf_string(&pb_buf[pb_len], 3, dst);
    pb_len += encode_protobuf_string(&pb_buf[pb_len], 4, ns);
    pb_len += encode_protobuf_varint(&pb_buf[pb_len], 5, 0); // payload_type = STRING
    pb_len += encode_protobuf_string(&pb_buf[pb_len], 6, payload_json);

    if (pb_len + 4 > max_len) {
        ESP_LOGE(TAG, "Cast frame buffer overflow");
        free(pb_buf);
        return -1;
    }

    frame_buf[0] = (pb_len >> 24) & 0xFF;
    frame_buf[1] = (pb_len >> 16) & 0xFF;
    frame_buf[2] = (pb_len >> 8) & 0xFF;
    frame_buf[3] = pb_len & 0xFF;
    memcpy(&frame_buf[4], pb_buf, pb_len);

    free(pb_buf);
    return pb_len + 4;
}

static esp_err_t send_cast_message(esp_tls_t *tls, const char *src, const char *dst,
                                   const char *ns, const char *payload_json)
{
    uint8_t *frame = (uint8_t *)malloc(1500);
    if (!frame) return ESP_ERR_NO_MEM;

    int frame_len = build_cast_frame(frame, 1500, src, dst, ns, payload_json);
    if (frame_len < 0) {
        free(frame);
        return ESP_FAIL;
    }

    size_t written = 0;
    while (written < (size_t)frame_len) {
        ssize_t ret = esp_tls_conn_write(tls, frame + written, frame_len - written);
        if (ret <= 0) {
            ESP_LOGE(TAG, "esp_tls_conn_write failed, ret = %d", (int)ret);
            free(frame);
            return ESP_FAIL;
        }
        written += ret;
    }
    ESP_LOGI(TAG, "Sent %d bytes to Google Home (ns: %s, dst: %s)", frame_len, ns, dst);
    free(frame);
    return ESP_OK;
}

static const char *find_pattern(const uint8_t *buf, size_t len, const char *pattern)
{
    size_t pat_len = strlen(pattern);
    if (pat_len == 0 || pat_len > len) return NULL;
    for (size_t i = 0; i <= len - pat_len; i++) {
        if (memcmp(buf + i, pattern, pat_len) == 0) {
            return (const char *)(buf + i);
        }
    }
    return NULL;
}

// Read incoming Cast messages and extract transportId
static esp_err_t wait_for_transport_id(esp_tls_t *tls, char *out_transport_id, size_t max_len, int max_seconds)
{
    const size_t buf_size = 4096;
    uint8_t *rx_buf = (uint8_t *)malloc(buf_size);
    if (!rx_buf) return ESP_ERR_NO_MEM;

    size_t total_len = 0;
    int max_attempts = max_seconds * 10;
    esp_err_t result = ESP_ERR_TIMEOUT;

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        if (total_len + 1024 >= buf_size) {
            memmove(rx_buf, rx_buf + total_len - 512, 512);
            total_len = 512;
        }

        ssize_t ret = esp_tls_conn_read(tls, rx_buf + total_len, buf_size - total_len - 1);
        if (ret > 0) {
            total_len += ret;
            ESP_LOGI(TAG, "Cast TLS received %d bytes (total buffered: %u)", (int)ret, (unsigned int)total_len);

            // Search for "transportId" (handles binary protobuf framing with embedded nulls)
            const char *p = find_pattern(rx_buf, total_len, "\"transportId\"");
            if (p) {
                p += strlen("\"transportId\"");
                const char *end_buf = (const char *)(rx_buf + total_len);
                while (p < end_buf && (*p == ' ' || *p == ':' || *p == '\"')) {
                    p++;
                }
                const char *start = p;
                while (p < end_buf && *p != '\"' && *p != ',' && *p != '}' && *p != ' ') {
                    p++;
                }
                size_t id_len = p - start;
                if (id_len > 0 && id_len < max_len) {
                    memcpy(out_transport_id, start, id_len);
                    out_transport_id[id_len] = '\0';
                    ESP_LOGI(TAG, "Successfully extracted transportId: %s", out_transport_id);
                    result = ESP_OK;
                    break;
                }
            }
        } else if (ret == -0x004C || ret == ESP_TLS_ERR_SSL_WANT_READ || ret == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    free(rx_buf);
    return result;
}

esp_err_t cast_client_init(void)
{
    return ESP_OK;
}

esp_err_t cast_client_play_url(const char *google_home_ip, uint16_t port, const char *media_url)
{
    if (!google_home_ip || !media_url) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Connecting to Google Home at %s:%u...", google_home_ip, port);

    esp_tls_cfg_t cfg = {
        .non_block = false,
        .timeout_ms = 1000,
        .skip_common_name = true,
    };

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to initialize TLS");
        return ESP_FAIL;
    }

    if (esp_tls_conn_new_sync(google_home_ip, strlen(google_home_ip), port, &cfg, tls) <= 0) {
        ESP_LOGE(TAG, "Failed to establish TLS connection to %s:%u", google_home_ip, port);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TLS Connected to Google Home.");

    // 1. CONNECT to receiver-0
    send_cast_message(tls, "sender-0", "receiver-0",
                      "urn:x-cast:com.google.cast.tp.connection",
                      "{\"type\":\"CONNECT\"}");

    // 2. LAUNCH Default Media Receiver (appId: CC1AD845)
    send_cast_message(tls, "sender-0", "receiver-0",
                      "urn:x-cast:com.google.cast.receiver",
                      "{\"type\":\"LAUNCH\",\"appId\":\"CC1AD845\",\"requestId\":1}");

    // 3. Wait for Google Home to launch receiver and return transportId (up to 5 seconds)
    char transport_id[64] = "transport-0";
    if (wait_for_transport_id(tls, transport_id, sizeof(transport_id), 5) == ESP_OK) {
        ESP_LOGI(TAG, "Using dynamic transportId: %s", transport_id);
    } else {
        ESP_LOGW(TAG, "Timed out waiting for transportId, falling back to transport-0");
    }

    // 4. CONNECT to transport destination
    send_cast_message(tls, "sender-0", transport_id,
                      "urn:x-cast:com.google.cast.tp.connection",
                      "{\"type\":\"CONNECT\"}");

    // 5. LOAD media
    char *load_json = (char *)malloc(512);
    if (!load_json) {
        esp_tls_conn_destroy(tls);
        return ESP_ERR_NO_MEM;
    }
    snprintf(load_json, 512,
             "{\"type\":\"LOAD\",\"requestId\":2,\"sessionId\":\"%s\","
             "\"media\":{\"contentId\":\"%s\","
             "\"contentType\":\"audio/mp3\",\"streamType\":\"BUFFERED\"},"
             "\"autoplay\":true,\"currentTime\":0}",
             transport_id, media_url);

    send_cast_message(tls, "sender-0", transport_id,
                      "urn:x-cast:com.google.cast.media",
                      load_json);
    free(load_json);

    ESP_LOGI(TAG, "Cast LOAD request sent for: %s", media_url);

    // Wait for playback duration (approx 9 seconds)
    vTaskDelay(pdMS_TO_TICKS(9000));

    // 6. STOP application on receiver-0
    char stop_json[128];
    snprintf(stop_json, sizeof(stop_json),
             "{\"type\":\"STOP\",\"requestId\":3,\"sessionId\":\"%s\"}",
             transport_id);
    send_cast_message(tls, "sender-0", "receiver-0",
                      "urn:x-cast:com.google.cast.receiver",
                      stop_json);

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_tls_conn_destroy(tls);
    ESP_LOGI(TAG, "Cast session ended successfully.");
    return ESP_OK;
}
