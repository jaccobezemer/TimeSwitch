#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include <lvgl.h>

esp_err_t ui_main_init(lv_display_t *disp);
void      ui_main_update_relay(bool on);
