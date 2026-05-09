#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "hardware.h"
#include "settings.h"
#include "relay.h"

static const char *TAG = "relay";

typedef struct {
    gpio_num_t         gpio;
    uint8_t            type;
    uint16_t           pulse_ms;
    esp_timer_handle_t start_timer; // wacht tot de staggered slot, dan GPIO HIGH
    esp_timer_handle_t stop_timer;  // na pulse_ms GPIO LOW
} relay_hw_t;

static relay_hw_t s_relays[NUM_RELAYS];
static uint8_t    s_relay_states = 0;

// Tijdstip (µs, monotone klok) waarop het volgende impulsrelais mag starten
static int64_t s_next_slot_us = 0;

static const gpio_num_t RELAY_GPIOS[] = {
    RELAY_1_GPIO, RELAY_2_GPIO, RELAY_3_GPIO,
    RELAY_4_GPIO, RELAY_5_GPIO, RELAY_6_GPIO
};

static void pulse_stop_cb(void *arg)
{
    relay_hw_t *r = (relay_hw_t *)arg;
    gpio_set_level(r->gpio, 0);
}

static void pulse_start_cb(void *arg)
{
    relay_hw_t *r = (relay_hw_t *)arg;
    gpio_set_level(r->gpio, 1);
    esp_timer_start_once(r->stop_timer, (uint64_t)r->pulse_ms * 1000ULL);
}

esp_err_t relay_init(void)
{
    const settings_t *cfg = settings_get();

    uint64_t pin_mask = 0;
    for (int i = 0; i < NUM_RELAYS; i++) {
        s_relays[i].gpio     = RELAY_GPIOS[i];
        s_relays[i].type     = cfg->relay_type[i];
        s_relays[i].pulse_ms = cfg->relay_pulse_ms[i];
        pin_mask |= (1ULL << RELAY_GPIOS[i]);
    }

    gpio_config_t io_cfg = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t r = gpio_config(&io_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config mislukt: %s", esp_err_to_name(r));
        return r;
    }

    for (int i = 0; i < NUM_RELAYS; i++) {
        gpio_set_level(s_relays[i].gpio, 0);

        esp_timer_create_args_t stop_args = {
            .callback = pulse_stop_cb,
            .arg      = &s_relays[i],
            .name     = "relay_stop",
        };
        esp_timer_create(&stop_args, &s_relays[i].stop_timer);

        esp_timer_create_args_t start_args = {
            .callback = pulse_start_cb,
            .arg      = &s_relays[i],
            .name     = "relay_start",
        };
        esp_timer_create(&start_args, &s_relays[i].start_timer);
    }

    s_relay_states  = 0;
    s_next_slot_us  = 0;
    ESP_LOGI(TAG, "%d relays geïnitialiseerd.", NUM_RELAYS);
    return ESP_OK;
}

esp_err_t relay_set_config(uint8_t relay_index, uint8_t type, uint16_t pulse_ms)
{
    if (relay_index >= NUM_RELAYS) return ESP_ERR_INVALID_ARG;
    relay_hw_t *r = &s_relays[relay_index];

    esp_timer_stop(r->start_timer);
    esp_timer_stop(r->stop_timer);
    gpio_set_level(r->gpio, 0);

    r->type     = type;
    r->pulse_ms = pulse_ms;

    if (type == RELAY_TYPE_NORMAL) {
        bool on = (s_relay_states >> relay_index) & 1;
        gpio_set_level(r->gpio, on ? 1 : 0);
    }

    ESP_LOGI(TAG, "Relay %d config: type=%d pulse=%dms", relay_index + 1, type, pulse_ms);
    return ESP_OK;
}

esp_err_t relay_set_state(uint8_t relay_index, bool on)
{
    if (relay_index >= NUM_RELAYS) {
        ESP_LOGE(TAG, "Ongeldig relay index: %d", relay_index);
        return ESP_ERR_INVALID_ARG;
    }

    bool current = (s_relay_states >> relay_index) & 1;
    if (on == current) return ESP_OK;

    relay_hw_t *r = &s_relays[relay_index];

    if (r->type == RELAY_TYPE_NORMAL) {
        gpio_set_level(r->gpio, on ? 1 : 0);
    } else {
        // Bereken wanneer dit relais mag starten om PSU niet te overbelasten
        int64_t now      = esp_timer_get_time();
        int64_t start_at = (s_next_slot_us > now) ? s_next_slot_us : now;
        int64_t delay_us = start_at - now;

        // Reserveer slot: puls + minimale tussentijd voor volgend relais
        s_next_slot_us = start_at + (int64_t)r->pulse_ms * 1000LL
                                  + (int64_t)RELAY_INTER_PULSE_MS * 1000LL;

        // Stop eventuele lopende timers
        esp_timer_stop(r->start_timer);
        esp_timer_stop(r->stop_timer);
        gpio_set_level(r->gpio, 0);

        if (delay_us > 0) {
            esp_timer_start_once(r->start_timer, delay_us);
            ESP_LOGI(TAG, "Relay %d -> %s (puls over %.0fms)", relay_index + 1,
                     on ? "AAN" : "UIT", delay_us / 1000.0);
        } else {
            pulse_start_cb(r);
            ESP_LOGI(TAG, "Relay %d -> %s (puls direct)", relay_index + 1, on ? "AAN" : "UIT");
        }
    }

    if (on) s_relay_states |=  (1 << relay_index);
    else    s_relay_states &= ~(1 << relay_index);

    return ESP_OK;
}

bool relay_get_state(uint8_t relay_index)
{
    if (relay_index >= NUM_RELAYS) return false;
    return (s_relay_states >> relay_index) & 1;
}

uint8_t relay_get_all_states(void)
{
    return s_relay_states;
}
