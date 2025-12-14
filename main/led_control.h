/*
 * LED Control Module - Header File
 *
 * 提供LED控制功能，用于显示USB和BLE HID连接状态
 */

#ifndef LED_CONTROL_H__
#define LED_CONTROL_H__

#include <stdbool.h>
#include "esp_err.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C"
{
#endif

// LED配置参数
#define LED_GPIO_PIN 48
#define LED_RMT_RES_HZ (10 * 1000 * 1000)
#define LED_BRIGHTNESS 25

  /**
   * @brief 初始化LED控制模块
   *
   * @return led_strip_handle_t LED条带句柄，如果失败返回NULL
   */
  led_strip_handle_t led_control_init(void);

  /**
   * @brief 根据USB和BLE HID连接状态设置LED颜色
   *
   * @param led_strip LED条带句柄
   * @param usb_keyboard_connected USB键盘是否已连接
   * @param usb_mouse_connected USB鼠标是否已连接
   * @param ble_connected BLE HID是否已连接
   */
  void led_control_set_color(led_strip_handle_t led_strip,
                             bool usb_keyboard_connected,
                             bool usb_mouse_connected,
                             bool ble_connected);

#ifdef __cplusplus
}
#endif

#endif /* LED_CONTROL_H__ */
