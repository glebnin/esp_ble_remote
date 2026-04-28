#ifndef LED_H
#define LED_H

#include <stdint.h>

// Light Emitting Diode states
typedef enum {
    LED_STATE_OFF,
    LED_STATE_CONNECTING, // Blinking
    LED_STATE_CONNECTED,  // Solid or breathing
    LED_STATE_SLEEP_PREP  // Quick flash before sleep
} led_mode_t;

// Function prototypes
void led_init(void);
void led_set_mode(led_mode_t mode);
void led_task(void *pvParameters);
void led_deinit(void);

#endif