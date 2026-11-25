#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t cast_client_init(void);
esp_err_t cast_client_play_url(const char *google_home_ip, uint16_t port, const char *media_url);
