#include "peripherals.h"
#include "esp_log.h"


static const char *TAG = "PERIPHERALS";

volatile uint32_t last_pressed_pin = 0; 
uint32_t s_wakeup_pin_to_process = 0;
SemaphoreHandle_t gpio_evt_semaphore = NULL;
adc_oneshot_unit_handle_t adc1_handle;

// Helper to convert bitmask to pin number
uint32_t get_pin_from_mask(uint64_t mask) {
    if (mask & (1ULL << BTN_VOL_UP)) return BTN_VOL_UP;
    if (mask & (1ULL << BTN_VOL_DOWN)) return BTN_VOL_DOWN;
    if (mask & (1ULL << BTN_PAUSE)) return BTN_PAUSE;
    return 0;
}

void IRAM_ATTR gpio_isr_handler(void *arg)
{
    last_pressed_pin = (uint32_t)arg;
    xSemaphoreGiveFromISR(gpio_evt_semaphore, NULL);
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

// Calculate relative speed based on joystick position
int8_t map_joystick_to_mouse(int adc_value) {
    const int center = 2048;
    const int deadzone = 200;
    const int max_adc = 4095;
    const int8_t max_speed = 15; // Max pixels per update

    int offset = adc_value - center;

    if (abs(offset) < deadzone) {
        return 0;
    }

    // Normalize and scale to speed
    float normalized = (float)(offset) / (center - deadzone);
    int speed = (int)(normalized * max_speed);

    // Clamp values to int8_t range
    if (speed > 127) return 127;
    if (speed < -127) return -127;

    return speed;
}

void adc_init(){
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

int read_adc(adc_channel_t channel) {
    int adc_raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &adc_raw));
    return adc_raw;
}
