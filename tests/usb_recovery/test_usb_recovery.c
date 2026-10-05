#include "usb_hid_recovery.h"
#include "usb/usb_host.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int params_result, stop_result, start_result, submit_result, transfer_status;
static int halt_result, flush_result, halts, flushes;
static bool defer_after_flush;
static int starts, stops, opens, closes, allocations, frees, clears;
static bool defer_completion;
static TickType_t ticks;
static usb_transfer_t *pending;
static uint8_t iface;
static const uint8_t *config_bytes;
static char order[16];
static size_t order_len;
static const uint8_t config[] = {
  9,2,66,0,3,1,0,0x80,50,
  // Unrelated interface's interrupt IN endpoint must never be cleared.
  9,4,0,0,1,3,1,2,0, 7,5,0x81,3,8,0,1,
  // Desired interface contains OUT and bulk IN before the actual interrupt IN.
  9,4,1,0,3,3,1,2,0, 7,5,0x02,3,8,0,1, 7,5,0x83,2,8,0,1, 7,5,0x84,3,8,0,1,
  9,4,1,1,1,3,1,2,0, 2,0xff
};
static void record(char ch) { assert(order_len < sizeof(order)); order[order_len++] = ch; }
TickType_t xTaskGetTickCount(void) { return ticks; }
esp_err_t usb_host_client_register(const usb_host_client_config_t *c, usb_host_client_handle_t *out) {
  assert(!c->is_synchronous); *out = (void *)2; return ESP_OK;
}
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client, unsigned timeout) {
  (void)client; ticks += timeout;
  if (pending && !defer_completion) { usb_transfer_t *t = pending; pending = NULL; t->status = transfer_status; t->callback(t); }
  return ESP_OK;
}
esp_err_t hid_host_device_get_params(hid_host_device_handle_t handle, hid_host_dev_params_t *out) {
  assert(handle == (void *)1); out->addr = 7; out->iface_num = iface; return params_result;
}
esp_err_t usb_host_device_open(usb_host_client_handle_t client, uint8_t address, usb_device_handle_t *out) {
  (void)client; assert(address == 7); opens++; *out = (void *)3; return ESP_OK;
}
esp_err_t usb_host_device_close(usb_host_client_handle_t client, usb_device_handle_t device) {
  (void)client; assert(device == (void *)3); closes++; return ESP_OK;
}
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device, const usb_config_desc_t **out) {
  (void)device; *out = (const usb_config_desc_t *)config_bytes; return ESP_OK;
}
esp_err_t hid_host_device_stop(hid_host_device_handle_t handle) {
  (void)handle; stops++; record('S'); return stop_result;
}
esp_err_t hid_host_device_start(hid_host_device_handle_t handle) {
  (void)handle; starts++; record('R'); return start_result;
}
esp_err_t usb_host_transfer_alloc(size_t size, int iso, usb_transfer_t **out) {
  (void)iso; allocations++; *out = calloc(1, sizeof(**out)); assert(*out); (*out)->data_buffer = calloc(1, size); return ESP_OK;
}
esp_err_t usb_host_transfer_free(usb_transfer_t *t) {
  assert(pending != t); frees++; free(t->data_buffer); free(t); return ESP_OK;
}
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t client, usb_transfer_t *t) {
  (void)client;
  const uint8_t expected[] = {2,1,0,0,0x84,0,0,0};
  assert(t->num_bytes == 8 && t->bEndpointAddress == 0 && t->device_handle == (void *)3);
  assert(memcmp(t->data_buffer, expected, 8) == 0);
  record('C'); clears++;
  if (submit_result == ESP_OK) pending = t;
  return submit_result;
}
esp_err_t usb_host_endpoint_halt(usb_device_handle_t d, uint8_t endpoint) {
  (void)d; assert(endpoint == 0); halts++; return halt_result;
}
esp_err_t usb_host_endpoint_flush(usb_device_handle_t d, uint8_t endpoint) {
  (void)d; assert(endpoint == 0); flushes++;
  if (flush_result == ESP_OK) { defer_completion = defer_after_flush; transfer_status = 1; }
  return flush_result;
}
esp_err_t usb_host_endpoint_clear(usb_device_handle_t d, uint8_t endpoint) { (void)d; assert(endpoint == 0); return ESP_OK; }

