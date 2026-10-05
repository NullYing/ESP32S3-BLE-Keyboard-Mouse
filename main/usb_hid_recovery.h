#pragma once

#include "esp_err.h"
#include "usb/hid_host.h"

// Call from the application task, after USB host installation.
esp_err_t usb_hid_recovery_init(void);
void usb_hid_recovery_poll(void);
esp_err_t usb_hid_recover(hid_host_device_handle_t handle);
