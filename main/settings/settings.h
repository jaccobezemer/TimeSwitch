#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char    wifi_ssid[33];
    char    wifi_password[65];
    bool    relay_schedule_enabled;
    uint8_t relay_on_hour;
    uint8_t relay_on_minute;
    uint8_t relay_off_hour;
    uint8_t relay_off_minute;
} settings_t;

esp_err_t        settings_init(void);
const settings_t *settings_get(void);
esp_err_t        settings_set_wifi(const char *ssid, const char *password);
esp_err_t        settings_set_relay_schedule(bool enabled,
                                              uint8_t on_h,  uint8_t on_m,
                                              uint8_t off_h, uint8_t off_m);
esp_err_t        settings_reset(void);
