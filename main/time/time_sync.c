#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "time_sync";
static bool s_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    struct tm t;
    localtime_r(&tv->tv_sec, &t);
    ESP_LOGI(TAG, "Tijd gesynchroniseerd: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

esp_err_t time_sync_init(void)
{
    setenv("TZ", CONFIG_TIMESWITCH_TIMEZONE, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_TIMESWITCH_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP gestart (server: %s, tijdzone: %s)",
             CONFIG_TIMESWITCH_NTP_SERVER, CONFIG_TIMESWITCH_TIMEZONE);
    return ESP_OK;
}

bool time_sync_is_synced(void)
{
    return s_synced;
}

bool time_sync_get_localtime(struct tm *out_tm)
{
    if (!out_tm) return false;
    time_t now = time(NULL);
    localtime_r(&now, out_tm);
    return s_synced;
}
