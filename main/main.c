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
#include "ui_main.h"

static const char *TAG = "main";

/* ── Relay schema check ─────────────────────────────────────────────── */
static void check_relay_schedule(void)
{
    const settings_t *cfg = settings_get();
    if (!cfg->relay_schedule_enabled || !time_sync_is_synced()) {
        return;
    }

    struct tm t;
    time_sync_get_localtime(&t);

    int now_min = t.tm_hour * 60 + t.tm_min;
    int on_min  = cfg->relay_on_hour  * 60 + cfg->relay_on_minute;
    int off_min = cfg->relay_off_hour * 60 + cfg->relay_off_minute;

    bool should_be_on;
    if (on_min < off_min) {
        // bijv. 07:00–23:00
        should_be_on = (now_min >= on_min && now_min < off_min);
    } else {
        // over middernacht, bijv. 22:00–06:00
        should_be_on = (now_min >= on_min || now_min < off_min);
    }

    if (should_be_on != relay_get()) {
        relay_set(should_be_on);
        ui_main_update_relay(should_be_on);
    }
}

/* ── WiFi event callback ─────────────────────────────────────────────── */
static void wifi_event_cb(wifi_mgr_event_t event, void *arg)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi verbonden — NTP starten");
            time_sync_init();
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

    // Hardware init
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
    ESP_ERROR_CHECK(lcd_display_brightness_set(75));
    ESP_ERROR_CHECK(lcd_display_rotate(lvgl_display, LV_DISPLAY_ROTATION_90));

    ESP_ERROR_CHECK(settings_init());  // NVS moet eerst geïnitialiseerd zijn    
    // // Touch kalibratie laden; als niet geldig → kalibratie-routine draaien
    // touch_cal_load();
    // if (!touch_cal_is_valid()) {
    //     ESP_LOGW(TAG, "Geen touch kalibratie gevonden, kalibratie starten...");
    //     touch_cal_run(lvgl_display);
    // }

    // Settings + WiFi
    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_cb));
    ESP_ERROR_CHECK(wifi_manager_start());

    // UI
    ESP_ERROR_CHECK(ui_main_init(lvgl_display));

    // Hoofdlus: relay schema elke 30 seconden controleren
    while (true) {
        check_relay_schedule();
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
