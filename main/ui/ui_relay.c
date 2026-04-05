#include "ui_relay.h"
#include "relay.h"
#include "esp_log.h"

// Extern gedeclareerd in main.c
extern void relay_override_set(bool on);
extern void relay_override_cancel(void);
extern bool relay_override_is_active(void);
extern bool relay_override_base_state(void);

static const char *TAG = "ui_relay";

static lv_obj_t *s_btn_on      = NULL;
static lv_obj_t *s_btn_off     = NULL;
static lv_obj_t *s_status_label = NULL;

static void apply_relay(bool on)
{
    if (relay_override_is_active()) {
        if (on == relay_override_base_state()) {
            // Terug naar schema-staat: override opheffen
            relay_set(on);
            relay_override_cancel();
            ui_relay_update(on);
            ESP_LOGI(TAG, "Relais terug naar schema-staat, override opgeheven");
        } else if (on != relay_get()) {
            // Override staat al aan maar naar andere staat wisselen
            relay_override_set(on);
            ESP_LOGI(TAG, "Relais handmatig %s (override)", on ? "AAN" : "UIT");
        }
        // on == relay_get(): al in gewenste staat, niets doen
    } else {
        if (on == relay_get()) return;  // al conform schema, niets doen
        relay_override_set(on);
        ESP_LOGI(TAG, "Relais handmatig %s (override)", on ? "AAN" : "UIT");
    }
}

static void btn_on_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    apply_relay(true);
}

static void btn_off_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    apply_relay(false);
}

void ui_relay_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 16, 0);

    // AAN knop (groen)
    s_btn_on = lv_button_create(parent);
    lv_obj_set_size(s_btn_on, 160, 70);
    lv_obj_set_style_radius(s_btn_on, 12, 0);
    lv_obj_set_style_bg_color(s_btn_on, lv_color_hex(0x00aa44), 0);
    lv_obj_add_event_cb(s_btn_on, btn_on_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_on = lv_label_create(s_btn_on);
    lv_label_set_text(lbl_on, "AAN");
    lv_obj_set_style_text_font(lbl_on, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_on, lv_color_white(), 0);
    lv_obj_center(lbl_on);

    // UIT knop (rood)
    s_btn_off = lv_button_create(parent);
    lv_obj_set_size(s_btn_off, 160, 70);
    lv_obj_set_style_radius(s_btn_off, 12, 0);
    lv_obj_set_style_bg_color(s_btn_off, lv_color_hex(0xcc2222), 0);
    lv_obj_add_event_cb(s_btn_off, btn_off_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_off = lv_label_create(s_btn_off);
    lv_label_set_text(lbl_off, "UIT");
    lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_off, lv_color_white(), 0);
    lv_obj_center(lbl_off);

    // Schema / override statuslabel
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, LV_PCT(90));

    // Initiële staat
    ui_relay_update(relay_get());
}

void ui_relay_update(bool on)
{
    if (!s_btn_on || !s_btn_off) return;

    // Actieve knop: volledig kleur; inactieve knop: gedimd
    lv_obj_set_style_bg_color(s_btn_on,  on ? lv_color_hex(0x00aa44) : lv_color_hex(0x224433), 0);
    lv_obj_set_style_bg_color(s_btn_off, on ? lv_color_hex(0x442222) : lv_color_hex(0xcc2222), 0);
}

void ui_relay_update_status(const char *text, bool is_override)
{
    if (!s_status_label) return;
    lv_label_set_text(s_status_label, text);
    lv_obj_set_style_text_color(s_status_label,
        is_override ? lv_color_hex(0xff8800) : lv_color_hex(0x888888), 0);
}
