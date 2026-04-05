#include "ui_main.h"
#include "ui_relay.h"
#include "ui_schedule.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "touch_cal.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui_main";

static lv_obj_t *s_status_time = NULL;
static lv_obj_t *s_status_wifi = NULL;
static lv_display_t *s_disp    = NULL;

/* ── Herkalibratietaak (apart task zodat LVGL niet blokkeert) ────────── */
static void recal_task(void *arg)
{
    touch_cal_erase();
    touch_cal_run(s_disp);

    // Na kalibratie: scherm opnieuw opbouwen
    if (lvgl_port_lock(0)) {
        lv_obj_clean(lv_scr_act());
        lvgl_port_unlock();
    }
    // Herstart app zodat normale UI opnieuw inits
    esp_restart();
}

static void recal_btn_cb(lv_event_t *e)
{
    xTaskCreate(recal_task, "recal", 4096, NULL, 5, NULL);
}

/* ── Statusbalk refresh (elke seconde) ──────────────────────────────── */
static void status_timer_cb(lv_timer_t *timer)
{
    struct tm t;
    if (time_sync_get_localtime(&t)) {
        lv_label_set_text_fmt(s_status_time, "%02d:%02d:%02d",
                              t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        lv_label_set_text(s_status_time, "--:--:--");
    }

    if (wifi_manager_is_connected()) {
        const char *ip = wifi_manager_get_ip_str();
        lv_label_set_text_fmt(s_status_wifi, LV_SYMBOL_WIFI " %s", ip ? ip : "OK");
        lv_obj_set_style_text_color(s_status_wifi, lv_color_hex(0x00cc44), 0);
    } else {
        lv_label_set_text(s_status_wifi, LV_SYMBOL_WIFI " --");
        lv_obj_set_style_text_color(s_status_wifi, lv_color_hex(0x888888), 0);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */
esp_err_t ui_main_init(lv_display_t *disp)
{
    s_disp = disp;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* ── Statusbalk (bovenaan, 24px) ── */
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, LV_PCT(100), 24);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_hor(status_bar, 8, 0);
    lv_obj_set_style_pad_ver(status_bar, 2, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status_time = lv_label_create(status_bar);
    lv_label_set_text(s_status_time, "--:--:--");
    lv_obj_set_style_text_font(s_status_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_time, lv_color_hex(0xffffff), 0);

    s_status_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_status_wifi, LV_SYMBOL_WIFI " --");
    lv_obj_set_style_text_font(s_status_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_wifi, lv_color_hex(0x888888), 0);

    /* ── Tabview (onder statusbalk) ── */
    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, LV_PCT(100), LV_VER_RES - 24);
    lv_obj_align(tv, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14, 0);

    lv_obj_t *tab_relay    = lv_tabview_add_tab(tv, LV_SYMBOL_POWER " Relais");
    lv_obj_t *tab_schedule = lv_tabview_add_tab(tv, LV_SYMBOL_SETTINGS " Schema");
    lv_obj_t *tab_system   = lv_tabview_add_tab(tv, LV_SYMBOL_LIST " Systeem");

    ui_relay_create(tab_relay);
    ui_schedule_create(tab_schedule);

    /* ── Systeemtab: herkalibreer-knop ── */
    lv_obj_set_flex_flow(tab_system, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_system, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tab_system, 12, 0);

    lv_obj_t *recal_btn = lv_button_create(tab_system);
    lv_obj_set_size(recal_btn, 200, 50);
    lv_obj_set_style_bg_color(recal_btn, lv_color_hex(0x884400), 0);
    lv_obj_set_style_radius(recal_btn, 10, 0);
    lv_obj_add_event_cb(recal_btn, recal_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *recal_lbl = lv_label_create(recal_btn);
    lv_label_set_text(recal_lbl, LV_SYMBOL_REFRESH " Kalibreer touch");
    lv_obj_set_style_text_font(recal_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(recal_lbl);

    /* ── Statusbalk timer ── */
    lv_timer_create(status_timer_cb, 1000, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI geïnitialiseerd");
    return ESP_OK;
}

void ui_main_update_relay(bool on)
{
    if (lvgl_port_lock(0)) {
        ui_relay_update(on);
        lvgl_port_unlock();
    }
}
