#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_KEYBOARD_MAX_FIELDS 64
#define USB_KEYBOARD_MAX_REPORTS 16

typedef struct {
  uint32_t bit_offset;
  uint16_t usage_min, usage_max, usage_page;
  int32_t logical_min;
  uint16_t count;
  uint8_t size, report_index;
  bool variable;
} usb_keyboard_field_t;

typedef struct {
  uint32_t input_bits;
  uint8_t id;
  bool keyboard;
  uint8_t pressed[32];
} usb_keyboard_input_t;

typedef struct {
  usb_keyboard_field_t fields[USB_KEYBOARD_MAX_FIELDS];
  usb_keyboard_input_t reports[USB_KEYBOARD_MAX_REPORTS];
  uint8_t field_count, report_count;
  bool has_report_ids;
} usb_keyboard_decoder_t;

// Configure only Report Protocol inputs. Boot inputs must bypass this decoder.
bool usb_keyboard_decoder_init(usb_keyboard_decoder_t *decoder,
                               const uint8_t *descriptor, size_t length);
// Decode key arrays and NKRO Variable fields, preserving independent Report IDs.
// More than six keys produces HID ErrorRollOver (01) in all six key slots.
bool usb_keyboard_decode(usb_keyboard_decoder_t *decoder, const uint8_t *data,
                         size_t length, uint8_t boot_report[8]);
