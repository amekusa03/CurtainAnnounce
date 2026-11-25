#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SPIFFS_BASE_PATH "/spiffs"

esp_err_t spiffs_storage_init(void);
bool spiffs_storage_file_exists(const char *path);

