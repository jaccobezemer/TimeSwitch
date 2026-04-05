#include "touch_cal.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_cal";
static const char *NVS_NS  = "touch_cal";

// Gedeeld met process_coordinates in touch.c
volatile bool    g_touch_cal_raw_mode  = false;
volatile int32_t g_touch_cal_raw_x     = 0;
volatile int32_t g_touch_cal_raw_y     = 0;
volatile bool    g_touch_cal_raw_ready = false;

static touch_cal_data_t s_cal = { .valid = false };

/* ── NVS opslaan / laden ─────────────────────────────────────────────── */

esp_err_t touch_cal_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Geen kalibratie in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    touch_cal_data_t tmp;
    size_t len = sizeof(tmp);
    esp_err_t r = nvs_get_blob(h, "cal", &tmp, &len);
    nvs_close(h);

    if (r == ESP_OK && tmp.valid) {
        s_cal = tmp;
        ESP_LOGI(TAG, "Kalibratie geladen: X(%d->%d raw %d->%d) Y(%d->%d raw %d->%d)",
                 (int)s_cal.cal_x0, (int)s_cal.cal_x1,
                 (int)s_cal.raw_x0, (int)s_cal.raw_x1,
                 (int)s_cal.cal_y0, (int)s_cal.cal_y1,
                 (int)s_cal.raw_y0, (int)s_cal.raw_y1);
    }

    ESP_LOGI(TAG, "raw_x0=%d raw_x1=%d cal_x0=%d cal_x1=%d",
        (int)s_cal.raw_x0, (int)s_cal.raw_x1,
        (int)s_cal.cal_x0, (int)s_cal.cal_x1);
    ESP_LOGI(TAG, "raw_y0=%d raw_y1=%d cal_y0=%d cal_y1=%d",
        (int)s_cal.raw_y0, (int)s_cal.raw_y1,
        (int)s_cal.cal_y0, (int)s_cal.cal_y1);

    return r;
}

esp_err_t touch_cal_save(const touch_cal_data_t *cal)
{
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) return r;
    r = nvs_set_blob(h, "cal", cal, sizeof(*cal));
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r == ESP_OK) s_cal = *cal;
    return r;
}

esp_err_t touch_cal_erase(void)
{
    s_cal.valid = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

bool touch_cal_is_valid(void)   { return s_cal.valid; }
const touch_cal_data_t *touch_cal_get(void) { return &s_cal; }

/* ── Coordinaat-omrekening ───────────────────────────────────────────── */

int32_t touch_cal_apply_x(int32_t raw)
{
    if (!s_cal.valid) return raw;
    int32_t dx_raw = s_cal.raw_x1 - s_cal.raw_x0;
    if (dx_raw == 0) return raw;
    int32_t x_max = lv_display_get_horizontal_resolution(lv_display_get_default()) - 1;
    int32_t x = s_cal.cal_x0 + (raw - s_cal.raw_x0) * (s_cal.cal_x1 - s_cal.cal_x0) / dx_raw;
    if (x < 0) x = 0;
    if (x > x_max) x = x_max;
    return x;
}

int32_t touch_cal_apply_y(int32_t raw)
{
    if (!s_cal.valid) return raw;
    int32_t dy_raw = s_cal.raw_y1 - s_cal.raw_y0;
    if (dy_raw == 0) return raw;
    int32_t y_max = lv_display_get_vertical_resolution(lv_display_get_default()) - 1;
    int32_t y = s_cal.cal_y0 + (raw - s_cal.raw_y0) * (s_cal.cal_y1 - s_cal.cal_y0) / dy_raw;
    if (y < 0) y = 0;
    if (y > y_max) y = y_max;
    return y;
}

/* ── Raw touch uitlezen (wacht op 1 tap + loslaten) ─────────────────── */

static void wait_for_raw_tap(int32_t *out_x, int32_t *out_y)
{
    // Wacht totdat er geen touch meer is (lege toestand)
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!g_touch_cal_raw_ready) break;
        g_touch_cal_raw_ready = false;
    }

    // Wacht op touch
    while (!g_touch_cal_raw_ready) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Debounce: lees gemiddelde over 5 samples terwijl vinger op scherm
    int64_t sum_x = 0, sum_y = 0;
    int count = 0;
    while (g_touch_cal_raw_ready) {
        sum_x += g_touch_cal_raw_x;
        sum_y += g_touch_cal_raw_y;
        count++;
        g_touch_cal_raw_ready = false;
        vTaskDelay(pdMS_TO_TICKS(30));
        if (count >= 10) break;
    }

    *out_x = (count > 0) ? (int32_t)(sum_x / count) : g_touch_cal_raw_x;
    *out_y = (count > 0) ? (int32_t)(sum_y / count) : g_touch_cal_raw_y;

    ESP_LOGI(TAG, "Raw tap: x=%d y=%d (n=%d)", (int)*out_x, (int)*out_y, count);

    // Wacht tot vinger weg is
    while (g_touch_cal_raw_ready) {
        g_touch_cal_raw_ready = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(300));
}

/* ── Kalibratie UI ───────────────────────────────────────────────────── */

static const struct {
    int32_t x, y;
    const char *label;
} CAL_POINTS[] = {
    { 30,  30,  "1/4" },
    { 290, 30,  "2/4" },
    { 290, 210, "3/4" },
    { 30,  210, "4/4" },
};
#define NUM_CAL_POINTS 4

static lv_obj_t *s_cross   = NULL;
static lv_obj_t *s_instr   = NULL;
static lv_obj_t *s_counter = NULL;

