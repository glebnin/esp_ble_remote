#include "esp_hidd.h"
#include "esp_log.h"
#include "hid_dev.h"
#include "freertos/task.h"

#include "gap.h"
#include "led.h"


static const char *TAG = "HID_DEV";


void ble_hid_task_start_up(void);
void ble_hid_task_shut_down(void);

const unsigned char mediaReportMap[] = {
    // --- Mouse Clicks (Report ID 2) ---
    0x05, 0x01,        // Usage Page (Generic Desktop Controls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1) - Left Click
    0x29, 0x02,        //     Usage Maximum (2) - Right Click
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x02,        //     Report Count (2)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x06,        //     Report Size (6) - Padding to 1 byte
    0x81, 0x03,        //     Input (Const,Var,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection (Physical)
    0xC0,              // End Collection (Application)

    // --- Media Keys (Report ID 3) ---
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x03,        //   Report Count (3)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x05,        //   Report Size (5) - Padding to 1 byte
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0xC0               // End Collection
};

static esp_hid_raw_report_map_t ble_report_map = {
    .data = mediaReportMap,
    .len = sizeof(mediaReportMap)};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESPremote",
    .manufacturer_name = "Espressif",
    .serial_number = "1234567890",
    .report_maps = &ble_report_map,
    .report_maps_len = 1};

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

local_param_t s_ble_hid_param = {0};

void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
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
        led_set_mode(LED_STATE_CONNECTED);
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
        led_set_mode(LED_STATE_CONNECTING);
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

// Helper function to send media reports
void send_media_key(uint8_t key_mask)
{
    if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev))
    {
        uint8_t report[HID_CC_IN_RPT_LEN] = {key_mask};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MEDIA, report, HID_CC_IN_RPT_LEN);

        // Send release
        uint8_t release[HID_CC_IN_RPT_LEN] = {0x00};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MEDIA, release, HID_CC_IN_RPT_LEN);
        ESP_LOGI(TAG, "Media key sent");
    }
}

void send_mouse_click(uint8_t click_mask)
{
    if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev))
    {
        uint8_t report[HID_MOUSE_IN_RPT_LEN] = {click_mask, 0x00, 0x00};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MOUSE, report, HID_MOUSE_IN_RPT_LEN);
                
        // release
        uint8_t release[HID_MOUSE_IN_RPT_LEN] = {0x00, 0x00, 0x00};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MOUSE, release, HID_MOUSE_IN_RPT_LEN);
        ESP_LOGI(TAG, "Mouse click sent");
    }
}

void send_mouse_move(int8_t dx, int8_t dy)
{
    if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev))
    {
        uint8_t report[HID_MOUSE_IN_RPT_LEN] = {0x00, (uint8_t)dx, (uint8_t)dy};
        esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_MOUSE, report, HID_MOUSE_IN_RPT_LEN);
    }
}


int hid_dev_init(){
    return esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev);
}
const char *hid_dev_get_name()
{
    return ble_hid_config.device_name;
}

int hid_dev_is_connected()
{
    return esp_hidd_dev_connected(s_ble_hid_param.hid_dev);
}


TaskHandle_t * hid_dev_get_task_hdl_ptr()
{
    return &s_ble_hid_param.task_hdl;
}