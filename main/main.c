/*
TODO:
1. Add comments
2. Make map_joystick_to_mouse() quadrantic
3. Joystick interrupts
4. Status LED

*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "esp_log.h"
#include "nvs.h"

#include "gap.h"
#include "hid_dev.h"
#include "peripherals.h"
#include "led.h"

static const char *TAG = "ESP_HID_APP";

#define INACTIVITY_TIMEOUT_MS 20000 // 30 seconds
static uint32_t last_activity_tick = 0;

void power_management_task(void *pvParameters)
{
    last_activity_tick = xTaskGetTickCount();

    while (1)
    {
        uint32_t current_tick = xTaskGetTickCount();
        uint32_t elapsed = (current_tick - last_activity_tick) * portTICK_PERIOD_MS;

        if (elapsed >= INACTIVITY_TIMEOUT_MS)
        {
            ESP_LOGI("POWER", "No activity for %d ms. Preparing for Deep Sleep...", INACTIVITY_TIMEOUT_MS);

            ESP_LOGI(TAG, "Preparing for Deep Sleep...");
            led_set_mode(LED_STATE_SLEEP_PREP);

            // 1. Stop BLE advertising and disconnect to release resources gracefully
            ble_gap_adv_stop();
            vTaskDelay(pdMS_TO_TICKS(100));

            // 2. Configure RTC pins AFTER stopping heavy processes
            gpio_num_t buttons[] = {BTN_VOL_UP, BTN_VOL_DOWN, BTN_PAUSE, BTN_MOUSE_RIGHT};
            for (int i = 0; i < 4; i++)
            {
                rtc_gpio_init(buttons[i]);
                rtc_gpio_set_direction(buttons[i], RTC_GPIO_MODE_INPUT_ONLY);
                rtc_gpio_pullup_en(buttons[i]);
                rtc_gpio_pulldown_dis(buttons[i]);
            }

            uint64_t mask = (1ULL << BTN_VOL_UP) | (1ULL << BTN_VOL_DOWN) | (1ULL << BTN_PAUSE) | (1ULL << BTN_MOUSE_RIGHT);
            esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
            led_deinit();
            
            ESP_LOGI(TAG, "Starting Deep Sleep...");
            esp_deep_sleep_start();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void ble_hid_task(void *pvParameters)
{
    gpio_init_buttons();

    ESP_LOGI(TAG, "Waiting for connection to send wakeup key...");
    while (!hid_dev_is_connected())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

#ifdef GPIO_WAKEUP_FIRST_SEND
    // 2. If we have a pending wakeup pin, process it once
    if (s_wakeup_pin_to_process != 0)
    {
        vTaskDelay(pdMS_TO_TICKS(500)); // Small delay for Windows HID driver to be ready
        ESP_LOGI(TAG, "Processing wakeup key for pin: %lu", s_wakeup_pin_to_process);

        if (s_wakeup_pin_to_process == BTN_VOL_UP)
            send_media_key(0x01);
        else if (s_wakeup_pin_to_process == BTN_VOL_DOWN)
            send_media_key(0x02);
        else if (s_wakeup_pin_to_process == BTN_PAUSE)
            send_media_key(0x04);

        s_wakeup_pin_to_process = 0; // Clear it
    }
#endif

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (last_pressed_pin != 0 && gpio_get_level(last_pressed_pin) == 0) // last condition is for preventing sending media button if the semaphore been given from the joystick task
        // conditions positioning is important here!
        {
            // Handle button presses
            if (last_pressed_pin == BTN_VOL_UP)
                send_media_key(0x01);
            else if (last_pressed_pin == BTN_VOL_DOWN)
                send_media_key(0x02);
            else if (last_pressed_pin == BTN_PAUSE)
                send_media_key(0x04);
            else if (last_pressed_pin == BTN_MOUSE_LEFT)
                send_mouse_click(0x01);
            else if (last_pressed_pin == BTN_MOUSE_RIGHT)
                send_mouse_click(0x02);
            while (gpio_get_level(last_pressed_pin) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            gpio_intr_enable(last_pressed_pin);
            last_pressed_pin = 0;
            last_activity_tick = xTaskGetTickCount();
        }
    }
}

void joystick_task(void *pvParameters)
{
    adc_init();

    while (!hid_dev_is_connected())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    while (1)
    {
        int x_val = 0, y_val = 0;
        x_val = read_adc(JOYSTICK_X_CH);
        y_val = read_adc(JOYSTICK_Y_CH);

        int8_t dx, dy;
        process_joystick_radial(x_val, y_val, &dx, &dy);
        // ESP_LOGI(TAG, "X: %d, Y: %d -> dx: %d, dy: %d", x_val, y_val, dx, dy);

        if (dx != 0 || dy != 0)
        {
            send_mouse_move(dx, dy);
            last_activity_tick = xTaskGetTickCount();
        }
        // Delay defines the "smoothness" and update rate (10-20ms is ideal)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

void ble_hid_task_start_up(void)
{
    if (hid_dev_get_task_hdl_ptr() == NULL)
    {
        // Task already exists
        return;
    }
    xTaskCreate(ble_hid_task, "ble_hid_task", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                hid_dev_get_task_hdl_ptr());
    xTaskCreate(joystick_task, "joystick_task", 3 * 1024, NULL, configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(power_management_task, "power_management_task", 3 * 1024, NULL, configMAX_PRIORITIES - 5, NULL);

    ESP_LOGI(TAG, "Tasks created");
}

void ble_hid_task_shut_down(void)
{
    if (hid_dev_get_task_hdl_ptr() != NULL)
    {
        vTaskDelete(*hid_dev_get_task_hdl_ptr());
        *hid_dev_get_task_hdl_ptr() = NULL;
    }
    ESP_LOGI(TAG, "Task deleted");
}

void ble_store_config_init(void);

void app_main(void)
{
    led_init();
    xTaskCreate(led_task, "led_task", 2048, NULL, 2, NULL);
    led_set_mode(LED_STATE_CONNECTING);

    /* Initialize NVS. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "setting hid gap");
    ret = gap_init();
    ESP_ERROR_CHECK(ret);

#ifdef GPIO_WAKEUP_FIRST_SEND

    // Check if we woke up from Deep Sleep via EXT1
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1)
    {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        s_wakeup_pin_to_process = get_pin_from_mask(wakeup_pin_mask);
        ESP_LOGI(TAG, "Woke up by GPIO: %lu", s_wakeup_pin_to_process);
    }
#endif

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, hid_dev_get_name());

    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "setting ble device");
    ret = hid_dev_init();
    ESP_ERROR_CHECK(ret);

    ble_store_config_init();

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* Starting nimble task after gatts is initialized*/
    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret)
    {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
}