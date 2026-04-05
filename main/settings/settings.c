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

static void load_defaults(void)
{
    strlcpy(s_settings.wifi_ssid,     CONFIG_TIMESWITCH_WIFI_SSID,     sizeof(s_settings.wifi_ssid));
    strlcpy(s_settings.wifi_password, CONFIG_TIMESWITCH_WIFI_PASSWORD, sizeof(s_settings.wifi_password));

    // Standaard: elke dag 07:00-23:00, uitgeschakeld
    for (int i = 0; i < 7; i++) {
        s_settings.days[i].enabled  = false;
        s_settings.days[i].on_hour  = 7;
        s_settings.days[i].on_min   = 0;
        s_settings.days[i].off_hour = 23;
        s_settings.days[i].off_min  = 0;
    }
}

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Geen NVS data, defaults gebruikt");
        return;
    }

    size_t len = sizeof(s_settings.wifi_ssid);
    nvs_get_str(h, "ssid", s_settings.wifi_ssid, &len);

    len = sizeof(s_settings.wifi_password);
    nvs_get_str(h, "pass", s_settings.wifi_password, &len);

    len = sizeof(s_settings.days);
    nvs_get_blob(h, "schedule", s_settings.days, &len);

    nvs_close(h);
}

esp_err_t settings_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS wissen...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    load_defaults();
    load_from_nvs();

    s_initialized = true;
    ESP_LOGI(TAG, "Settings geladen (SSID: '%s')", s_settings.wifi_ssid);
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

esp_err_t settings_set_schedule(const day_schedule_t days[7])
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memcpy(s_settings.days, days, sizeof(s_settings.days));

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        nvs_set_blob(h, "schedule", s_settings.days, sizeof(s_settings.days));
        nvs_commit(h);
        nvs_close(h);
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Schema opgeslagen");
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
