#include "ui_relay.h"
#include "relay.h"
#include "esp_log.h"

static const char *TAG = "ui_relay";

static lv_obj_t *s_btn   = NULL;
static lv_obj_t *s_label = NULL;

static void relay_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    bool on = lv_obj_has_state(s_btn, LV_STATE_CHECKED);
    relay_set(on);
    lv_label_set_text(s_label, on ? "Relais AAN" : "Relais UIT");
    ESP_LOGI(TAG, "Relais handmatig %s", on ? "AAN" : "UIT");
}

void ui_relay_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 16, 0);

    s_btn = lv_button_create(parent);
    lv_obj_set_size(s_btn, 180, 80);
    lv_obj_add_flag(s_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn, relay_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(0x00aa44), LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_btn, 14, LV_STATE_DEFAULT);

    s_label = lv_label_create(s_btn);
    lv_label_set_text(s_label, "Relais UIT");
    lv_obj_set_style_text_color(s_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(s_label);
}

void ui_relay_update(bool on)
{
    if (!s_btn || !s_label) return;

    if (on) {
        lv_obj_add_state(s_btn, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_btn, LV_STATE_CHECKED);
    }
    lv_label_set_text(s_label, on ? "Relais AAN" : "Relais UIT");
}
