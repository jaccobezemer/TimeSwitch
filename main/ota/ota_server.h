#pragma once

#include "esp_err.h"

esp_err_t ota_server_start(void);
int       ota_get_progress(void);
