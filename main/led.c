#include "led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED_STRIP_GPIO 48
#define LED_STRIP_COUNT 1

static led_strip_handle_t led_strip;
static led_mode_t current_mode = LED_STATE_OFF;
static const char *TAG = "LED_SYSTEM";

void led_init(void) {
    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT (Remote Control) backend configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void led_set_mode(led_mode_t mode) {
    current_mode = mode;
}

void led_task(void *pvParameters) {
    uint8_t brightness = 0;
    int8_t step = 5;

    while (1) {
        switch (current_mode) {
            case LED_STATE_CONNECTING:
                // Blinking blue
                led_strip_set_pixel(led_strip, 0, 0, 0, 50);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(300));
                led_strip_clear(led_strip);
                vTaskDelay(pdMS_TO_TICKS(300));
                break;

            case LED_STATE_CONNECTED:
                // Breathing Green effect
                led_strip_set_pixel(led_strip, 0, 0, 50, 0);
                led_strip_refresh(led_strip);
                break;

            case LED_STATE_SLEEP_PREP:
                // Solid Red before power down
                led_strip_set_pixel(led_strip, 0, 100, 0, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                led_strip_clear(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            default:
                led_strip_clear(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

void led_deinit(void) {
    if (led_strip) {
        // Turn off all LEDs
        led_strip_clear(led_strip);
        
        // Small delay to ensure the WS2812 chip processed the "off" signal
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Delete the driver and free RMT/DMA resources
        led_strip_del(led_strip);
        led_strip = NULL;

        // Reset the pin to a high-impedance state to prevent parasitic leakage
        gpio_reset_pin(LED_STRIP_GPIO);
        gpio_set_direction(LED_STRIP_GPIO, GPIO_MODE_INPUT);
        
        ESP_LOGI(TAG, "LED system deinitialized for sleep");
    }
}