#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "thermal_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize web reporter module.
 */
esp_err_t web_reporter_init(void);

/**
 * @brief Send current climate, curtain and window report to Vercel endpoint via HTTPS POST.
 *
 * @param outdoor_temp Outdoor temperature [deg C]
 * @param indoor_temp Indoor temperature [deg C]
 * @param lux Ambient light level [lx]
 * @param curtain_state Current curtain state enum
 * @param window_state Current window state enum
 * @param last_action Action title string (e.g. "冬・断熱（閉）")
 * @param action_time Formatted string of action timestamp (e.g. "2026-11-26 17:15")
 * @return esp_err_t ESP_OK on success, ESP_FAIL or error code otherwise
 */
esp_err_t web_reporter_send(float outdoor_temp,
                            float indoor_temp,
                            float lux,
                            curtain_state_t curtain_state,
                            window_state_t window_state,
                            const char *last_action,
                            const char *action_time);

#ifdef __cplusplus
}
#endif
