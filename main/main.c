#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include <string.h>

#include "hardware.h"
#include "relay.h"
#include "settings.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "time_sync.h"
#include "ota_server.h"

static const char* TAG = "main";

/* ── Override State ──────────────────────────────────────────────────── */
// Logic to temporarily ignore the schedule when a user manually toggles a relay.
// This is now managed per-relay using bitmasks.
static uint8_t s_override_active = 0; // Bitmask: 1 if override is active for the relay
static uint8_t s_override_state = 0; // Bitmask: desired state during override
static uint8_t s_override_base_schedule = 0; // Bitmask: schedule state at the moment of activation
static uint8_t s_override_base_valid = 0; // Bitmask: 1 if base schedule state has been captured

// Forward declaration for websocket update. This function needs to be implemented.
void ws_broadcast_all_relay_states(void);


void relay_override_set(uint8_t relay_index, bool on)
{
    if (relay_index >= NUM_RELAYS) return;

    s_override_active |= (1 << relay_index);
    s_override_base_valid &= ~(1 << relay_index);

    if (on) {
        s_override_state |= (1 << relay_index);
    } else {
        s_override_state &= ~(1 << relay_index);
    }

    relay_set_state(relay_index, on);
    ws_broadcast_all_relay_states();
    ESP_LOGI(TAG, "Override active for relay %d: state %s", relay_index + 1, on ? "ON" : "OFF");
}

bool relay_override_is_active(uint8_t relay_index)
{
    if (relay_index >= NUM_RELAYS) return false;
    return (s_override_active >> relay_index) & 1;
}

void relay_override_cancel(uint8_t relay_index)
{
    if (relay_index >= NUM_RELAYS) return;
    s_override_active &= ~(1 << relay_index);
    ws_broadcast_all_relay_states();
    ESP_LOGI(TAG, "Override for relay %d cancelled by user", relay_index + 1);
}

/* ── Schedule Check ────────────────────────────────────────────────────── */
static void check_relay_schedule(void)
{
    if (!time_sync_is_synced()) return;

    struct tm t;
    time_sync_get_localtime(&t);

    int dow = (t.tm_wday + 6) % 7;
    int now_min = t.tm_hour * 60 + t.tm_min;

    const settings_t* cfg = settings_get();
    bool state_changed = false;

    for (int i = 0; i < NUM_RELAYS; i++) {
        const relay_schedule_t* relay_sched = &cfg->schedules[i];
        const day_schedule_t* day = &relay_sched->days[dow];
        bool is_override = (s_override_active >> i) & 1;

        bool should_be_on = false;
        if (day->enabled) {
            int on_min = day->on_hour * 60 + day->on_min;
            int off_min = day->off_hour * 60 + day->off_min;
            if (on_min < off_min) {
                should_be_on = (now_min >= on_min && now_min < off_min);
            } else {
                should_be_on = (now_min >= on_min || now_min < off_min);
            }
        }

        bool current_state = relay_get_state(i);
        bool new_state = current_state;

        if (is_override) {
            bool base_valid = (s_override_base_valid >> i) & 1;
            if (!base_valid) {
                if (should_be_on) { s_override_base_schedule |= (1 << i); }
                else { s_override_base_schedule &= ~(1 << i); }
                s_override_base_valid |= (1 << i);
            } else {
                bool base_schedule_state = (s_override_base_schedule >> i) & 1;
                if (should_be_on != base_schedule_state) {
                    ESP_LOGI(TAG, "Schedule transition on relay %d: override cancelled.", i + 1);
                    s_override_active &= ~(1 << i);
                    new_state = should_be_on;
                }
            }
        } else {
            new_state = should_be_on;
        }

        if (current_state != new_state) {
            relay_set_state(i, new_state);
            state_changed = true;
        }
    }

    if (state_changed) {
        ws_broadcast_all_relay_states();
    }
}

/* ── WiFi Event Callback ─────────────────────────────────────────────── */
static void wifi_event_cb(wifi_mgr_event_t event, void* arg)
{
    switch (event) {
    case WIFI_MGR_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected");
        time_sync_init();
        ota_server_start();
        break;
    case WIFI_MGR_EVENT_NO_CREDENTIALS:
        ESP_LOGW(TAG, "No credentials - starting captive portal");
        wifi_manager_start_ap();
        captive_portal_start();
        break;
    case WIFI_MGR_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected");
        break;
    default:
        break;
    }
}

/* ── Entry Point ─────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(relay_init());

    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_cb));
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_LOGI(TAG, "Initialization complete. Starting main loop.");

    while (true) {
        check_relay_schedule();
        vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    }
}
