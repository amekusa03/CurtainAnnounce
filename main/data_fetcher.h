#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    float outdoor_temp;
    float lux;
    float indoor_temp;
    bool outdoor_valid;
    bool valid;
} weather_data_t;

esp_err_t data_fetcher_init(void);
esp_err_t data_fetcher_poll(weather_data_t *out_data);

