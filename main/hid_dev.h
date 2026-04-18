#ifndef HID_DEV_H
#define HID_DEV_H


#include "esp_hidd.h"
#define HID_CC_IN_RPT_LEN 1
#define HID_RPT_ID_MEDIA 3

void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);
void ble_hid_task_shut_down(void);
void ble_hid_task_start_up(void);
void send_media_key(uint8_t key_mask);

int hid_dev_init();
const char *hid_dev_get_name();
int hid_dev_is_connected();
TaskHandle_t *hid_dev_get_task_hdl_ptr();

#endif // HID_DEV_H