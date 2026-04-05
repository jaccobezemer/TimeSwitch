#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <lvgl.h>

// Wordt door process_coordinates gevuld in raw-modus
extern volatile bool    g_touch_cal_raw_mode;
extern volatile int32_t g_touch_cal_raw_x;
extern volatile int32_t g_touch_cal_raw_y;
extern volatile bool    g_touch_cal_raw_ready;

typedef struct {
    // Twee kalibratiepunten per as:
    // Op schermpos (cal_x0, *) zat raw_x0, op (cal_x1, *) zat raw_x1, etc.
    int32_t cal_x0, cal_x1;   // scherm-X coordinaten van de kalibratiepunten
    int32_t cal_y0, cal_y1;   // scherm-Y coordinaten van de kalibratiepunten
    int32_t raw_x0, raw_x1;   // gemeten raw X waarden op die schermposities
    int32_t raw_y0, raw_y1;   // gemeten raw Y waarden op die schermposities
    bool valid;
} touch_cal_data_t;

esp_err_t       touch_cal_load(void);
esp_err_t       touch_cal_save(const touch_cal_data_t *cal);
esp_err_t       touch_cal_erase(void);
bool            touch_cal_is_valid(void);
const touch_cal_data_t *touch_cal_get(void);

// Zet scherm-coordinaat om via kalibratie (gebruikt door process_coordinates)
int32_t touch_cal_apply_x(int32_t raw_x);
int32_t touch_cal_apply_y(int32_t raw_y);

// Start de kalibratie-UI (blokkeert totdat kalibratie klaar is)
void    touch_cal_run(lv_display_t *disp);
