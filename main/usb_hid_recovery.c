#include "usb_hid_recovery.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_RECOVERY";
static usb_host_client_handle_t s_client;
// Keep timed-out transfers and their device reference alive until the callback.
// All access occurs on the application task while it pumps this USB client.
static usb_transfer_t *s_pending;
static bool s_done;
static bool s_deferred_cleanup;


static void client_event(const usb_host_client_event_msg_t *event, void *arg) {
  // This client only opens a device while clearing a failed HID endpoint.
  (void)event;
  (void)arg;
}

esp_err_t usb_hid_recovery_init(void) {
  if (s_client != NULL) {
    return ESP_OK;
  }
  const usb_host_client_config_t config = {
      .is_synchronous = false,
      .max_num_event_msg = 8,
      .async = {.client_event_callback = client_event, .callback_arg = NULL},
  };
  return usb_host_client_register(&config, &s_client);
}

void usb_hid_recovery_poll(void) {
  if (s_client != NULL) {
    usb_host_client_handle_events(s_client, 0);
    if (s_deferred_cleanup && s_pending && s_done) {
      usb_device_handle_t device = s_pending->device_handle;
      usb_host_transfer_free(s_pending);
      s_pending = NULL;
      s_deferred_cleanup = false;
      usb_host_device_close(s_client, device);
    }
  }
}

static void transfer_done(usb_transfer_t *transfer) {
  bool *done = transfer->context;
  *done = true;
}

// Find the interrupt IN endpoint of this interface in the cached configuration.
static uint8_t find_endpoint(const usb_config_desc_t *config, uint8_t iface) {
  const uint8_t *bytes = (const uint8_t *)config;
  size_t size = config->wTotalLength;
  bool matching = false;
  for (size_t offset = 0; offset + 2 <= size;) {
    uint8_t length = bytes[offset];
    if (length < 2 || length > size - offset) {
      return 0;
    }
    if (bytes[offset + 1] == 4 && length >= 9) {
      matching = bytes[offset + 2] == iface && bytes[offset + 3] == 0;
    } else if (matching && bytes[offset + 1] == 5 && length >= 7 &&
               (bytes[offset + 2] & 0x80) &&
               (bytes[offset + 3] & 0x03) == 3) {
      return bytes[offset + 2];
    }
    offset += length;
  }
  return 0;
}

static esp_err_t clear_device_stall(usb_device_handle_t device, uint8_t endpoint) {
  usb_transfer_t *transfer = NULL;
  esp_err_t ret = usb_host_transfer_alloc(8, 0, &transfer);
  if (ret != ESP_OK) {
    return ret;
  }
  // USB standard CLEAR_FEATURE(ENDPOINT_HALT). No data stage.
  const uint8_t setup[8] = {0x02, 0x01, 0, 0, endpoint, 0, 0, 0};
  memcpy(transfer->data_buffer, setup, sizeof(setup));
  s_done = false;
  s_pending = transfer;
  transfer->device_handle = device;
  transfer->bEndpointAddress = 0;
  transfer->num_bytes = sizeof(setup);
  transfer->callback = transfer_done;
  transfer->context = &s_done;
  ret = usb_host_transfer_submit_control(s_client, transfer);
  if (ret == ESP_OK) {
    // Completion callbacks for this client must be pumped here. The HID driver's
    // own background task keeps processing its client while recovery runs.
    TickType_t started = xTaskGetTickCount();
    bool canceling = false;
    while (!s_done) {
      usb_host_client_handle_events(s_client, pdMS_TO_TICKS(10));
      if (!canceling && xTaskGetTickCount() - started > pdMS_TO_TICKS(1000)) {
        // USB host does not implement transfer timeout_ms. Cancel the failed
        // control pipe, then wait for its callback before freeing the buffer.
        esp_err_t halted = usb_host_endpoint_halt(device, 0);
        if (halted == ESP_OK) {
          usb_host_endpoint_flush(device, 0);
          usb_host_endpoint_clear(device, 0);
        }
        canceling = true;
      }
      if (canceling && !s_done &&
          xTaskGetTickCount() - started > pdMS_TO_TICKS(1100)) {
        // Cancellation may fail during unplug. Never free an in-flight buffer,
        // and never block the app queue waiting indefinitely for its callback.
        s_deferred_cleanup = true;
        return ESP_ERR_TIMEOUT;
      }
    }
    ret = !canceling && transfer->status == USB_TRANSFER_STATUS_COMPLETED
              ? ESP_OK : ESP_FAIL;
  }
  s_pending = NULL;
  usb_host_transfer_free(transfer);
  return ret;
}

esp_err_t usb_hid_recover(hid_host_device_handle_t handle) {
  usb_hid_recovery_poll();
  if (s_client == NULL || s_pending != NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  hid_host_dev_params_t params;
  esp_err_t ret = hid_host_device_get_params(handle, &params);
  if (ret != ESP_OK) {
    return ret; // The device may have disconnected while the event was queued.
  }
  usb_device_handle_t device;
  ret = usb_host_device_open(s_client, params.addr, &device);
  if (ret != ESP_OK) {
    return ret;
  }
  const usb_config_desc_t *config;
  ret = usb_host_get_active_config_descriptor(device, &config);
  uint8_t endpoint = ret == ESP_OK ? find_endpoint(config, params.iface_num) : 0;
  if (ret == ESP_OK && endpoint == 0) {
    ret = ESP_ERR_NOT_FOUND;
  }
  if (ret == ESP_OK) {
    // stop flushes/cancels pending interrupt transfers and clears host pipe halt.
    ret = hid_host_device_stop(handle);
    if (ret == ESP_ERR_INVALID_STATE) {
      // A previous recovery attempt may already have stopped this interface.
      ret = ESP_OK;
    }
  }
  if (ret == ESP_OK) {
    ret = clear_device_stall(device, endpoint);
  }
  if (ret == ESP_OK) {
    ret = hid_host_device_start(handle);
  }
  if (!s_deferred_cleanup) usb_host_device_close(s_client, device);
  ESP_LOGI(TAG, "Interface %u recovery: %s", params.iface_num,
           esp_err_to_name(ret));
  return ret;
}
