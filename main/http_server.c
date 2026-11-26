#include "http_server.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spiffs_storage.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "cast_client.h"
#include "announce_history.h"
#include "thermal_logic.h"

static const char *TAG = "HTTP_SERVER";
static httpd_handle_t s_server = NULL;

#define SCRATCH_BUFSIZE 1024

static bool s_cast_in_progress = false;

// Task to play test audio on Google Home without blocking HTTP server
static void test_cast_task(void *pvParameters)
{
    char media_url[256];
    snprintf(media_url, sizeof(media_url), "http://%s%s",
             wifi_manager_get_ip_str(), AUDIO_TEST_PATH);

    ESP_LOGI(TAG, "Triggering test cast to %s: %s", CONFIG_GOOGLE_HOME_IP, media_url);
    esp_err_t res = cast_client_play_url(CONFIG_GOOGLE_HOME_IP, CONFIG_GOOGLE_HOME_PORT, media_url);

    climate_sample_t latest;
    bool has_sample = thermal_logic_get_latest_sample(&latest);
    float out_t = has_sample ? latest.outdoor_temp : 0.0f;
    float in_t = has_sample ? latest.indoor_temp : 0.0f;
    float lx = has_sample ? latest.lux : 0.0f;

    announce_history_add("テスト再生",
                         res == ESP_OK ? "Web管理画面よりテスト実行 (再生成功)" : "Web管理画面よりテスト実行 (接続失敗)",
                         out_t, in_t, lx, res == ESP_OK);

    s_cast_in_progress = false;
    vTaskDelete(NULL);
}

