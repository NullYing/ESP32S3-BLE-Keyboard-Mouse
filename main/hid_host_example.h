/*
 * HID Host Example - Header File
 *
 * 提供给其他模块使用的公共接口
 */

#ifndef HID_HOST_EXAMPLE_H__
#define HID_HOST_EXAMPLE_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief 检查BLE是否已连接且安全连接已建立
   *
   * @return true BLE已连接，false BLE未连接
   */
  bool mouse_accumulator_is_ble_connected(void);

  /**
   * @brief 通过BLE发送鼠标报告
   *
   * @param report 鼠标报告数据（6字节）
   * @param length 报告长度（应为6）
   * @return ESP_OK 成功，其他值表示失败
   */
  esp_err_t mouse_accumulator_send_ble_report(const uint8_t *report, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif /* HID_HOST_EXAMPLE_H__ */
