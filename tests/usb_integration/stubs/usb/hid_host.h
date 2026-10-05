#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *hid_host_device_handle_t;
typedef enum { HID_HOST_DRIVER_EVENT_CONNECTED } hid_host_driver_event_t;
typedef enum { HID_HOST_INTERFACE_EVENT_INPUT_REPORT, HID_HOST_INTERFACE_EVENT_DISCONNECTED, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR } hid_host_interface_event_t;
#define HID_PROTOCOL_NONE 0
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_MOUSE 2
#define HID_SUBCLASS_BOOT_INTERFACE 1
#define HID_REPORT_PROTOCOL_REPORT 1
#define HID_REPORT_PROTOCOL_BOOT 0
typedef struct { uint8_t addr, iface_num, sub_class, proto; } hid_host_dev_params_t;
typedef struct { void (*callback)(hid_host_device_handle_t, hid_host_interface_event_t, void *); void *callback_arg; } hid_host_device_config_t;
esp_err_t hid_host_device_get_params(hid_host_device_handle_t, hid_host_dev_params_t *);
esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t, uint8_t *, size_t, size_t *);
esp_err_t hid_host_device_open(hid_host_device_handle_t, const hid_host_device_config_t *);
esp_err_t hid_host_device_close(hid_host_device_handle_t);
esp_err_t hid_host_device_start(hid_host_device_handle_t);
const uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t, size_t *);
esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t, int);
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t, int, int);
