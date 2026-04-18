#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_attr.h"


#define BTN_VOL_UP GPIO_NUM_5
#define BTN_VOL_DOWN GPIO_NUM_2
#define BTN_PAUSE GPIO_NUM_6

extern volatile uint32_t last_pressed_pin;
// Global variable to store the pin that woke up the ESP
extern uint32_t s_wakeup_pin_to_process;
extern SemaphoreHandle_t gpio_evt_semaphore;

uint32_t get_pin_from_mask(uint64_t mask);
void gpio_init_buttons();

#endif // PERIPHERALS_H