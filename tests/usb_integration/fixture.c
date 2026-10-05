static hid_host_dev_params_t params;
static const uint8_t *descriptor;
static size_t descriptor_size;
static uint8_t raw[64], sent[8];
static size_t raw_size;
static int opens, closes, starts, queue_sends, queue_result;
static int report_protocol_calls, boot_protocol_calls, report_protocol_result, start_result;
static int recovery_calls, recovery_result, mouse_adds, mouse_clears;
static bool disconnect_during_recovery, reerror_during_recovery;
static int16_t mouse_x, mouse_y;
static int8_t mouse_wheel;
static uint8_t mouse_buttons;
static unsigned ticks;
static const uint8_t keyboard_desc[] = {
  0x05,1,0x09,6,0xa1,1,0x85,5,0x05,7,0x19,0xe0,0x29,0xe7,
  0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
  0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,
  0x75,8,0x95,6,0x81,0,0xc0
};
static const uint8_t mouse_desc[] = {
  0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,
  0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
  0x75,5,0x95,1,0x81,1,0x05,1,0x19,0x30,0x29,0x31,
  0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6,0x09,0x38,0x95,1,0x81,6,0xc0,0xc0
};
TickType_t xTaskGetTickCount(void) { return ticks; }
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout) {
  (void)item; assert(queue == app_event_queue && timeout == 0); queue_sends++; return queue_result;
}
void update_led_color(void) {}
void mouse_accumulator_add(int16_t x, int16_t y, int8_t wheel, uint8_t buttons) {
  mouse_adds++; mouse_x = x; mouse_y = y; mouse_wheel = wheel; mouse_buttons = buttons;
}
void mouse_accumulator_clear(void) { mouse_clears++; }
bool ble_hid_send_is_ready(void) { return true; }
esp_err_t ble_hid_send_keyboard_report(const uint8_t *data, uint8_t length) {
  assert(length == 8); memcpy(sent, data, length); return ESP_OK;
}
esp_err_t ble_device_manager_switch_slot(void) { return ESP_OK; }
esp_err_t ble_device_manager_discover_new(void) { return ESP_OK; }
void usb_hid_recovery_poll(void) {}
esp_err_t usb_hid_recover(hid_host_device_handle_t handle) {
  assert(handle == (void *)1); recovery_calls++;
  if (disconnect_during_recovery) {
    disconnect_during_recovery = false;
    usb_hid_host_interface_callback(handle, HID_HOST_INTERFACE_EVENT_DISCONNECTED, NULL);
    assert(closes == 0); // Recovery still owns the interface record on this task.
  }
  if (reerror_during_recovery) {
    reerror_during_recovery = false;
    usb_hid_host_interface_callback(handle, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  }
  return recovery_result;
}
esp_err_t hid_host_device_get_params(hid_host_device_handle_t handle, hid_host_dev_params_t *out) {
  assert(handle == (void *)1); *out = params; return ESP_OK;
}
esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t handle,
                                                  uint8_t *out, size_t size, size_t *length) {
  assert(handle == (void *)1 && size >= raw_size); memcpy(out, raw, raw_size); *length = raw_size; return ESP_OK;
}
const uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t handle, size_t *size) {
  assert(handle == (void *)1); *size = descriptor_size; return descriptor;
}
esp_err_t hid_host_device_open(hid_host_device_handle_t handle, const hid_host_device_config_t *config) {
  assert(handle == (void *)1 && config->callback == usb_hid_host_interface_callback); opens++; return ESP_OK;
}
esp_err_t hid_host_device_close(hid_host_device_handle_t handle) { assert(handle == (void *)1); closes++; return ESP_OK; }
esp_err_t hid_host_device_start(hid_host_device_handle_t handle) { assert(handle == (void *)1); starts++; return start_result; }
esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t handle, int protocol) {
  assert(handle == (void *)1);
  if (protocol == HID_REPORT_PROTOCOL_REPORT) { report_protocol_calls++; return report_protocol_result; }
  assert(protocol == HID_REPORT_PROTOCOL_BOOT); boot_protocol_calls++; return ESP_OK;
}
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t handle, int rate, int id) {
  (void)handle; (void)rate; (void)id; return ESP_FAIL; // Optional SET_IDLE rejection must not abort startup.
}
static void reset(void) {
  keyboard_handler_clear((void *)1);
  memset(&usb_hid_devices, 0, sizeof(usb_hid_devices));
  memset(recovery_handles, 0, sizeof(recovery_handles));
  memset(deferred_close_handles, 0, sizeof(deferred_close_handles));
  memset(recovery_generation, 0, sizeof(recovery_generation));
  memset(recovery_retry_at, 0, sizeof(recovery_retry_at));
  g_mouse_layout_count = 0; g_mouse_boot_protocol = false; g_mouse_buttons = 0;
  opens = closes = starts = queue_sends = report_protocol_calls = boot_protocol_calls = 0;
  recovery_calls = mouse_adds = mouse_clears = 0;
  disconnect_during_recovery = reerror_during_recovery = false;
  queue_result = 1; report_protocol_result = recovery_result = start_result = ESP_OK; ticks = 0;
  memset(raw, 0, sizeof(raw)); memset(sent, 0, sizeof(sent));
  raw_size = 0; descriptor = NULL; descriptor_size = 0;
  params = (hid_host_dev_params_t){.addr = 1, .iface_num = 0, .sub_class = HID_SUBCLASS_BOOT_INTERFACE};
}
static void register_device(bool keyboard) {
  descriptor = keyboard ? keyboard_desc : mouse_desc;
  descriptor_size = keyboard ? sizeof(keyboard_desc) : sizeof(mouse_desc);
  params.proto = keyboard ? HID_PROTOCOL_KEYBOARD : HID_PROTOCOL_MOUSE;
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 1 && report_protocol_calls == 1 && boot_protocol_calls == 0);
}
static void dispatch(void) {
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_INPUT_REPORT, NULL);
}
int main(void) {
  reset(); register_device(false);
  memcpy(raw, (uint8_t[]){1,10,20,1}, 4); raw_size = 4; dispatch();
  assert(mouse_adds == 1 && mouse_buttons == 1 && mouse_x == 10 && mouse_y == 20 && mouse_wheel == 1);
  raw_size = 3; dispatch(); assert(mouse_adds == 1);
  puts("PASS application USB: registered four-byte mouse dispatch uses descriptor, truncated packet rejected");

  reset(); register_device(true);
  memcpy(raw, (uint8_t[]){5,0,0,4,0,0,0,0,0}, 9); raw_size = 9; dispatch();
  assert(sent[0] == 0 && sent[2] == 4 && sent[7] == 0);
  puts("PASS application USB: Report keyboard configure -> USB dispatch -> actual decoder -> BLE Boot report");

  reset(); params.proto = HID_PROTOCOL_KEYBOARD;
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 1 && report_protocol_calls == 1 && boot_protocol_calls == 1);
  memcpy(raw, (uint8_t[]){0,0,4,0,0,0,0,0}, 8); raw_size = 8; dispatch(); assert(sent[2] == 4);
  reset(); params.proto = HID_PROTOCOL_MOUSE; report_protocol_result = ESP_FAIL;
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 1 && g_mouse_boot_protocol && boot_protocol_calls == 1);
  memcpy(raw, (uint8_t[]){1,10,20}, 3); raw_size = 3; dispatch();
  assert(mouse_adds == 1 && mouse_buttons == 1 && mouse_x == 10 && mouse_y == 20 && mouse_wheel == 0);
  puts("PASS application USB: explicit Boot fallback sets protocol before parsing keyboard/mouse");

  reset(); params.proto = HID_PROTOCOL_NONE; params.sub_class = 0;
  descriptor = keyboard_desc; descriptor_size = sizeof(keyboard_desc);
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 1 && usb_hid_devices.keyboard_handle == (void *)1 && report_protocol_calls == 0);
  puts("PASS application USB: protocol NONE keyboard registers by actual descriptor detection");

  reset(); register_device(false); queue_result = 0;
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  assert(usb_hid_devices.mouse_handle == (void *)1 && recovery_calls == 0 && recovery_handles[0] == (void *)1 && queue_sends == 1);
  recover_pending_interfaces();
  assert(recovery_calls == 1 && !recovery_handles[0] && usb_hid_devices.mouse_handle == (void *)1);
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  recovery_result = ESP_FAIL; recover_pending_interfaces(); assert(recovery_calls == 2);
  recover_pending_interfaces(); assert(recovery_calls == 2);
  ticks = 250; recovery_result = ESP_OK; recover_pending_interfaces(); assert(recovery_calls == 3 && !recovery_handles[0]);
  puts("PASS application USB: full queue preserves recovery, callback keeps handle, app task recovers and retries");

  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_DISCONNECTED, NULL);
  assert(!usb_hid_devices.mouse_handle && !recovery_handles[0] && closes == 0);
  close_disconnected_interfaces(); assert(closes == 1);
  close_disconnected_interfaces(); assert(closes == 1);
  assert(mouse_buttons == 0 && mouse_x == 0 && mouse_y == 0 && mouse_wheel == 0 && mouse_clears == 1);
  recover_pending_interfaces(); assert(recovery_calls == 3);
  puts("PASS application USB: disconnect cancels recovery, performs second close and queues mouse button release");
  reset(); register_device(true);
  memset(sent, 0xff, sizeof(sent));
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_DISCONNECTED, NULL);
  assert(!usb_hid_devices.keyboard_handle && closes == 0);
  close_disconnected_interfaces(); assert(closes == 1);
  for (int i = 0; i < 8; i++) assert(sent[i] == 0);
  puts("PASS application USB: keyboard disconnect clears parser state and releases held keys");
  reset(); params.proto = HID_PROTOCOL_KEYBOARD;
  descriptor = keyboard_desc; descriptor_size = sizeof(keyboard_desc); start_result = ESP_FAIL;
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 1 && !usb_hid_devices.keyboard_handle && closes == 1);
  memcpy(raw, (uint8_t[]){5,0,0,4,0,0,0,0,0}, 9); raw_size = 9;
  hid_host_keyboard_report_callback((void *)1, raw, raw_size); assert(sent[2] == 0);
  start_result = ESP_OK;
  usb_hid_host_device_event((void *)1, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
  assert(starts == 2 && usb_hid_devices.keyboard_handle == (void *)1);
  puts("PASS application USB: failed start clears registered handle/parser and allows subsequent registration");
  reset(); register_device(false);
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  disconnect_during_recovery = true;
  recover_pending_interfaces();
  assert(recovery_calls == 1 && !recovery_handles[0] && !usb_hid_devices.mouse_handle && closes == 0);
  close_disconnected_interfaces(); assert(closes == 1);
  reset(); register_device(false);
  usb_hid_host_interface_callback((void *)1, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, NULL);
  reerror_during_recovery = true; recover_pending_interfaces();
  assert(recovery_calls == 1 && recovery_handles[0] == (void *)1);
  recover_pending_interfaces(); assert(recovery_calls == 2 && !recovery_handles[0]);
  puts("PASS application USB: disconnect during recovery delays close; newer recovery generation survives old result");
  return 0;
}
