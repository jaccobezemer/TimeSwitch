#include <driver/gpio.h>
#include <esp_log.h>

#include "hardware.h"
#include "relay.h"

static const char *TAG = "relay";

static bool relay_state = false;

esp_err_t relay_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t r = gpio_config(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(r));
        return r;
    }

    gpio_set_level(RELAY_GPIO, 0);
    relay_state = false;

    ESP_LOGI(TAG, "Relay initialized on GPIO %d", RELAY_GPIO);
    return ESP_OK;
}

esp_err_t relay_set(bool on)
{
    relay_state = on;
    esp_err_t r = gpio_set_level(RELAY_GPIO, on ? 1 : 0);
    ESP_LOGI(TAG, "Relay %s", on ? "ON" : "OFF");
    return r;
}

bool relay_get(void)
{
    return relay_state;
}
