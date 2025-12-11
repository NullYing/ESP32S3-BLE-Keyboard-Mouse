// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText:  2022 Istvan Pasztor
// C language version of HID report parser
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// HID usage pages
#define PAGE_GENERIC_DESKTOP 0x01
#define PAGE_BUTTON 0x09
#define PAGE_CONSUMER 0x0C

// Generic Desktop Page usages
#define USAGE_MOUSE 0x02
#define USAGE_X 0x30
#define USAGE_Y 0x31
#define USAGE_WHEEL 0x38

// Consumer Page usages
#define USAGE_CONSUMER_AC_PAN 0x0238

// Collection types
#define COLLECTION_TYPE_APPLICATION 0x01

  // HID report layout structure for mouse
  typedef struct
  {
    uint8_t report_id;         // Report ID (0 means no report ID)
    uint32_t report_size_bits; // Total report size in bits

    // Button fields
    uint32_t buttons_count;      // Number of button bits
    uint32_t buttons_bit_offset; // Bit offset of buttons field

    // Axis fields
    uint32_t x_bit_offset; // Bit offset of X axis
    uint32_t x_size;       // Size in bits of X axis

    uint32_t y_bit_offset; // Bit offset of Y axis
    uint32_t y_size;       // Size in bits of Y axis

    uint32_t wheel_bit_offset; // Bit offset of wheel/vertical scroll
    uint32_t wheel_size;       // Size in bits of wheel

    uint32_t pan_bit_offset; // Bit offset of horizontal scroll/pan
    uint32_t pan_size;       // Size in bits of pan
  } hid_report_layout_t;

  /**
   * @brief Parse HID report descriptor and extract multiple mouse layouts
   *
   * @param descriptor Pointer to HID report descriptor
   * @param descriptor_size Size of descriptor in bytes
   * @param layouts Output array to store parsed layouts
   * @param max_layouts Maximum number of layouts to parse
   * @return Number of layouts found (0 on error)
   */
  int parse_hid_report_descriptor_layouts(const void *descriptor, size_t descriptor_size,
                                          hid_report_layout_t *layouts, int max_layouts);

  /**
   * @brief Parse HID report descriptor and extract a single mouse layout
   *
   * @param descriptor Pointer to HID report descriptor
   * @param descriptor_size Size of descriptor in bytes
   * @param layout Output structure to store parsed layout
   * @return 0 on success, negative on error
   */
  int parse_hid_report_descriptor_layout(const void *descriptor, size_t descriptor_size,
                                         hid_report_layout_t *layout);

#ifdef __cplusplus
}
#endif
