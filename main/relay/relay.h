#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

esp_err_t relay_init(void);
esp_err_t relay_set_state(uint8_t relay_index, bool on);
esp_err_t relay_set_config(uint8_t relay_index, uint8_t type, uint16_t pulse_ms);
bool      relay_get_state(uint8_t relay_index);
uint8_t   relay_get_all_states(void);
