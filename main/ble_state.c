/*
 * BLE State Manager - Implementation
 *
 * 封装 BLE 连接相关的全局状态，提供线程安全的访问接口
 */

#include "ble_state.h"
#include "esp_log.h"

static const char *TAG = "BLE_STATE";

/* =================================================================================================
   内部状态变量
   =================================================================================================
 */

// BLE 连接 ID
static uint16_t s_ble_conn_id = 0;

// 安全连接状态
static bool s_sec_conn = false;

// 初始化标志
static bool s_initialized = false;

/* =================================================================================================
   初始化
   =================================================================================================
 */

esp_err_t ble_state_init(void) {
  if (s_initialized) {
    ESP_LOGD(TAG, "BLE 状态管理器已初始化");
    return ESP_OK;
  }

  s_ble_conn_id = 0;
  s_sec_conn = false;
  s_initialized = true;

  ESP_LOGI(TAG, "BLE 状态管理器已初始化");
  return ESP_OK;
}

/* =================================================================================================
   连接状态管理
   =================================================================================================
 */

void ble_state_set_conn_id(uint16_t conn_id) {
  s_ble_conn_id = conn_id;
  ESP_LOGD(TAG, "BLE 连接 ID 已设置: %d", conn_id);
}

uint16_t ble_state_get_conn_id(void) { return s_ble_conn_id; }

void ble_state_set_sec_conn(bool connected) {
  s_sec_conn = connected;
  ESP_LOGD(TAG, "安全连接状态: %s", connected ? "已连接" : "已断开");
}

bool ble_state_get_sec_conn(void) { return s_sec_conn; }

bool ble_state_is_connected(void) { return s_ble_conn_id != 0 && s_sec_conn; }

/* =================================================================================================
   兼容性接口
   =================================================================================================
 */

uint16_t *ble_state_get_conn_id_ptr(void) { return &s_ble_conn_id; }

bool *ble_state_get_sec_conn_ptr(void) { return &s_sec_conn; }
