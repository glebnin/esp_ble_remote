#include "peripherals.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "PERIPHERALS";

volatile uint32_t last_pressed_pin = 0;
uint32_t s_wakeup_pin_to_process = 0;
SemaphoreHandle_t gpio_evt_semaphore = NULL;
adc_oneshot_unit_handle_t adc1_handle;

// Helper to convert bitmask to pin number
uint32_t get_pin_from_mask(uint64_t mask)
{
    if (mask & (1ULL << BTN_VOL_UP))
        return BTN_VOL_UP;
    if (mask & (1ULL << BTN_VOL_DOWN))
        return BTN_VOL_DOWN;
    if (mask & (1ULL << BTN_PAUSE))
        return BTN_PAUSE;
    return 0;
}

void IRAM_ATTR gpio_isr_handler(void *arg)
{
    last_pressed_pin = (uint32_t)arg;
    gpio_intr_disable(last_pressed_pin);
}

void gpio_init_buttons()
{
    ESP_LOGI(TAG, "Init GPIO");
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Trigger on press (High to Low)
        .pin_bit_mask = (1ULL << BTN_VOL_UP) | (1ULL << BTN_VOL_DOWN) | (1ULL << BTN_PAUSE) | (1ULL << BTN_MOUSE_LEFT) | (1ULL << BTN_MOUSE_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
    };
    gpio_config(&io_conf);

    // Install the global ISR service
    gpio_install_isr_service(0);
    // Add handlers for specific pins
    gpio_isr_handler_add(BTN_VOL_UP, gpio_isr_handler, (void *)BTN_VOL_UP);
    gpio_isr_handler_add(BTN_VOL_DOWN, gpio_isr_handler, (void *)BTN_VOL_DOWN);
    gpio_isr_handler_add(BTN_PAUSE, gpio_isr_handler, (void *)BTN_PAUSE);
    gpio_isr_handler_add(BTN_MOUSE_LEFT, gpio_isr_handler, (void *)BTN_MOUSE_LEFT);
    gpio_isr_handler_add(BTN_MOUSE_RIGHT, gpio_isr_handler, (void *)BTN_MOUSE_RIGHT);

    gpio_wakeup_enable(BTN_VOL_UP, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_VOL_DOWN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_PAUSE, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_MOUSE_LEFT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_MOUSE_RIGHT, GPIO_INTR_LOW_LEVEL);
}

// Vector-based joystick mapping to fix axial/diagonal inconsistency
void process_joystick_radial(int raw_x, int raw_y, int8_t *out_dx, int8_t *out_dy)
{
    const int center_x = 1900;
    const int center_y = 1900;
    const int deadzone = 70;
    const float max_speed = 15.0f;

    // Calculate offsets from center
    float off_x = (float)raw_x - center_x;
    float off_y = (float)raw_y - center_y;

    // Calculate magnitude (distance from center)
    float magnitude = sqrtf(off_x * off_x + off_y * off_y);

    if (magnitude < deadzone)
    {
        *out_dx = 0;
        *out_dy = 0;
        return;
    }

    // Normalize magnitude (0.0 to 1.0)
    // Assume the physical limit is a circle with radius ~1900
    float normalized_mag = (magnitude - deadzone) / (1900.0f - deadzone);
    if (normalized_mag > 1.0f)
        normalized_mag = 1.0f;

    // Apply Cubic Curve to magnitude for precision
    float scaled_mag = normalized_mag * normalized_mag * normalized_mag;

    // Project scaled magnitude back to X and Y axes
    // This ensures diagonal speed equals axial speed
    float final_speed = scaled_mag * max_speed;

    *out_dx = -(int8_t)((off_x / magnitude) * final_speed);
    *out_dy = -(int8_t)((off_y / magnitude) * final_speed);
}

void adc_init()
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    adc_oneshot_config_channel(adc1_handle, JOYSTICK_X_CH, &config);
    adc_oneshot_config_channel(adc1_handle, JOYSTICK_Y_CH, &config);
    ESP_LOGI(TAG, "ADC initialized");
}

int read_adc(adc_channel_t channel)
{
    int adc_raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &adc_raw));
    return adc_raw;
}
