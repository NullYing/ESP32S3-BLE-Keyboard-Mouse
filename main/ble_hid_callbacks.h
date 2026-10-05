/*
 * BLE HID Callbacks - Header
 *
 * BLE HID 事件回调函数
 */

#ifndef BLE_HID_CALLBACKS_H
#define BLE_HID_CALLBACKS_H

#include "esp_gap_ble_api.h"
#include "esp_hidd_prf_api.h"
#include "led_strip.h"
#include "usb_hid_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE HID 回调模块
 *
 * @param led LED条带句柄
 * @param usb_devices USB HID设备句柄指针
 * @param adv_data 广播数据指针
 * @param adv_params 广播参数指针
 */
void ble_hid_callbacks_init(led_strip_handle_t led,
                            usb_hid_devices_t *usb_devices,
                            esp_ble_adv_data_t *adv_data,
                            esp_ble_adv_params_t *adv_params);

/**
 * @brief BLE HID 事件回调
 *
 * 处理 HID 设备事件（注册完成、连接、断开、报告写入等）
 */
void ble_hid_event_callback(esp_hidd_cb_event_t event,
                            esp_hidd_cb_param_t *param);

/**
 * @brief GAP 事件处理器
 *
 * 处理 BLE GAP 事件（广播、安全、认证、连接参数等）
 */
void gap_event_handler(esp_gap_ble_cb_event_t event,
                       esp_ble_gap_cb_param_t *param);

/**
 * @brief 更新 LED 颜色回调
 *
 * 设置回调函数，用于在连接状态变化时更新LED
 *
 * @param callback LED更新回调函数
 */
typedef void (*led_update_callback_t)(void);
void ble_hid_callbacks_set_led_callback(led_update_callback_t callback);

/** Request advertising once when no link is present; no delays in callbacks. */
void ble_hid_callbacks_request_advertising(void);

/** Blink feedback from a separate task so HID callbacks keep running. */
void ble_hid_callbacks_show_switching(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_HID_CALLBACKS_H
