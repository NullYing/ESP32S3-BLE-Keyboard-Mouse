/*
 * BLE HID Send Manager - Implementation
 *
 * 使用 FreeRTOS 互斥锁保护 BLE HID 报告发送操作
 * 解决多任务并发访问导致的发送失败问题
 *
 * 改进版：将切换状态也纳入互斥锁保护，实现原子性状态管理
 */

#include "ble_hid_send.h"
#include "ble_device_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include "hid_dev.h"
#include "hidd_le_prf_int.h"
#include "mouse_accumulator.h"

static const char *TAG = "BLE_SEND";

/* =================================================================================================
   内部变量
   =================================================================================================
 */

// 发送互斥锁（保护所有状态和发送操作）
static SemaphoreHandle_t s_send_mutex = NULL;

// 发送使能状态
static bool s_send_enabled = false;
static portMUX_TYPE s_state_spinlock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_send_conn_id;

// 外部变量
extern uint16_t ble_hid_conn_id;
extern hidd_le_env_t hidd_le_env;

// 报告长度定义
#define HID_KEYBOARD_IN_RPT_LEN 8
#define HID_CC_IN_RPT_LEN 2

/* =================================================================================================
   公共API实现
   =================================================================================================
 */

esp_err_t ble_hid_send_init(void) {
  if (s_send_mutex != NULL) {
    ESP_LOGD(TAG, "BLE HID 发送管理器已初始化");
    return ESP_OK; // 已初始化
  }

  s_send_mutex = xSemaphoreCreateMutex();
  if (s_send_mutex == NULL) {
    ESP_LOGE(TAG, "创建发送互斥锁失败");
    return ESP_ERR_NO_MEM;
  }

  s_send_enabled = false;
  ESP_LOGI(TAG, "BLE HID 发送管理器已初始化");
  return ESP_OK;
}

void ble_hid_send_enable(bool enable) {
  if (s_send_mutex == NULL) {
    return;
  }
  /* Serialize session changes with an in-flight send. Readiness snapshots
   * never take this mutex, so transient contention is not a disconnection. */
  xSemaphoreTake(s_send_mutex, portMAX_DELAY);
  if (enable && ble_device_manager_is_switching()) {
    enable = false;
  }
  portENTER_CRITICAL(&s_state_spinlock);
  s_send_enabled = enable;
  if (enable) {
    s_send_conn_id = ble_hid_conn_id;
  }
  portEXIT_CRITICAL(&s_state_spinlock);
  xSemaphoreGive(s_send_mutex);
}

bool ble_hid_send_is_ready(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  bool ready = s_send_mutex != NULL && s_send_enabled;
  portEXIT_CRITICAL(&s_state_spinlock);
  return ready && !ble_device_manager_is_switching();
}

bool ble_hid_send_get_status(void) { return ble_hid_send_is_ready(); }

/**
 * @brief 内部发送函数，带互斥锁保护
 */
static esp_err_t ble_hid_send_report_internal(uint8_t report_id, uint8_t type,
                                              const uint8_t *report,
                                              uint8_t length,
                                              bool check_session,
                                              uint32_t generation) {
  if (s_send_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = ESP_ERR_INVALID_STATE;

  // 尝试获取互斥锁（使用短超时避免阻塞）
  if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // 在锁内检查所有状态（原子性）
    if (ble_hid_send_is_ready()) {
      // 再次检查 ble_device_manager 的状态（双重保险）
      if (!ble_device_manager_is_switching() &&
          (!check_session || mouse_accumulator_session_is_current(generation))) {
        ret = hid_dev_send_report(hidd_le_env.gatt_if, s_send_conn_id,
                                  report_id, type, length, (uint8_t *)report);
      }
    }
    xSemaphoreGive(s_send_mutex);
  } else {
    // 获取锁超时
    ESP_LOGD(TAG, "发送报告超时 (report_id=%d)", report_id);
    ret = ESP_ERR_TIMEOUT;
  }

  return ret;
}

esp_err_t ble_hid_send_mouse_report(const uint8_t *report, uint8_t length) {
  return ble_hid_send_report_internal(HID_RPT_ID_MOUSE_IN,
                                      HID_REPORT_TYPE_INPUT, report, length,
                                      false, 0);
}

esp_err_t ble_hid_send_mouse_report_for_session(const uint8_t *report,
                                                uint8_t length,
                                                uint32_t generation) {
  return ble_hid_send_report_internal(HID_RPT_ID_MOUSE_IN,
                                      HID_REPORT_TYPE_INPUT, report, length,
                                      true, generation);
}

esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, uint8_t length) {
  return ble_hid_send_report_internal(HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT,
                                      report, length, false, 0);
}

esp_err_t ble_hid_send_cc_report(const uint8_t *report, uint8_t length) {
  return ble_hid_send_report_internal(HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT,
                                      report, length, false, 0);
}
