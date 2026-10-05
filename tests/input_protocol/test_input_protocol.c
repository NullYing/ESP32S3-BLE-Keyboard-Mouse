#include "hid_report_parser_c.h"
#include "usb_keyboard_report.h"
#include "keyboard_handler.h"
#include "ble_hid_send.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int switches, discoveries, sends;
static bool ready;
static uint8_t sent[8];
bool ble_hid_send_is_ready(void) { return ready; }
esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, uint8_t length) {
  assert(length == 8); memcpy(sent, report, 8); sends++; return ESP_OK;
}
esp_err_t ble_device_manager_switch_slot(void) { switches++; return ESP_OK; }
esp_err_t ble_device_manager_discover_new(void) { discoveries++; return ESP_OK; }

static const uint8_t keyboard[] = {
  0x05,1,0x09,6,0xa1,1,0x85,5,
  0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
  0x75,8,0x95,1,0x81,1,
  0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0
};
static const uint8_t nkro[] = {
  0x05,1,0x09,6,0xa1,1,0x85,3,
  0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
  0x19,4,0x29,0x3b,0x75,1,0x95,56,0x81,2,0xc0
};

static void test_mouse(void) {
  const uint8_t desc[] = {
    0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,
    0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
    0x75,5,0x95,1,0x81,1,
    0x05,1,0x19,0x30,0x29,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6,
    0x09,0x38,0x95,1,0x81,6,0xc0,0xc0
  };
  hid_report_layout_t layouts[16];
  assert(parse_hid_report_descriptor_layouts(desc, sizeof(desc), layouts, 16) == 1);
  assert(layouts[0].x_size == 8 && layouts[0].y_size == 8);
  assert(layouts[0].x_bit_offset == 8 && layouts[0].y_bit_offset == 16);
  uint8_t data[] = {1,10,20,1};
  hid_decoded_mouse_report_t out;
  assert(hid_decode_mouse_report(layouts, 1, data, 4, &out));
  assert(out.has_buttons && out.buttons == 1 && out.x == 10 && out.y == 20 && out.wheel == 1);
  data[1] = 0x80; data[2] = 0xff;
  assert(hid_decode_mouse_report(layouts, 1, data, 4, &out) && out.x == -128 && out.y == -1);
  assert(!hid_decode_mouse_report(layouts, 1, data, 3, &out));
  layouts[0].report_id = 5;
  uint8_t numbered[] = {5,1,10,20,1};
  assert(hid_decode_mouse_report(layouts, 1, numbered, 5, &out) && out.buttons == 1 && out.x == 10);
  assert(!hid_decode_mouse_report(layouts, 1, numbered, 4, &out));
  numbered[0] = 6;
  assert(!hid_decode_mouse_report(layouts, 1, numbered, 5, &out));
  layouts[0].buttons_count = 0;
  numbered[0] = 5;
  assert(hid_decode_mouse_report(layouts, 1, numbered, 5, &out) && !out.has_buttons);
  // Parse a numbered descriptor through the actual parser, not just a synthetic layout.
  uint8_t with_id[sizeof(desc) + 2];
  memcpy(with_id, desc, 6);
  with_id[6] = 0x85; with_id[7] = 5;
  memcpy(with_id + 8, desc + 6, sizeof(desc) - 6);
  assert(parse_hid_report_descriptor_layouts(with_id, sizeof(with_id), layouts, 1) == 1);
  assert(layouts[0].report_id == 5 && layouts[0].report_size_bits == 32);
  assert(hid_decode_mouse_report(layouts, 1, numbered, 5, &out) && out.x == 10);
  with_id[sizeof(with_id) - 1] = 0x75; // Truncated final item must reject partial layouts.
  assert(parse_hid_report_descriptor_layouts(with_id, sizeof(with_id), layouts, 1) == 0);
  const uint8_t repeated_id[] = {
    0x05,1,0x09,2,0xa1,1,0x85,1,0x09,0x30,
    0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6,
    0x85,2,0x09,0x31,0x81,6,
    0x85,1,0x09,0x38,0x81,6,0xc0
  };
  assert(parse_hid_report_descriptor_layouts(repeated_id, sizeof(repeated_id), layouts, 16) == 2);
  assert(layouts[0].report_id == 1 && layouts[0].report_size_bits == 16 && layouts[0].wheel_bit_offset == 8);
  uint8_t separate_wheel[] = {1,10,1};
  assert(hid_decode_mouse_report(layouts, 2, separate_wheel, sizeof(separate_wheel), &out));
  assert(!out.has_buttons && out.x == 10 && out.wheel == 1);
  puts("PASS mouse: four-byte no ID, Usage Range, signed axes, ID/size validation, optional buttons");
}