static void draw_target(int32_t sx, int32_t sy, const char *cnt_txt)
{
    lvgl_port_lock(0);

    lv_obj_set_pos(s_cross, sx - 30, sy - 30);

    lv_label_set_text(s_counter, cnt_txt);
    lv_obj_align(s_counter, LV_ALIGN_BOTTOM_MID, 0, -10);

    lvgl_port_unlock();
}

void touch_cal_run(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Kalibratie gestart");

    // Schakel kalibratie raw-modus in (process_coordinates geeft ruwe waarden door)
    g_touch_cal_raw_mode  = true;
    g_touch_cal_raw_ready = false;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    // Instructietekst
    s_instr = lv_label_create(scr);
    lv_label_set_text(s_instr, "Tik op het kruis");
    lv_obj_set_style_text_font(s_instr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_instr, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_instr, LV_ALIGN_TOP_MID, 0, 6);

    // Kruisdraad (60x60 container met twee lijnen)
    s_cross = lv_obj_create(scr);
    lv_obj_set_size(s_cross, 60, 60);
    lv_obj_set_style_bg_opa(s_cross, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cross, 0, 0);

    // Horizontale lijn
    lv_obj_t *hline = lv_obj_create(s_cross);
    lv_obj_set_size(hline, 60, 2);
    lv_obj_center(hline);
    lv_obj_set_style_bg_color(hline, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_border_width(hline, 0, 0);
    lv_obj_set_style_radius(hline, 0, 0);
    lv_obj_clear_flag(hline, LV_OBJ_FLAG_CLICKABLE);

    // Verticale lijn
    lv_obj_t *vline = lv_obj_create(s_cross);
    lv_obj_set_size(vline, 2, 60);
    lv_obj_center(vline);
    lv_obj_set_style_bg_color(vline, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_border_width(vline, 0, 0);
    lv_obj_set_style_radius(vline, 0, 0);
    lv_obj_clear_flag(vline, LV_OBJ_FLAG_CLICKABLE);

    // Middenpunt stip
    lv_obj_t *dot = lv_obj_create(s_cross);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_center(dot);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    // Teller label
    s_counter = lv_label_create(scr);
    lv_obj_set_style_text_font(s_counter, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_counter, lv_color_hex(0xaaaaaa), 0);

    lvgl_port_unlock();

    // ── Kalibratiepunten meten ──
    int32_t raw_x[NUM_CAL_POINTS];
    int32_t raw_y[NUM_CAL_POINTS];

    for (int i = 0; i < NUM_CAL_POINTS; i++) {
        draw_target(CAL_POINTS[i].x, CAL_POINTS[i].y, CAL_POINTS[i].label);
        vTaskDelay(pdMS_TO_TICKS(400));  // wacht tot UI getekend is
        wait_for_raw_tap(&raw_x[i], &raw_y[i]);
    }

    // ── Bereken kalibratie uit 4 punten ──
    // Gebruik gemiddelden voor betere nauwkeurigheid:
    // raw_x bij linkerrand  = gemiddelde punt 0 en 3 (x=30)
    // raw_x bij rechterrand = gemiddelde punt 1 en 2 (x=290)
    // raw_y bij bovenrand   = gemiddelde punt 0 en 1 (y=30)
    // raw_y bij onderrand   = gemiddelde punt 2 en 3 (y=210)

    touch_cal_data_t cal = {
        .cal_x0 = CAL_POINTS[0].x,
        .cal_x1 = CAL_POINTS[1].x,
        .cal_y0 = CAL_POINTS[0].y,
        .cal_y1 = CAL_POINTS[2].y,
        .raw_x0 = (raw_x[0] + raw_x[3]) / 2,
        .raw_x1 = (raw_x[1] + raw_x[2]) / 2,
        .raw_y0 = (raw_y[0] + raw_y[1]) / 2,
        .raw_y1 = (raw_y[2] + raw_y[3]) / 2,
        .valid  = true,
    };

    // ESP_LOGI(TAG, "Kalibratie berekend: X raw %d->%d = scherm %d->%d",
    //          (int)cal.raw_x0, (int)cal.raw_x1, (int)cal.cal_x0, (int)cal.cal_x1);
    // ESP_LOGI(TAG, "                     Y raw %d->%d = scherm %d->%d",
    //          (int)cal.raw_y0, (int)cal.raw_y1, (int)cal.cal_y0, (int)cal.cal_y1);

    ESP_LOGI(TAG, "PUNT 0 (linksboven  scherm 30,30):   raw x=%d y=%d", (int)raw_x[0], (int)raw_y[0]);
    ESP_LOGI(TAG, "PUNT 1 (rechtsboven scherm 290,30):  raw x=%d y=%d", (int)raw_x[1], (int)raw_y[1]);
    ESP_LOGI(TAG, "PUNT 2 (rechtsonder scherm 290,210): raw x=%d y=%d", (int)raw_x[2], (int)raw_y[2]);
    ESP_LOGI(TAG, "PUNT 3 (linksonder  scherm 30,210):  raw x=%d y=%d", (int)raw_x[3], (int)raw_y[3]);

    touch_cal_save(&cal);

    // Kalibratie raw-modus uit
    g_touch_cal_raw_mode = false;

    // Bevestigingsscherm
    lvgl_port_lock(0);
    lv_obj_clean(scr);
    lv_obj_t *done = lv_label_create(scr);
    lv_label_set_text(done, LV_SYMBOL_OK "  Kalibratie opgeslagen!");
    lv_obj_set_style_text_font(done, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(done, lv_color_hex(0x00cc44), 0);
    lv_obj_center(done);
    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "Kalibratie klaar");
}
