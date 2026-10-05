#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef void *hid_host_device_handle_t;
typedef struct { uint8_t addr, iface_num; } hid_host_dev_params_t;
esp_err_t hid_host_device_get_params(hid_host_device_handle_t handle, hid_host_dev_params_t *params);
esp_err_t hid_host_device_stop(hid_host_device_handle_t handle);
esp_err_t hid_host_device_start(hid_host_device_handle_t handle);
