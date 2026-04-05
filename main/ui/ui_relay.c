#include "ui_relay.h"
#include "relay.h"
#include "esp_log.h"

// Extern gedeclareerd in main.c
extern void relay_override_set(bool on);
extern void relay_override_cancel(void);
extern bool relay_override_is_active(void);
extern bool relay_override_base_state(void);

static const char *TAG = "ui_relay";

static lv_obj_t *s_btn          = NULL;
static lv_obj_t *s_relay_label  = NULL;
static lv_obj_t *s_status_label = NULL;

static void relay_toggle_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    bool on = lv_obj_has_state(s_btn, LV_STATE_CHECKED);

    // Als override actief is en de gebruiker zet het relais terug naar de schema-staat:
    // override cancelen zodat het schema weer het overneemt.
    if (relay_override_is_active() && on == relay_override_base_state()) {
        relay_set(on);
        relay_override_cancel();
        // Knopstatus is al correct (LVGL heeft CHECKABLE al getoggeld),
        // alleen het label bijwerken.
        ESP_LOGI(TAG, "Relais terug naar schema-staat, override opgeheven");
    } else {
        relay_override_set(on);  // zet relay + update display intern
        ESP_LOGI(TAG, "Relais handmatig %s (override)", on ? "AAN" : "UIT");
    }
    lv_label_set_text(s_relay_label, on ? "AAN" : "UIT");
}

void ui_relay_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 12, 0);

    // Toggle knop
    s_btn = lv_button_create(parent);
    lv_obj_set_size(s_btn, 140, 60);
    lv_obj_add_flag(s_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn, relay_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(0x00aa44), LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_btn, 12, LV_STATE_DEFAULT);

    s_relay_label = lv_label_create(s_btn);
    lv_label_set_text(s_relay_label, "UIT");
    lv_obj_set_style_text_color(s_relay_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_relay_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(s_relay_label);

    // Schema / override statuslabel
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xaaaaaa), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, LV_PCT(90));
}

void ui_relay_update(bool on)
{
    if (!s_btn || !s_relay_label) return;

    if (on) {
        lv_obj_add_state(s_btn, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_btn, LV_STATE_CHECKED);
    }
    lv_label_set_text(s_relay_label, on ? "AAN" : "UIT");
}

void ui_relay_update_status(const char *text, bool is_override)
{
    if (!s_status_label) return;
    lv_label_set_text(s_status_label, text);
    lv_obj_set_style_text_color(s_status_label,
        is_override ? lv_color_hex(0xff8800) : lv_color_hex(0x888888), 0);
}
