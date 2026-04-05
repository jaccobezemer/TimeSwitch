#include "ui_main.h"
#include "ui_relay.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "settings.h"
#include "relay.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui_main";

static lv_obj_t *s_status_time = NULL;
static lv_obj_t *s_status_wifi = NULL;
static lv_display_t *s_disp    = NULL;

extern bool relay_override_is_active(void);

static const char *DAY_NL[] = {"Ma","Di","Wo","Do","Vr","Za","Zo"};

/* Zoekt de eerstvolgende schema-overgang (AAN of UIT) na het huidige moment.
 * Kijkt maximaal 7 dagen vooruit.
 * Geeft true terug als er een gevonden is. */
static bool find_next_transition(const struct tm *now,
                                  int *out_dow, int *out_hour, int *out_min,
                                  bool *out_is_on)
{
    const settings_t *cfg = settings_get();
    int cur_dow = (now->tm_wday + 6) % 7;  // 0=ma…6=zo
    int cur_min = now->tm_hour * 60 + now->tm_min;

    for (int d = 0; d < 7; d++) {
        int dow = (cur_dow + d) % 7;
        const day_schedule_t *day = &cfg->days[dow];
        if (!day->enabled) continue;

        int on_min  = day->on_hour  * 60 + day->on_min;
        int off_min = day->off_hour * 60 + day->off_min;

        if (d == 0) {
            // Vandaag: alleen tijden die nog in de toekomst liggen
            // Check of aan-tijd nog moet komen
            if (on_min > cur_min) {
                *out_dow = dow; *out_hour = day->on_hour;  *out_min = day->on_min;  *out_is_on = true;
                return true;
            }
            // Check of we nu actief zijn (aan-tijd voorbij, uit-tijd nog niet)
            if (on_min <= cur_min && off_min > cur_min) {
                *out_dow = dow; *out_hour = day->off_hour; *out_min = day->off_min; *out_is_on = false;
                return true;
            }
        } else {
            // Toekomstige dag: aan-tijd is altijd de eerste overgang
            *out_dow = dow; *out_hour = day->on_hour; *out_min = day->on_min; *out_is_on = true;
            return true;
        }
    }
    return false;
}

#include "touch_cal.h"

static void recal_btn_cb(lv_event_t *e)
{
    touch_cal_erase();
    esp_restart();
}

/* ── Status timer (elke seconde) ─────────────────────────────────────── */
static void status_timer_cb(lv_timer_t *timer)
{
    if (!s_status_time || !s_status_wifi) return;

    struct tm t;
    bool synced = time_sync_get_localtime(&t);

    if (synced) {
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

    // Schema / override status onder de relay knop
    if (!synced) {
        ui_relay_update_status("Tijd niet gesynchroniseerd", false);
        return;
    }

    char status_buf[64];
    int next_dow, next_hour, next_min;
    bool next_is_on;
    int cur_dow = (t.tm_wday + 6) % 7;

    if (relay_override_is_active()) {
        // Zoek eerstvolgende schema-overgang — dat is wanneer de override eindigt
        if (find_next_transition(&t, &next_dow, &next_hour, &next_min, &next_is_on)) {
            if (next_dow == cur_dow) {
                snprintf(status_buf, sizeof(status_buf),
                         "Override tot %02d:%02d", next_hour, next_min);
            } else {
                snprintf(status_buf, sizeof(status_buf),
                         "Override tot %s %02d:%02d", DAY_NL[next_dow], next_hour, next_min);
            }
        } else {
            snprintf(status_buf, sizeof(status_buf), "Override actief");
        }
        ui_relay_update_status(status_buf, true);
    } else {
        // Toon het eerstvolgende relevante schema-event
        if (find_next_transition(&t, &next_dow, &next_hour, &next_min, &next_is_on)) {
            if (!next_is_on) {
                // Schema is nu actief; toon het volledige dagschema
                const settings_t *cfg = settings_get();
                const day_schedule_t *day = &cfg->days[next_dow];
                snprintf(status_buf, sizeof(status_buf),
                         "%s %02d:%02d - %02d:%02d",
                         DAY_NL[next_dow],
                         day->on_hour, day->on_min,
                         day->off_hour, day->off_min);
            } else {
                // Schema is nu niet actief; toon wanneer het de volgende keer aangaat
                if (next_dow == cur_dow) {
                    snprintf(status_buf, sizeof(status_buf),
                             "Volgende: %02d:%02d", next_hour, next_min);
                } else {
                    snprintf(status_buf, sizeof(status_buf),
                             "Volgende: %s %02d:%02d", DAY_NL[next_dow], next_hour, next_min);
                }
            }
        } else {
            snprintf(status_buf, sizeof(status_buf), "Geen schema actief");
        }
        ui_relay_update_status(status_buf, false);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */
esp_err_t ui_main_init(lv_display_t *disp)
{
    s_disp = disp;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // Statusbalk bovenaan (24px)
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

    // Tabview
    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, LV_PCT(100), LV_VER_RES - 24);
    lv_obj_align(tv, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14, 0);

    lv_obj_t *tab_relay  = lv_tabview_add_tab(tv, LV_SYMBOL_POWER " Relais");
    lv_obj_t *tab_system = lv_tabview_add_tab(tv, LV_SYMBOL_LIST " Systeem");

    ui_relay_create(tab_relay);

    // Systeemtab
    lv_obj_set_flex_flow(tab_system, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_system, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tab_system, 12, 0);

    lv_obj_t *ip_lbl = lv_label_create(tab_system);
    lv_label_set_text(ip_lbl, "Schema instellen via browser:\nhttp://<ip-adres>/schedule");
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(ip_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ip_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ip_lbl, LV_PCT(90));

    lv_obj_t *recal_btn = lv_button_create(tab_system);
    lv_obj_set_size(recal_btn, 200, 44);
    lv_obj_set_style_bg_color(recal_btn, lv_color_hex(0x884400), 0);
    lv_obj_set_style_radius(recal_btn, 10, 0);
    lv_obj_add_event_cb(recal_btn, recal_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *recal_lbl = lv_label_create(recal_btn);
    lv_label_set_text(recal_lbl, LV_SYMBOL_REFRESH " Kalibreer touch");
    lv_obj_set_style_text_font(recal_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(recal_lbl);

    lv_timer_create(status_timer_cb, 1000, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI geinitialiseerd");
    return ESP_OK;
}

static void relay_update_async_cb(void *arg)
{
    ui_relay_update((bool)(intptr_t)arg);
}

void ui_main_update_relay(bool on)
{
    lv_async_call(relay_update_async_cb, (void *)(intptr_t)on);
}
