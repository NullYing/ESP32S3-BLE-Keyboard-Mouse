/*
 * USB HID Host - Header
 *
 * USB HID Host 管理模块
 */

#ifndef USB_HID_HOST_H
#define USB_HID_HOST_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "usb/hid_host.h"
#include "usb_hid_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取 USB HID 设备句柄
 *
 * @return 指向 usb_hid_devices_t 结构的指针
 */
usb_hid_devices_t *usb_hid_get_devices(void);

/**
 * @brief 获取应用事件队列
 *
 * @return 事件队列句柄
 */
QueueHandle_t usb_hid_get_event_queue(void);

/**
 * @brief USB 库任务
 *
 * 处理 USB 主机库事件，需要作为 FreeRTOS 任务运行
 *
 * @param arg 未使用
 */
void usb_lib_task(void *arg);

/**
 * @brief USB HID Host 设备回调
 *
 * 将新的 HID 设备事件放入队列
 */
void usb_hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg);

/**
 * @brief USB HID Host 设备事件处理
 *
 * 处理设备连接、断开等事件
 */
void usb_hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                               const hid_host_driver_event_t event, void *arg);

/**
 * @brief USB HID Host 接口回调
 *
 * 处理 HID 接口事件（输入报告等）
 */
void usb_hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_interface_event_t event,
                                     void *arg);

/**
 * @brief 打印 USB 设备信息
 */
void print_usb_device_info(hid_host_device_handle_t hid_device_handle);

#ifdef __cplusplus
}
#endif

#endif // USB_HID_HOST_H
