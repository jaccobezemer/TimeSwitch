#include "wifi_manager.h"
#include "settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

static wifi_mgr_callback_t s_callback   = NULL;
static bool                s_connected  = false;
static char                s_ip_str[16] = {0};
static EventGroupHandle_t  s_wifi_event_group;
static int                 s_retry_count = 0;

#define WIFI_CONNECTED_BIT BIT0
#define MAX_RETRY          INT32_MAX

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_retry_count++;
        if (s_callback) s_callback(WIFI_MGR_EVENT_DISCONNECTED, NULL);

        int delay_s = s_retry_count < 5 ? s_retry_count : 5;
        ESP_LOGW(TAG, "Verbinding verbroken, opnieuw proberen in %ds...", delay_s);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "IP verkregen: %s", s_ip_str);
        s_connected   = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_callback) s_callback(WIFI_MGR_EVENT_CONNECTED, NULL);
    }
}

esp_err_t wifi_manager_init(wifi_mgr_callback_t callback)
{
    s_callback        = callback;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, CONFIG_TIMESWITCH_HOSTNAME));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,    &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    const settings_t *cfg = settings_get();

    if (cfg->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "Geen WiFi credentials ingesteld");
        if (s_callback) s_callback(WIFI_MGR_EVENT_NO_CREDENTIALS, NULL);
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid,     cfg->wifi_ssid,     sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, cfg->wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(cfg->wifi_password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Verbinden met '%s'...", cfg->wifi_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "AP starten: 'TimeSwitch-Setup'...");
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = "TimeSwitch-Setup",
            .ssid_len       = 16,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP gestart — verbind met 'TimeSwitch-Setup' en open http://192.168.4.1");
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    esp_wifi_disconnect();

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid,     ssid,     sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_retry_count = 0;
    esp_wifi_connect();

    ESP_LOGI(TAG, "Opnieuw verbinden met '%s'...", ssid);
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip_str(void)
{
    return strlen(s_ip_str) > 0 ? s_ip_str : NULL;
}
