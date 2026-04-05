#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_MGR_EVENT_CONNECTED,
    WIFI_MGR_EVENT_DISCONNECTED,
    WIFI_MGR_EVENT_NO_CREDENTIALS,
    WIFI_MGR_EVENT_CONNECT_FAILED,
} wifi_mgr_event_t;

typedef void (*wifi_mgr_callback_t)(wifi_mgr_event_t event, void *arg);

esp_err_t   wifi_manager_init(wifi_mgr_callback_t callback);
esp_err_t   wifi_manager_start(void);
esp_err_t   wifi_manager_start_ap(void);
esp_err_t   wifi_manager_set_credentials(const char *ssid, const char *password);
bool        wifi_manager_is_connected(void);
const char *wifi_manager_get_ip_str(void);
