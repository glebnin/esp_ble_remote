#include "peripherals.h"
#include "esp_log.h"


static const char *TAG = "PERIPHERALS";

volatile uint32_t last_pressed_pin = 0; 
uint32_t s_wakeup_pin_to_process = 0;
SemaphoreHandle_t gpio_evt_semaphore = NULL;

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
        .pin_bit_mask = (1ULL << BTN_VOL_UP) | (1ULL << BTN_VOL_DOWN) | (1ULL << BTN_PAUSE),
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

    gpio_wakeup_enable(BTN_VOL_UP, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_VOL_DOWN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BTN_PAUSE, GPIO_INTR_LOW_LEVEL);
}