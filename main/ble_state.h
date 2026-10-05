/*
 * BLE State Manager - Header
 *
 * 封装 BLE 连接相关的全局状态，提供线程安全的访问接口
 * 替代原来散落在各模块中的 extern 全局变量访问
 */

#ifndef BLE_STATE_H
#define BLE_STATE_H

#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================================================
   初始化
   =================================================================================================
 */

/**
 * @brief 初始化 BLE 状态管理器
 *
 * 必须在使用其他函数之前调用
 *
 * @return ESP_OK 成功
 */
esp_err_t ble_state_init(void);

/* =================================================================================================
   连接状态管理
   =================================================================================================
 */

/**
 * @brief 设置 BLE 连接 ID
 *
 * @param conn_id 连接 ID，0 表示未连接
 */
void ble_state_set_conn_id(uint16_t conn_id);

/**
 * @brief 获取当前 BLE 连接 ID
 *
 * @return 连接 ID，0 表示未连接
 */
uint16_t ble_state_get_conn_id(void);

/**
 * @brief 设置安全连接状态
 *
 * @param connected true 表示安全连接已建立
 */
void ble_state_set_sec_conn(bool connected);

/**
 * @brief 获取安全连接状态
 *
 * @return true 安全连接已建立
 */
bool ble_state_get_sec_conn(void);

/**
 * @brief 检查 BLE 是否已连接且可以发送数据
 *
 * 综合检查连接状态和安全连接状态
 *
 * @return true 可以发送数据
 */
bool ble_state_is_connected(void);

/* =================================================================================================
   兼容性接口（用于逐步迁移）
   =================================================================================================
 */

/**
 * @brief 获取 conn_id 的指针（用于兼容旧代码）
 *
 * @deprecated 请使用 ble_state_get_conn_id() 替代
 * @return 指向 conn_id 的指针
 */
uint16_t *ble_state_get_conn_id_ptr(void);

/**
 * @brief 获取 sec_conn 的指针（用于兼容旧代码）
 *
 * @deprecated 请使用 ble_state_get_sec_conn() 替代
 * @return 指向 sec_conn 的指针
 */
bool *ble_state_get_sec_conn_ptr(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_STATE_H
