#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "freertos/FreeRTOS.h"

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_attr.h"

#include "esp_adc/adc_oneshot.h"

// Configuration for ADC
#define JOYSTICK_X_CH ADC_CHANNEL_7  // GPIO 
#define JOYSTICK_Y_CH ADC_CHANNEL_2  // GPIO 
#define ADC_BITWIDTH ADC_BITWIDTH_12 // 0-4095 range
#define ADC_ATTEN ADC_ATTEN_DB_12    // Full scale voltage (~3.3V)

#define BTN_VOL_UP GPIO_NUM_5
#define BTN_VOL_DOWN GPIO_NUM_2
#define BTN_PAUSE GPIO_NUM_6
#define BTN_MOUSE_LEFT GPIO_NUM_41
#define BTN_MOUSE_RIGHT GPIO_NUM_7


extern volatile uint32_t last_pressed_pin;
// Global variable to store the pin that woke up the ESP
extern uint32_t s_wakeup_pin_to_process;

uint32_t get_pin_from_mask(uint64_t mask);
void gpio_init_buttons();
void process_joystick_radial(int raw_x, int raw_y, int8_t *out_dx, int8_t *out_dy);
void adc_init();
int read_adc(adc_channel_t channel);


#endif // PERIPHERALS_H