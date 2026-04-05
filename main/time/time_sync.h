#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t time_sync_init(void);
bool      time_sync_is_synced(void);
bool      time_sync_get_localtime(struct tm *out_tm);
