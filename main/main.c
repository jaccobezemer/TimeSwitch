#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>

#include <lvgl.h>
#include <esp_lvgl_port.h>

#include "lcd.h"
#include "touch.h"
#include "touch_cal.h"
#include "relay.h"
#include "settings.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "time_sync.h"
#include "ota_server.h"
#include "ui_main.h"

static const char *TAG = "main";

/* ── Override state ──────────────────────────────────────────────────── */
// Wanneer de gebruiker handmatig het relais bedient, wordt het schema tijdelijk
// genegeerd tot de volgende schema-overgang.
static bool    s_override_active        = false;
static bool    s_override_state         = false;  // gewenste staat tijdens override
static bool    s_override_base_schedule = false;  // schema-staat op moment van activeren
static bool    s_override_base_valid    = false;  // nog niet bepaald na activatie

void relay_override_set(bool on)
{
    s_override_active     = true;
    s_override_state      = on;
    s_override_base_valid = false;  // wordt bij eerste check bepaald
    relay_set(on);
    ui_main_update_relay(on);
    ESP_LOGI(TAG, "Override actief: relais %s", on ? "AAN" : "UIT");
}

bool relay_override_is_active(void)    { return s_override_active; }
bool relay_override_base_state(void)   { return s_override_base_schedule; }

void relay_override_cancel(void)
{
    s_override_active = false;
    ESP_LOGI(TAG, "Override gecanceld door gebruiker");
}

/* ── Schema check ────────────────────────────────────────────────────── */
static void check_relay_schedule(void)
{
    if (!time_sync_is_synced()) return;

    struct tm t;
    time_sync_get_localtime(&t);

    // tm_wday: 0=zondag…6=zaterdag; wij gebruiken 0=maandag…6=zondag
    int dow = (t.tm_wday + 6) % 7;

    const settings_t *cfg = settings_get();
    const day_schedule_t *day = &cfg->days[dow];

    if (!day->enabled) return;

    int now_min = t.tm_hour * 60 + t.tm_min;
    int on_min  = day->on_hour  * 60 + day->on_min;
    int off_min = day->off_hour * 60 + day->off_min;

    bool should_be_on;
    if (on_min < off_min) {
        should_be_on = (now_min >= on_min && now_min < off_min);
    } else {
        should_be_on = (now_min >= on_min || now_min < off_min);
    }

    if (s_override_active) {
        if (!s_override_base_valid) {
            // Eerste check na activatie: onthoud wat het schema nu zegt
            s_override_base_schedule = should_be_on;
            s_override_base_valid    = true;
        } else if (should_be_on != s_override_base_schedule) {
            // Schema heeft een overgang gemaakt — override beëindigen
            ESP_LOGI(TAG, "Schema-overgang: override gecanceld, relais %s", should_be_on ? "AAN" : "UIT");
            s_override_active = false;
            relay_set(should_be_on);
            ui_main_update_relay(should_be_on);
        }
    } else {
        if (should_be_on != relay_get()) {
            relay_set(should_be_on);
            ui_main_update_relay(should_be_on);
        }
    }
}

/* ── WiFi event callback ─────────────────────────────────────────────── */
static void wifi_event_cb(wifi_mgr_event_t event, void *arg)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi verbonden");
            time_sync_init();
            ota_server_start();
            break;
        case WIFI_MGR_EVENT_NO_CREDENTIALS:
            ESP_LOGW(TAG, "Geen credentials — captive portal starten");
            wifi_manager_start_ap();
            captive_portal_start();
            break;
        case WIFI_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi verbinding verbroken");
            break;
        default:
            break;
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
void app_main(void)
{
    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t    lcd_panel;
    esp_lcd_touch_handle_t    tp;
    lvgl_port_touch_cfg_t     touch_cfg = { 0 };
    lv_display_t             *lvgl_display = NULL;

    ESP_ERROR_CHECK(lcd_display_brightness_init());
    ESP_ERROR_CHECK(app_lcd_init(&lcd_io, &lcd_panel));

    lvgl_display = app_lvgl_init(lcd_io, lcd_panel);
    if (lvgl_display == NULL) {
        ESP_LOGE(TAG, "Fatale fout in app_lvgl_init");
        esp_restart();
    }

    ESP_ERROR_CHECK(touch_init(&tp));
    touch_cfg.disp   = lvgl_display;
    touch_cfg.handle = tp;
    lvgl_port_add_touch(&touch_cfg);

    ESP_ERROR_CHECK(relay_init());
    ESP_ERROR_CHECK(lcd_display_brightness_set(50));
    ESP_ERROR_CHECK(lcd_display_rotate(lvgl_display, LV_DISPLAY_ROTATION_0));

    ESP_ERROR_CHECK(settings_init());
    touch_cal_load();
    if (!touch_cal_is_valid()) {
        ESP_LOGW(TAG, "Geen touch kalibratie, kalibratie starten...");
        touch_cal_run(lvgl_display);
    }

    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_cb));
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_ERROR_CHECK(ui_main_init(lvgl_display));

    // Schema elke 5 seconden controleren
    while (true) {
        check_relay_schedule();
        vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    }
}
