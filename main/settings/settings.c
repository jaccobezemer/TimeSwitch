#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "settings";
static const char *NVS_NS = "timeswitch";

static settings_t s_settings;
static SemaphoreHandle_t s_mutex;
static bool s_initialized = false;

static const uint8_t  DEFAULT_TYPES[]    = { RELAY_1_TYPE, RELAY_2_TYPE, RELAY_3_TYPE,
                                              RELAY_4_TYPE, RELAY_5_TYPE, RELAY_6_TYPE,
                                              RELAY_7_TYPE, RELAY_8_TYPE };
static const uint16_t DEFAULT_PULSE_MS[] = { RELAY_PULSE_MS, RELAY_PULSE_MS, RELAY_PULSE_MS,
                                              RELAY_PULSE_MS, RELAY_PULSE_MS, RELAY_PULSE_MS,
                                              RELAY_PULSE_MS, RELAY_PULSE_MS };
_Static_assert(sizeof(DEFAULT_TYPES)    / sizeof(DEFAULT_TYPES[0])    >= NUM_RELAYS, "Voeg RELAY_N_TYPE toe in hardware.h en DEFAULT_TYPES");
_Static_assert(sizeof(DEFAULT_PULSE_MS) / sizeof(DEFAULT_PULSE_MS[0]) >= NUM_RELAYS, "Voeg entry toe in DEFAULT_PULSE_MS");

static void load_defaults(void)
{
    strlcpy(s_settings.wifi_ssid,     CONFIG_TIMESWITCH_WIFI_SSID,     sizeof(s_settings.wifi_ssid));
    strlcpy(s_settings.wifi_password, CONFIG_TIMESWITCH_WIFI_PASSWORD, sizeof(s_settings.wifi_password));

    for (int r = 0; r < NUM_RELAYS; r++) {
        snprintf(s_settings.relay_names[r], RELAY_NAME_MAX_LEN, "Relay %d", r + 1);
        s_settings.relay_type[r]     = DEFAULT_TYPES[r];
        s_settings.relay_pulse_ms[r] = DEFAULT_PULSE_MS[r];
        for (int i = 0; i < 7; i++) {
            s_settings.schedules[r].days[i].enabled  = false;
            s_settings.schedules[r].days[i].on_hour  = 7;
            s_settings.schedules[r].days[i].on_min   = 0;
            s_settings.schedules[r].days[i].off_hour = 23;
            s_settings.schedules[r].days[i].off_min  = 0;
        }
    }
}

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS data found, using defaults.");
        return;
    }

    size_t len = sizeof(s_settings.wifi_ssid);
    nvs_get_str(h, "ssid", s_settings.wifi_ssid, &len);

    len = sizeof(s_settings.wifi_password);
    nvs_get_str(h, "pass", s_settings.wifi_password, &len);

    len = sizeof(s_settings.schedules);
    if (nvs_get_blob(h, "schedules", s_settings.schedules, &len) != ESP_OK)
        ESP_LOGW(TAG, "Geen schema's in NVS, gebruik defaults.");

    len = sizeof(s_settings.relay_names);
    if (nvs_get_blob(h, "relay_names", s_settings.relay_names, &len) != ESP_OK)
        ESP_LOGI(TAG, "Geen relaynamen in NVS, gebruik defaults.");

    len = sizeof(s_settings.relay_type);
    if (nvs_get_blob(h, "relay_type", s_settings.relay_type, &len) != ESP_OK)
        ESP_LOGI(TAG, "Geen relaytype in NVS, gebruik defaults.");

    len = sizeof(s_settings.relay_pulse_ms);
    if (nvs_get_blob(h, "relay_pulse", s_settings.relay_pulse_ms, &len) != ESP_OK)
        ESP_LOGI(TAG, "Geen pulse_ms in NVS, gebruik defaults.");

    nvs_close(h);
}

esp_err_t settings_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    load_defaults();
    load_from_nvs();

    s_initialized = true;
    ESP_LOGI(TAG, "Settings loaded (SSID: '%s')", s_settings.wifi_ssid);
    return ESP_OK;
}

const settings_t *settings_get(void)
{
    return &s_settings;
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) { xSemaphoreGive(s_mutex); return ret; }

    if (ssid)     { strlcpy(s_settings.wifi_ssid,     ssid,     sizeof(s_settings.wifi_ssid));     nvs_set_str(h, "ssid", s_settings.wifi_ssid); }
    if (password) { strlcpy(s_settings.wifi_password, password, sizeof(s_settings.wifi_password)); nvs_set_str(h, "pass", s_settings.wifi_password); }

    nvs_commit(h);
    nvs_close(h);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t settings_set_schedule_for_relay(uint8_t relay_index, const relay_schedule_t *schedule)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (relay_index >= NUM_RELAYS || !schedule) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memcpy(&s_settings.schedules[relay_index], schedule, sizeof(relay_schedule_t));

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        nvs_set_blob(h, "schedules", s_settings.schedules, sizeof(s_settings.schedules));
        nvs_commit(h);
        nvs_close(h);
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Schema voor relay %d opgeslagen", relay_index + 1);
    return ret;
}

esp_err_t settings_set_relay_name(uint8_t relay_index, const char* name)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (relay_index >= NUM_RELAYS || !name) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    strlcpy(s_settings.relay_names[relay_index], name, RELAY_NAME_MAX_LEN);

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        nvs_set_blob(h, "relay_names", s_settings.relay_names, sizeof(s_settings.relay_names));
        nvs_commit(h);
        nvs_close(h);
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t settings_set_relay_config(uint8_t relay_index, uint8_t type, uint16_t pulse_ms)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (relay_index >= NUM_RELAYS) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.relay_type[relay_index]     = type;
    s_settings.relay_pulse_ms[relay_index] = pulse_ms;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        nvs_set_blob(h, "relay_type",  s_settings.relay_type,     sizeof(s_settings.relay_type));
        nvs_set_blob(h, "relay_pulse", s_settings.relay_pulse_ms, sizeof(s_settings.relay_pulse_ms));
        nvs_commit(h);
        nvs_close(h);
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Relay %d config: type=%d pulse=%dms", relay_index + 1, type, pulse_ms);
    return ret;
}

esp_err_t settings_reset(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    load_defaults();
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