// Handler to trigger Google Home Cast
static esp_err_t test_cast_handler(httpd_req_t *req)
{
    if (s_cast_in_progress) {
        const char *resp = "{\"status\":\"busy\",\"message\":\"Cast already in progress\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
    s_cast_in_progress = true;
    xTaskCreate(test_cast_task, "test_cast_task", 16384, NULL, 5, NULL);
    const char *resp = "{\"status\":\"ok\",\"message\":\"Test cast requested\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Handler for /api/status
static esp_err_t status_api_handler(httpd_req_t *req)
{
    climate_sample_t latest;
    bool has_sample = thermal_logic_get_latest_sample(&latest);
    curtain_state_t state = thermal_logic_get_curtain_state();

    const char *state_str = "未判定";
    if (state == CURTAIN_STATE_OPEN) state_str = "開（採光・通風中）";
    else if (state == CURTAIN_STATE_CLOSED) state_str = "閉（保温・遮熱中）";

    char resp[384];
    snprintf(resp, sizeof(resp),
        "{\"curtain_state\":\"%s\",\"outdoor_temp\":%.1f,\"indoor_temp\":%.1f,\"lux\":%.1f,\"has_sample\":%s}",
        state_str,
        has_sample ? latest.outdoor_temp : 0.0f,
        has_sample ? latest.indoor_temp : 0.0f,
        has_sample ? latest.lux : 0.0f,
        has_sample ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Handler for /api/history
static esp_err_t history_api_handler(httpd_req_t *req)
{
    announce_history_item_t *items = (announce_history_item_t *)malloc(sizeof(announce_history_item_t) * ANNOUNCE_HISTORY_MAX_ENTRIES);
    if (!items) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int count = announce_history_get_items(items, ANNOUNCE_HISTORY_MAX_ENTRIES);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);

    char item_buf[512];
    for (int i = 0; i < count; i++) {
        int len = snprintf(item_buf, sizeof(item_buf),
            "%s{\"time\":\"%s\",\"action\":\"%s\",\"detail\":\"%s\",\"out_temp\":%.1f,\"in_temp\":%.1f,\"lux\":%.1f,\"success\":%s}",
            (i > 0) ? "," : "",
            items[i].time_str,
            items[i].action_name,
            items[i].detail,
            items[i].outdoor_temp,
            items[i].indoor_temp,
            items[i].lux,
            items[i].success ? "true" : "false"
        );
        httpd_resp_send_chunk(req, item_buf, len);
    }

    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);

    free(items);
    return ESP_OK;
}

// Root Dashboard Handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html lang=\"ja\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Curtain Announce System</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;margin:16px;background:#f0f2f5;color:#333;}"
        ".container{max-width:680px;margin:0 auto;}"
        ".card{background:#fff;border-radius:12px;padding:20px;margin-bottom:16px;box-shadow:0 2px 8px rgba(0,0,0,0.06);}"
        "h1{font-size:20px;margin:0 0 12px 0;color:#1a73e8;display:flex;align-items:center;gap:8px;}"
        "h2{font-size:15px;margin:0 0 12px 0;color:#555;border-bottom:1px solid #eee;padding-bottom:6px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin-bottom:12px;}"
        ".stat-box{background:#f8f9fa;border-radius:8px;padding:12px;text-align:center;}"
        ".stat-label{font-size:12px;color:#777;margin-bottom:4px;}"
        ".stat-val{font-size:18px;font-weight:bold;color:#1a73e8;}"
        ".btn{background:#1a73e8;color:#fff;border:none;padding:12px 20px;border-radius:8px;font-size:15px;cursor:pointer;width:100%;font-weight:bold;transition:0.2s;}"
        ".btn:hover{background:#1557b0;}"
        "#cast-msg{margin-top:8px;font-size:13px;color:#28a745;display:none;text-align:center;}"
        "table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px;}"
        "th,td{padding:8px;text-align:left;border-bottom:1px solid #eee;}"
        "th{background:#f8f9fa;color:#666;font-weight:600;}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:11px;font-weight:bold;}"
        ".badge-ok{background:#e6f4ea;color:#137333;}"
        ".badge-err{background:#fce8e6;color:#c5221f;}"
        ".empty-row{text-align:center;color:#999;padding:16px;}"
        ".audio-item{margin:8px 0;padding:10px;background:#f8f9fa;border-radius:8px;}"
        ".audio-item label{font-weight:bold;display:block;margin-bottom:4px;font-size:13px;}"
        "audio{width:100%;height:32px;}"
        "</style></head><body>"
        "<div class=\"container\">"
        "<div class=\"card\">"
        "<h1>🏠 カーテン開閉予測システム</h1>"
        "<p style=\"font-size:13px;color:#666;margin:0 0 16px 0;\">Google Home (192.168.11.7) 連携中</p>"
        "<h2>現在ステータス</h2>"
        "<div class=\"grid\">"
        "<div class=\"stat-box\"><div class=\"stat-label\">カーテン状態</div><div class=\"stat-val\" id=\"curtain-state\">--</div></div>"
        "<div class=\"stat-box\"><div class=\"stat-label\">外気照度</div><div class=\"stat-val\" id=\"lux-val\">-- lx</div></div>"
        "<div class=\"stat-box\"><div class=\"stat-label\">外気温</div><div class=\"stat-val\" id=\"out-temp-val\">-- ℃</div></div>"
        "<div class=\"stat-box\"><div class=\"stat-label\">室内温度</div><div class=\"stat-val\" id=\"in-temp-val\">-- ℃</div></div>"
        "</div>"
        "</div>"
        "<div class=\"card\">"
        "<h2>Google Home 動作確認テスト</h2>"
        "<button class=\"btn\" onclick=\"testCast()\">Google Home でテスト音声を再生</button>"
        "<div id=\"cast-msg\">リクエスト送信済み（再生開始までお待ちください）</div>"
        "</div>"
        "<div class=\"card\">"
        "<h2>📢 アナウンス履歴（最新20件）</h2>"
        "<table><thead><tr><th>日時</th><th>内容</th><th>センサー状態</th><th>結果</th></tr></thead>"
        "<tbody id=\"history-body\"><tr><td colspan=\"4\" class=\"empty-row\">履歴を取得中...</td></tr></tbody>"
        "</table>"
        "</div>"
        "<div class=\"card\">"
        "<h2>内蔵音声ファイル一覧（ブラウザ試聴）</h2>"
        "<div class=\"audio-item\"><label>テスト音声 (test.mp3)</label><audio controls preload=\"none\" src=\"/test.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>冬・採光（開）(win_open.mp3)</label><audio controls preload=\"none\" src=\"/win_open.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>冬・断熱（閉）(win_close.mp3)</label><audio controls preload=\"none\" src=\"/win_close.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>夏・遮熱（閉）(sum_close.mp3)</label><audio controls preload=\"none\" src=\"/sum_close.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>夏・排熱（開）(sum_open.mp3)</label><audio controls preload=\"none\" src=\"/sum_open.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>冬春・日中換気（窓開け）(mid_vent.mp3)</label><audio controls preload=\"none\" src=\"/mid_vent.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>夏秋・夕涼み（窓開け）(night_purge.mp3)</label><audio controls preload=\"none\" src=\"/night_purge.mp3\"></audio></div>"
        "<div class=\"audio-item\"><label>夏・朝の外気逆転（窓閉め）(morning_close.mp3)</label><audio controls preload=\"none\" src=\"/morning_close.mp3\"></audio></div>"
        "</div>"
        "</div>"
        "<script>"
        "function updateStatus(){"
        "  fetch('/api/status').then(r=>r.json()).then(d=>{"
        "    document.getElementById('curtain-state').innerText = d.curtain_state || '--';"
        "    document.getElementById('lux-val').innerText = d.has_sample ? d.lux.toFixed(1)+' lx' : '--';"
        "    document.getElementById('out-temp-val').innerText = d.has_sample ? d.outdoor_temp.toFixed(1)+' ℃' : '--';"
        "    document.getElementById('in-temp-val').innerText = d.has_sample ? d.indoor_temp.toFixed(1)+' ℃' : '--';"
        "  }).catch(e=>console.error(e));"
        "}"
        "function updateHistory(){"
        "  fetch('/api/history').then(r=>r.json()).then(data=>{"
        "    const tbody = document.getElementById('history-body');"
        "    if(!data || data.length===0){"
        "      tbody.innerHTML='<tr><td colspan=\"4\" class=\"empty-row\">まだアナウンス履歴はありません</td></tr>';"
        "      return;"
        "    }"
        "    let h='';"
        "    data.forEach(item=>{"
        "      const badge = item.success ? '<span class=\"badge badge-ok\">成功</span>' : '<span class=\"badge badge-err\">失敗</span>';"
        "      h+='<tr><td>'+item.time+'</td><td><b>'+item.action+'</b></td><td>'+item.detail+'</td><td>'+badge+'</td></tr>';"
        "    });"
        "    tbody.innerHTML=h;"
        "  }).catch(e=>console.error(e));"
        "}"
        "function testCast(){"
        "  const msg = document.getElementById('cast-msg');"
        "  msg.style.display = 'block';"
        "  fetch('/api/test_cast').then(r=>r.json()).then(()=>{"
        "    setTimeout(()=>{ msg.style.display='none'; updateHistory(); }, 4000);"
        "  });"
        "}"
        "updateStatus();"
        "updateHistory();"
        "setInterval(()=>{ updateStatus(); updateHistory(); }, 10000);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t audio_get_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // Route /api/test_cast
    if (strcmp(uri, "/api/test_cast") == 0) {
        return test_cast_handler(req);
    }

    // Route /api/status
    if (strcmp(uri, "/api/status") == 0) {
        return status_api_handler(req);
    }

    // Route /api/history
    if (strcmp(uri, "/api/history") == 0) {
        return history_api_handler(req);
    }

    // Route / or /index.html
    if (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0) {
        return root_get_handler(req);
    }

    // Ignore favicon requests
    if (strcmp(uri, "/favicon.ico") == 0) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }

    char filepath[600];
    if (strncmp(uri, "/spiffs/", 8) == 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, uri + 8);
    } else if (strncmp(uri, "/voice/", 7) == 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, uri + 7);
    } else if (uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "%s%s", SPIFFS_BASE_PATH, uri);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, uri);
    }

    ESP_LOGI(TAG, "Requesting audio file: %s (method=%d)", filepath, req->method);

    FILE *fd = fopen(filepath, "rb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    struct stat st;
    stat(filepath, &st);
    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%ld", st.st_size);

    httpd_resp_set_type(req, "audio/mpeg");
    httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");
    httpd_resp_set_hdr(req, "Content-Length", len_str);

    if (req->method == HTTP_HEAD) {
        fclose(fd);
        return httpd_resp_send(req, NULL, 0);
    }

    char chunk[SCRATCH_BUFSIZE];
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_get = {
    .uri       = "/*",
    .method    = HTTP_GET,
    .handler   = audio_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_head = {
    .uri       = "/*",
    .method    = HTTP_HEAD,
    .handler   = audio_get_handler,
    .user_ctx  = NULL
};

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 12288;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &uri_get);
        httpd_register_uri_handler(s_server, &uri_head);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
