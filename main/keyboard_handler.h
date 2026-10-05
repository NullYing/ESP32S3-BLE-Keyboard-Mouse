/*
 * Keyboard Handler - Header
 *
 * 键盘报告处理和热键检测
 */

#ifndef KEYBOARD_HANDLER_H
#define KEYBOARD_HANDLER_H

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the actual USB protocol before starting this interface. */
bool keyboard_handler_configure(hid_host_device_handle_t handle,
                                const uint8_t *descriptor, size_t descriptor_len,
                                bool boot_protocol);
/** Forget descriptor, independent Report ID key state, and hotkey latches. */
void keyboard_handler_clear(hid_host_device_handle_t handle);

/**
 * @brief 检测槽位切换快捷键 (Alt + `)
 *
 * @param[in] kb_report 键盘报告数据
 * @return true 如果检测到组合键
 */
bool check_slot_switch_hotkey(
    const hid_keyboard_input_report_boot_t *kb_report);

/**
 * @brief 检测发现新设备快捷键 (Alt + N)
 *
 * @param[in] kb_report 键盘报告数据
 * @return true 如果检测到组合键
 */
bool check_discover_hotkey(const hid_keyboard_input_report_boot_t *kb_report);

/**
 * @brief USB HID Host Keyboard Interface report callback handler
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] data    Pointer to input report data buffer
 * @param[in] length  Length of input report data buffer
 */
void hid_host_keyboard_report_callback(
    hid_host_device_handle_t hid_device_handle, uint8_t *data, int length);

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_HANDLER_H
