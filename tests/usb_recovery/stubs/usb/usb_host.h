#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;
typedef struct { int event; } usb_host_client_event_msg_t;
typedef struct {
  bool is_synchronous;
  int max_num_event_msg;
  struct { void (*client_event_callback)(const usb_host_client_event_msg_t *, void *); void *callback_arg; } async;
} usb_host_client_config_t;
typedef struct __attribute__((packed)) { uint8_t bLength,bDescriptorType; uint16_t wTotalLength; } usb_config_desc_t;
typedef struct usb_transfer usb_transfer_t;
struct usb_transfer {
  uint8_t *data_buffer;
  usb_device_handle_t device_handle;
  uint8_t bEndpointAddress;
  size_t num_bytes;
  void (*callback)(usb_transfer_t *);
  void *context;
  int status;
};
#define USB_TRANSFER_STATUS_COMPLETED 0
esp_err_t usb_host_client_register(const usb_host_client_config_t *, usb_host_client_handle_t *);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t, unsigned ticks);
esp_err_t usb_host_device_open(usb_host_client_handle_t, uint8_t, usb_device_handle_t *);
esp_err_t usb_host_device_close(usb_host_client_handle_t, usb_device_handle_t);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t, const usb_config_desc_t **);
esp_err_t usb_host_transfer_alloc(size_t, int, usb_transfer_t **);
esp_err_t usb_host_transfer_free(usb_transfer_t *);
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t, usb_transfer_t *);
esp_err_t usb_host_endpoint_halt(usb_device_handle_t, uint8_t);
esp_err_t usb_host_endpoint_flush(usb_device_handle_t, uint8_t);
esp_err_t usb_host_endpoint_clear(usb_device_handle_t, uint8_t);
