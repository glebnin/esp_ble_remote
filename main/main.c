#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_hidd.h"
#include "gap.h"

#define BTN_VOL_UP GPIO_NUM_5
#define BTN_VOL_DOWN GPIO_NUM_2
#define BTN_PAUSE GPIO_NUM_6

#define HID_RPT_ID_MEDIA 3

static const char *TAG = "ESP_HID_APP";

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

static local_param_t s_ble_hid_param = {0};

const unsigned char mediaReportMap[] = {
    0x05, 0x0C, // Usage Page (Consumer)
    0x09, 0x01, // Usage (Consumer Control)
    0xA1, 0x01, // Collection (Application)
    0x85, 0x03, //   Report ID (3)
    0x09, 0xE9, //   Usage (Volume Increment) - Bit 0
    0x09, 0xEA, //   Usage (Volume Decrement) - Bit 1
    0x09, 0xCD, //   Usage (Play/Pause)       - Bit 2
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x03, //   Report Count (3)
    0x81, 0x02, //   Input (Data,Var,Abs)
    0x75, 0x05, //   Report Size (5) - Padding
    0x95, 0x01, //   Report Count (1)
    0x81, 0x03, //   Input (Const,Var,Abs)
    0xC0        // End Collection
};
#define HID_CC_IN_RPT_LEN 1

// Binary semaphore to wake up the HID task
static SemaphoreHandle_t gpio_evt_semaphore = NULL;
static uint32_t last_pressed_pin = 0;

static esp_hid_raw_report_map_t ble_report_map = {
    .data = mediaReportMap,
    .len = sizeof(mediaReportMap)};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP Keyboard",
    .manufacturer_name = "Espressif",
    .serial_number = "1234567890",
    .report_maps = &ble_report_map,
    .report_maps_len = 1};

// Helper function to send media reports
void send_media_key(uint8_t key_mask)
{
    if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev))
    {
        // Send only 1 byte as defined in report map
        uint8_t report[HID_CC_IN_RPT_LEN] = {key_mask};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MEDIA, report, HID_CC_IN_RPT_LEN);

        // Send release report
        uint8_t release[HID_CC_IN_RPT_LEN] = {0x00};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MEDIA, release, HID_CC_IN_RPT_LEN);
        ESP_LOGI(TAG, "Media key sent");
    }
}

void gpio_init_buttons();

void ble_hid_demo_task_kbd(void *pvParameters)
{
    gpio_evt_semaphore = xSemaphoreCreateBinary();
    gpio_init_buttons();

    while (1)
    {
        if (xSemaphoreTake(gpio_evt_semaphore, pdMS_TO_TICKS(10000)))
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(last_pressed_pin) == 0)
            {
                // Handle button presses
                if (last_pressed_pin == BTN_VOL_UP)
                    send_media_key(0x01);
                else if (last_pressed_pin == BTN_VOL_DOWN)
                    send_media_key(0x02);
                else if (last_pressed_pin == BTN_PAUSE)
                    send_media_key(0x04);
            }
            gpio_intr_enable(last_pressed_pin);
        }
        else
        {
            ESP_LOGI(TAG, "Preparing for Deep Sleep...");

            // 1. Stop BLE advertising and disconnect to release resources gracefully
            ble_gap_adv_stop();
            vTaskDelay(pdMS_TO_TICKS(100));

            // 2. Configure RTC pins AFTER stopping heavy processes
            gpio_num_t buttons[] = {BTN_VOL_UP, BTN_VOL_DOWN, BTN_PAUSE};
            for (int i = 0; i < 3; i++)
            {
                rtc_gpio_init(buttons[i]);
                rtc_gpio_set_direction(buttons[i], RTC_GPIO_MODE_INPUT_ONLY);
                rtc_gpio_pullup_en(buttons[i]);
                rtc_gpio_pulldown_dis(buttons[i]);
            }

            uint64_t mask = (1ULL << BTN_VOL_UP) | (1ULL << BTN_VOL_DOWN) | (1ULL << BTN_PAUSE);
            esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

            ESP_LOGI(TAG, "Starting Deep Sleep...");
            esp_deep_sleep_start();
        }
    }
}

void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}


static void IRAM_ATTR gpio_isr_handler(void *arg)
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

void ble_hid_task_start_up(void)
{
    if (s_ble_hid_param.task_hdl)
    {
        // Task already exists
        return;
    }
    xTaskCreate(ble_hid_demo_task_kbd, "ble_hid_demo_task_kbd", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
    ESP_LOGI(TAG, "Task created");
}

void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl)
    {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
    ESP_LOGI(TAG, "Task deleted");
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    static const char *TAG = "HID_DEV_BLE";

    switch (event)
    {
    case ESP_HIDD_START_EVENT:
    {
        ESP_LOGI(TAG, "START");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_CONNECT_EVENT:
    {
        ESP_LOGI(TAG, "CONNECT");
        esp_hidd_dev_battery_set(param->connect.dev, 100);
        ble_hid_task_start_up();
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
    {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index, param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        break;
    }
    case ESP_HIDD_CONTROL_EVENT:
    {
        ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index, param->control.control ? "EXIT_" : "");
        if (param->control.control)
        {
            // exit suspend
            ble_hid_task_start_up();
        }
        else
        {
            // suspend
            ble_hid_task_shut_down();
        }
        break;
    }
    case ESP_HIDD_OUTPUT_EVENT:
    {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index, esp_hid_usage_str(param->output.usage), param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        break;
    }
    case ESP_HIDD_FEATURE_EVENT:
    {
        ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:", param->feature.map_index, esp_hid_usage_str(param->feature.usage), param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT:
    {
        ESP_LOGI(TAG, "DISCONNECT: %s", esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        ble_hid_task_shut_down();
        // esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT:
    {
        ESP_LOGI(TAG, "STOP");
        break;
    }
    default:
        break;
    }
    return;
}
void    ble_store_config_init(void);


void app_main(void)
{
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

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, ble_hid_config.device_name);

    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "setting ble device");
    ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev);
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