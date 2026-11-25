#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t ntp_client_init(void);
bool ntp_client_is_time_set(void);
int ntp_client_get_current_hour(void);
time_t ntp_client_get_now(void);