static void test_keyboard(void) {
  usb_keyboard_decoder_t d;
  uint8_t out[8];
  assert(usb_keyboard_decoder_init(&d, keyboard, sizeof(keyboard)));
  uint8_t input[9] = {5,0,0,4,0,0,0,0,0};
  assert(usb_keyboard_decode(&d, input, 9, out));
  assert(out[0] == 0 && out[2] == 4 && out[7] == 0);
  assert(!usb_keyboard_decode(&d, input, 8, out));
  input[0] = 6;
  assert(!usb_keyboard_decode(&d, input, 9, out));
  assert(usb_keyboard_decoder_init(&d, nkro, sizeof(nkro)));
  uint8_t bits[9] = {3,4,1}; // Alt + A
  assert(usb_keyboard_decode(&d, bits, 9, out) && out[0] == 4 && out[2] == 4);
  bits[2] = 0x7f; // 7 simultaneous keys => Boot rollover, never truncated keys.
  assert(usb_keyboard_decode(&d, bits, 9, out));
  for (int i = 2; i < 8; i++) assert(out[i] == 1);
  const uint8_t short_item[] = {0x75};
  assert(!usb_keyboard_decoder_init(&d, short_item, sizeof(short_item)));
  uint8_t no_id[sizeof(keyboard) - 2];
  memcpy(no_id, keyboard, 6);
  memcpy(no_id + 6, keyboard + 8, sizeof(keyboard) - 8);
  assert(usb_keyboard_decoder_init(&d, no_id, sizeof(no_id)));
  uint8_t plain[] = {4,0,4,0,0,0,0,0};
  assert(usb_keyboard_decode(&d, plain, sizeof(plain), out) && out[0] == 4 && out[2] == 4);
  puts("PASS keyboard: Report ID, NKRO, rollover, unknown ID, truncated descriptor/report");
}

static void test_multi_reports(void) {
  const uint8_t desc[] = {
    0x05,1,0x09,6,0xa1,1,0x85,1,0x05,7,0x19,0xe0,0x29,0xe7,
    0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x85,2,0x19,4,0x29,11,0x81,2,0xc0
  };
  usb_keyboard_decoder_t d;
  uint8_t out[8], a[] = {1,4}, b[] = {2,1};
  assert(usb_keyboard_decoder_init(&d, desc, sizeof(desc)));
  assert(usb_keyboard_decode(&d, a, sizeof(a), out) && out[0] == 4);
  assert(usb_keyboard_decode(&d, b, sizeof(b), out) && out[0] == 4 && out[2] == 4);
  a[1] = 0;
  assert(usb_keyboard_decode(&d, a, sizeof(a), out) && out[0] == 0 && out[2] == 4);
  b[1] = 0;
  assert(usb_keyboard_decode(&d, b, sizeof(b), out) && out[2] == 0);
  puts("PASS keyboard: independently updated numbered reports preserve other held keys");
}

static void test_hotkeys(void) {
  void *handle = (void *)1;
  ready = false;
  assert(keyboard_handler_configure(handle, keyboard, sizeof(keyboard), false));
  uint8_t input[9] = {5,4,0,0x35};
  hid_host_keyboard_report_callback(handle, input, 9);
  hid_host_keyboard_report_callback(handle, input, 9);
  input[4] = 4; // Extra key while holding same chord must not retrigger.
  hid_host_keyboard_report_callback(handle, input, 9);
  assert(switches == 1 && sends == 0);
  memset(input + 1, 0, 8);
  hid_host_keyboard_report_callback(handle, input, 9);
  input[1] = 4; input[3] = 0x35;
  hid_host_keyboard_report_callback(handle, input, 9);
  assert(switches == 2);
  input[3] = 0x11;
  hid_host_keyboard_report_callback(handle, input, 9);
  hid_host_keyboard_report_callback(handle, input, 9);
  assert(discoveries == 1);
  keyboard_handler_clear(handle);
  hid_host_keyboard_report_callback(handle, input, 9);
  assert(discoveries == 1);
  assert(keyboard_handler_configure(handle, NULL, 0, true));
  ready = true;
  uint8_t boot[8] = {0,0,4};
  hid_host_keyboard_report_callback(handle, boot, 8);
  assert(sends == 1 && sent[0] == 0 && sent[2] == 4);
  keyboard_handler_clear(handle);
  assert(keyboard_handler_configure(handle, nkro, sizeof(nkro), false));
  uint8_t bits[9] = {3,4};
  bits[2 + (0x35 - 4) / 8] = 1U << ((0x35 - 4) % 8);
  hid_host_keyboard_report_callback(handle, bits, 9);
  hid_host_keyboard_report_callback(handle, bits, 9);
  assert(switches == 3 && sends == 2); // Single empty release on hotkey edge.
  puts("PASS hotkeys: offline edge, held reports, rearm, clear, Boot and NKRO input");
}

int main(void) { test_mouse(); test_keyboard(); test_multi_reports(); test_hotkeys(); return 0; }
