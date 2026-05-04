#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hardware.h"

#define RELAY_NAME_MAX_LEN 32

typedef struct {
    bool    enabled;
    uint8_t on_hour,  on_min;
    uint8_t off_hour, off_min;
} day_schedule_t;

typedef struct {
    day_schedule_t days[7]; // 0=maandag … 6=zondag
} relay_schedule_t;

typedef struct {
    char             wifi_ssid[33];
    char             wifi_password[65];
    relay_schedule_t schedules[NUM_RELAYS];
    char             relay_names[NUM_RELAYS][RELAY_NAME_MAX_LEN];
    uint8_t          relay_type[NUM_RELAYS];     // RELAY_TYPE_NORMAL of RELAY_TYPE_IMPULSE
    uint16_t         relay_pulse_ms[NUM_RELAYS]; // Puls-duur in ms (alleen voor IMPULSE)
} settings_t;

esp_err_t           settings_init(void);
const settings_t*   settings_get(void);
esp_err_t           settings_set_wifi(const char *ssid, const char *password);
esp_err_t           settings_set_schedule_for_relay(uint8_t relay_index, const relay_schedule_t *schedule);
esp_err_t           settings_set_relay_name(uint8_t relay_index, const char* name);
esp_err_t           settings_set_relay_config(uint8_t relay_index, uint8_t type, uint16_t pulse_ms);
esp_err_t           settings_reset(void);
