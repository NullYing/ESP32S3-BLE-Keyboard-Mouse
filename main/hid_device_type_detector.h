/*
 * SPDX-License-Identifier: MIT
 * HID Device Type Detector
 *
 * This module provides functions to detect HID device types (keyboard/mouse)
 * by parsing HID Report Descriptors, which is more reliable than just checking
 * the protocol field.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "usb/hid_host.h"
#include "hid_report_parser_c.h"

/**
 * @brief Check device type from HID Report Descriptor
 *
 * This function parses the HID Report Descriptor to determine if the device
 * is actually a keyboard or mouse, which is more reliable than just checking
 * the protocol field.
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[out] is_keyboard  Set to true if device is a keyboard
 * @param[out] is_mouse     Set to true if device is a mouse
 * @return true if descriptor was successfully parsed, false otherwise
 */
bool hid_device_type_detect(hid_host_device_handle_t hid_device_handle, bool *is_keyboard, bool *is_mouse);