static void reset(void) {
  assert(!pending);
  params_result = stop_result = start_result = submit_result = ESP_OK;
  transfer_status = 0; defer_completion = false; ticks = 0;
  halt_result = flush_result = ESP_OK; halts = flushes = 0; defer_after_flush = false;
  starts = stops = opens = closes = allocations = frees = clears = 0;
  memset(order, 0, sizeof(order)); order_len = 0;
  iface = 1; config_bytes = config;
}
int main(void) {
  assert(sizeof(config) == 66);
  assert(usb_hid_recover((void *)1) == ESP_ERR_INVALID_STATE);
  assert(usb_hid_recovery_init() == ESP_OK);
  assert(usb_hid_recovery_init() == ESP_OK);
  reset(); assert(usb_hid_recover((void *)1) == ESP_OK);
  assert(memcmp(order, "SCR", 3) == 0 && starts == 1 && stops == 1 && clears == 1 && closes == 1 && frees == 1);
  puts("PASS USB recovery: stop -> device CLEAR_FEATURE -> start, exact interrupt IN endpoint");
  reset(); submit_result = ESP_FAIL;
  assert(usb_hid_recover((void *)1) == ESP_FAIL && starts == 0 && closes == 1 && frees == 1);
  reset(); transfer_status = 1;
  assert(usb_hid_recover((void *)1) == ESP_FAIL && starts == 0 && frees == 1);
  reset(); stop_result = ESP_FAIL;
  assert(usb_hid_recover((void *)1) == ESP_FAIL && clears == 0 && starts == 0 && closes == 1);
  puts("PASS USB recovery: stop, submission, and completion failures never restart input");
  reset(); params_result = ESP_ERR_INVALID_STATE;
  assert(usb_hid_recover((void *)1) == ESP_ERR_INVALID_STATE && opens == 0 && stops == 0);
  reset(); iface = 2;
  assert(usb_hid_recover((void *)1) == ESP_ERR_NOT_FOUND && stops == 0 && clears == 0 && closes == 1);
  puts("PASS USB recovery: disconnected handle and unmatched interface are rejected");
  reset(); stop_result = ESP_ERR_INVALID_STATE;
  assert(usb_hid_recover((void *)1) == ESP_OK && starts == 1 && clears == 1);
  puts("PASS USB recovery: retry after already stopped interface");
  reset(); defer_completion = true;
  assert(usb_hid_recover((void *)1) == ESP_FAIL && starts == 0 && frees == 1 && !pending);
  puts("PASS USB recovery: timeout cancellation drains callback before freeing transfer");
  reset(); defer_completion = true; halt_result = ESP_FAIL;
  assert(usb_hid_recover((void *)1) == ESP_ERR_TIMEOUT);
  assert(ticks > 1100 && ticks <= 1120 && pending && starts == 0);
  assert(allocations == 1 && frees == 0 && opens == 1 && closes == 0);
  assert(halts == 1 && flushes == 0);
  assert(usb_hid_recover((void *)1) == ESP_ERR_INVALID_STATE && opens == 1);
  usb_hid_recovery_poll();
  assert(pending && frees == 0 && closes == 0);
  defer_completion = false;
  usb_hid_recovery_poll();
  assert(!pending && frees == 1 && closes == 1 && starts == 0);
  usb_hid_recovery_poll();
  assert(frees == 1 && closes == 1);
  puts("PASS USB recovery: cancellation failure returns promptly, late callback safely frees/closes exactly once");

  reset(); defer_completion = true; flush_result = ESP_FAIL;
  assert(usb_hid_recover((void *)1) == ESP_ERR_TIMEOUT && pending && frees == 0 && closes == 0);
  assert(halts == 1 && flushes == 1);
  defer_completion = false;
  usb_hid_recovery_poll();
  assert(!pending && frees == 1 && closes == 1);
  puts("PASS USB recovery: flush failure retains live transfer until deferred cleanup");

  reset(); defer_completion = true; defer_after_flush = true;
  assert(usb_hid_recover((void *)1) == ESP_ERR_TIMEOUT && pending && frees == 0 && closes == 0);
  assert(halts == 1 && flushes == 1);
  defer_completion = false;
  usb_hid_recovery_poll();
  assert(!pending && frees == 1 && closes == 1);
  reset(); stop_result = ESP_ERR_INVALID_STATE;
  assert(usb_hid_recover((void *)1) == ESP_OK && starts == 1);
  puts("PASS USB recovery: delayed cancellation callback cleans resources and permits later recovery");
  return 0;
}
