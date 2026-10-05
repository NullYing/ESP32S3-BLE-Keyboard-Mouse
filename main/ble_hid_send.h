/*
 * BLE HID Send Manager
 *
 * 提供线程安全的 BLE HID 报告发送功能，解决并发问题
 */

#ifndef BLE_HID_SEND_H
#define BLE_HID_SEND_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE HID 发送管理器
 *
 * 创建互斥锁和初始化状态，必须在使用其他函数前调用
 *
 * @return ESP_OK 成功
 * @return ESP_ERR_NO_MEM 内存不足，创建互斥锁失败
 */
esp_err_t ble_hid_send_init(void);

/**
 * @brief 启用或禁用 BLE HID 发送
 *
 * 在 BLE 连接建立后调用 enable=true，断开时调用 enable=false
 * 此函数线程安全
 *
 * @param enable true=启用发送, false=禁用发送
 */
void ble_hid_send_enable(bool enable);

/**
 * @brief 检查 BLE HID 是否可以发送
 *
 * 综合检查：
 * - 发送是否已启用
 * - 是否正在进行设备切换
 * - 是否正在发现新设备
 *
 * @return true 可以发送
 * @return false 不可发送
 */
bool ble_hid_send_is_ready(void);

/**
 * @brief 发送鼠标报告（线程安全）
 *
 * 使用互斥锁保护发送操作，确保原子性
 *
 * @param report 报告数据指针
 * @param length 报告长度
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_STATE 未初始化或发送被禁用
 * @return ESP_ERR_TIMEOUT 获取互斥锁超时
 * @return 其他 底层BLE发送错误
 */
esp_err_t ble_hid_send_mouse_report(const uint8_t *report, uint8_t length);

/** Reject mouse batches captured before the current authenticated session. */
esp_err_t ble_hid_send_mouse_report_for_session(const uint8_t *report,
                                                uint8_t length,
                                                uint32_t generation);

/**
 * @brief 发送键盘报告（线程安全）
 *
 * @param report 报告数据指针
 * @param length 报告长度
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_STATE 未初始化或发送被禁用
 * @return ESP_ERR_TIMEOUT 获取互斥锁超时
 * @return 其他 底层BLE发送错误
 */
esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, uint8_t length);

/**
 * @brief 发送 Consumer Control 报告（线程安全）
 *
 * @param report 报告数据指针
 * @param length 报告长度
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_STATE 未初始化或发送被禁用
 * @return ESP_ERR_TIMEOUT 获取互斥锁超时
 * @return 其他 底层BLE发送错误
 */
esp_err_t ble_hid_send_cc_report(const uint8_t *report, uint8_t length);

/**
 * @brief 获取发送状态（用于调试/日志）
 *
 * @return true 发送已启用
 * @return false 发送已禁用
 */
bool ble_hid_send_get_status(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_HID_SEND_H
