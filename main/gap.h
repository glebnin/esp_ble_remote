#ifndef GAP_H
#define GAP_H


#include "esp_err.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/ble.h"
#include "host/ble_sm.h"
#include "esp_hid_common.h"
#include "esp_bt.h"

#define HIDD_BLE_MODE 0x01


esp_err_t gap_init(void);
esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name);
esp_err_t esp_hid_ble_gap_adv_start(void);

#endif