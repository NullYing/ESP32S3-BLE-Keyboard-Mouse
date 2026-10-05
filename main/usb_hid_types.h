/*
 * USB HID Types - Shared Type Definitions
 *
 * 集中定义 USB HID 相关的共享类型，避免多文件重复定义
 */

#ifndef USB_HID_TYPES_H
#define USB_HID_TYPES_H

#include "usb/hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief APP event group
 *
 * Application logic can be different. There is a one among other ways to
 * distinguish the event by application event group. In this example we have two
 * event groups: APP_EVENT - General event, APP_EVENT_HID_HOST - HID Host Driver
 * event, such as device connection/disconnection or input report.
 */
typedef enum { APP_EVENT = 0, APP_EVENT_HID_HOST, APP_EVENT_HID_RECOVER } app_event_group_t;

/**
 * @brief APP event queue
 *
 * This event is used for delivering the HID Host event from callback to a task.
 */
typedef struct {
  app_event_group_t event_group;
  /* HID Host - Device related info */
  struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
  } hid_host_device;
} app_event_queue_t;

/**
 * @brief USB HID设备管理（支持同时连接键盘和鼠标）
 */
typedef struct {
  hid_host_device_handle_t keyboard_handle;
  hid_host_device_handle_t mouse_handle;
} usb_hid_devices_t;

#ifdef __cplusplus
}
#endif

#endif // USB_HID_TYPES_H
