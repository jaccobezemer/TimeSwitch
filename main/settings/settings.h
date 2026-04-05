#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool    enabled;
    uint8_t on_hour,  on_min;
    uint8_t off_hour, off_min;
} day_schedule_t;

typedef struct {
    char           wifi_ssid[33];
    char           wifi_password[65];
    day_schedule_t days[7];   // 0=maandag … 6=zondag
} settings_t;

esp_err_t        settings_init(void);
const settings_t *settings_get(void);
esp_err_t        settings_set_wifi(const char *ssid, const char *password);
esp_err_t        settings_set_schedule(const day_schedule_t days[7]);
esp_err_t        settings_reset(void);
