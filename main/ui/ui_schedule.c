#include "ui_schedule.h"
#include "settings.h"
#include "esp_log.h"

static const char *TAG = "ui_schedule";

static lv_obj_t *s_switch     = NULL;
static lv_obj_t *s_on_hour    = NULL;
static lv_obj_t *s_on_min     = NULL;
static lv_obj_t *s_off_hour   = NULL;
static lv_obj_t *s_off_min    = NULL;
static lv_obj_t *s_save_label = NULL;

/* ── Spinbox helpers ─────────────────────────────────────────────────── */
static void spinbox_inc_cb(lv_event_t *e)
{
    lv_obj_t *sb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_spinbox_increment(sb);
}

static void spinbox_dec_cb(lv_event_t *e)
{
    lv_obj_t *sb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_spinbox_decrement(sb);
}

/* Creates [▲ spinbox ▼] in a row container */
static lv_obj_t *create_spinbox(lv_obj_t *parent, int32_t max_val, int32_t init_val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 4, 0);

    lv_obj_t *btn_dec = lv_button_create(row);
    lv_obj_set_size(btn_dec, 36, 36);
    lv_obj_set_style_radius(btn_dec, 6, 0);
    lv_obj_set_style_bg_color(btn_dec, lv_color_hex(0x333355), 0);
    lv_obj_t *lbl_dec = lv_label_create(btn_dec);
    lv_label_set_text(lbl_dec, LV_SYMBOL_DOWN);
    lv_obj_center(lbl_dec);

    lv_obj_t *sb = lv_spinbox_create(row);
    lv_spinbox_set_range(sb, 0, max_val);
    lv_spinbox_set_digit_count(sb, 2);
    lv_spinbox_set_value(sb, init_val);
    lv_obj_set_size(sb, 60, 36);
    lv_obj_set_style_text_font(sb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(sb, lv_color_hex(0x222233), 0);
    lv_obj_set_style_border_color(sb, lv_color_hex(0x444466), 0);
    lv_obj_set_style_text_color(sb, lv_color_white(), 0);

    lv_obj_t *btn_inc = lv_button_create(row);
    lv_obj_set_size(btn_inc, 36, 36);
    lv_obj_set_style_radius(btn_inc, 6, 0);
    lv_obj_set_style_bg_color(btn_inc, lv_color_hex(0x333355), 0);
    lv_obj_t *lbl_inc = lv_label_create(btn_inc);
    lv_label_set_text(lbl_inc, LV_SYMBOL_UP);
    lv_obj_center(lbl_inc);

    lv_obj_add_event_cb(btn_dec, spinbox_dec_cb, LV_EVENT_CLICKED, sb);
    lv_obj_add_event_cb(btn_inc, spinbox_inc_cb, LV_EVENT_CLICKED, sb);

    /* return the spinbox itself so the caller can read its value */
    lv_obj_set_user_data(row, sb);
    return sb;
}

/* Creates a label + time picker row (hh:mm) */
static void create_time_row(lv_obj_t *parent, const char *label_txt,
                             int32_t init_h, int32_t init_m,
                             lv_obj_t **out_h, lv_obj_t **out_m)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_width(lbl, 40);

    *out_h = create_spinbox(row, 23, init_h);

    lv_obj_t *colon = lv_label_create(row);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(colon, lv_color_white(), 0);

    *out_m = create_spinbox(row, 59, init_m);
}

/* ── Save button callback ─────────────────────────────────────────────── */
static void save_cb(lv_event_t *e)
{
    bool    enabled = lv_obj_has_state(s_switch, LV_STATE_CHECKED);
    uint8_t on_h    = (uint8_t)lv_spinbox_get_value(s_on_hour);
    uint8_t on_m    = (uint8_t)lv_spinbox_get_value(s_on_min);
    uint8_t off_h   = (uint8_t)lv_spinbox_get_value(s_off_hour);
    uint8_t off_m   = (uint8_t)lv_spinbox_get_value(s_off_min);

    settings_set_relay_schedule(enabled, on_h, on_m, off_h, off_m);

    lv_label_set_text(s_save_label, LV_SYMBOL_OK " Opgeslagen!");
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_save_label), lv_color_hex(0x00aa44), 0);

    ESP_LOGI(TAG, "Schema opgeslagen: %s %02d:%02d-%02d:%02d",
             enabled ? "aan" : "uit", on_h, on_m, off_h, off_m);
}

/* ── Public API ─────────────────────────────────────────────────────── */
void ui_schedule_create(lv_obj_t *parent)
{
    const settings_t *cfg = settings_get();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 10, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);

    /* Enable toggle row */
    lv_obj_t *enable_row = lv_obj_create(parent);
    lv_obj_set_size(enable_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enable_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(enable_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(enable_row, 0, 0);
    lv_obj_set_style_pad_all(enable_row, 4, 0);

    lv_obj_t *enable_lbl = lv_label_create(enable_row);
    lv_label_set_text(enable_lbl, "Schema inschakelen");
    lv_obj_set_style_text_font(enable_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(enable_lbl, lv_color_hex(0xcccccc), 0);

    s_switch = lv_switch_create(enable_row);
    if (cfg->relay_schedule_enabled) lv_obj_add_state(s_switch, LV_STATE_CHECKED);

    /* Time rows */
    create_time_row(parent, "AAN:", cfg->relay_on_hour,  cfg->relay_on_minute,  &s_on_hour,  &s_on_min);
    create_time_row(parent, "UIT:", cfg->relay_off_hour, cfg->relay_off_minute, &s_off_hour, &s_off_min);

    /* Save button */
    lv_obj_t *save_btn = lv_button_create(parent);
    lv_obj_set_size(save_btn, 160, 44);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x2255aa), 0);
    lv_obj_set_style_radius(save_btn, 10, 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);

    s_save_label = lv_label_create(save_btn);
    lv_label_set_text(s_save_label, "Opslaan");
    lv_obj_set_style_text_font(s_save_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_save_label, lv_color_white(), 0);
    lv_obj_center(s_save_label);
}
