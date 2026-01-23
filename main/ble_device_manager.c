/*
 * BLE Device Manager - Dual Slot Switching Implementation
 *
 * Manages two device slots (A/B) with hotkey switching.
 */

#include "ble_device_manager.h"
#include "ble_hid_send.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "hidd_le_prf_int.h"
#include "led_control.h"
#include "mouse_accumulator.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "BLE_DEV_MGR";

// NVS keys
#define NVS_KEY_SLOT_A "slot_a"
#define NVS_KEY_SLOT_B "slot_b"
#define NVS_KEY_ACTIVE_SLOT "active"

// State
static device_slot_t s_slots[MAX_DEVICE_SLOTS];
static int s_active_slot = 0; // 0=A, 1=B
static bool s_is_switching = false;
static bool s_is_discovering = false;
static bool s_initialized = false;

// Spinlock 保护状态变量（用于 ISR 安全访问）
static portMUX_TYPE s_state_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Target slot during switching (which slot's device we want to connect to)
static int s_target_slot = -1;

// External variables
extern led_strip_handle_t led_strip;
// 注意: sec_conn 仅用于 LED 状态显示，发送控制使用 ble_hid_send_enable()
extern bool sec_conn;
extern uint16_t ble_hid_conn_id;

// Forward declarations
static esp_err_t save_slots_to_nvs(void);
static esp_err_t load_slots_from_nvs(void);
static void start_advertising(void);

/**
 * @brief Check if two BDA addresses are equal
 */
static bool bda_equal(const esp_bd_addr_t a, const esp_bd_addr_t b) {
  return memcmp(a, b, sizeof(esp_bd_addr_t)) == 0;
}

/**
 * @brief Format BDA as string for logging
 */
static void bda_to_string(const esp_bd_addr_t bda, char *out, size_t out_len) {
  snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1],
           bda[2], bda[3], bda[4], bda[5]);
}

/**
 * @brief Initialize the BLE device manager
 */
esp_err_t ble_device_manager_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  // Initialize slots
  memset(s_slots, 0, sizeof(s_slots));
  s_active_slot = 0;
  s_is_switching = false;
  s_is_discovering = false;
  s_target_slot = -1;

  // Load from NVS
  esp_err_t ret = load_slots_from_nvs();
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load slots from NVS: %s", esp_err_to_name(ret));
  }

  // Log loaded slots
  for (int i = 0; i < MAX_DEVICE_SLOTS; i++) {
    if (s_slots[i].valid) {
      char bda_str[18];
      bda_to_string(s_slots[i].bda, bda_str, sizeof(bda_str));
      ESP_LOGI(TAG, "Slot %c: %s", 'A' + i, bda_str);
    } else {
      ESP_LOGI(TAG, "Slot %c: (empty)", 'A' + i);
    }
  }

  ESP_LOGI(TAG, "Device manager initialized, active slot: %c",
           'A' + s_active_slot);
  s_initialized = true;
  return ESP_OK;
}

/**
 * @brief Switch to the other slot (Alt + `)
 *
 * 如果当前正在切换等待目标设备，再次按 Alt+` 会切换到另一个槽位。
 */
esp_err_t ble_device_manager_switch_slot(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Device manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // 如果已经在发现模式，不允许切换
  if (s_is_discovering) {
    ESP_LOGW(TAG, "Discovery in progress, cannot switch");
    return ESP_ERR_INVALID_STATE;
  }

  // 计算目标槽位
  int other_slot;
  if (s_is_switching && s_target_slot >= 0) {
    // 已经在切换模式，切换到另一个槽位
    other_slot = 1 - s_target_slot;
    ESP_LOGI(TAG, "Already switching, changing target from Slot %c to Slot %c",
             'A' + s_target_slot, 'A' + other_slot);
  } else {
    // 正常模式，切换到另一个槽位
    other_slot = 1 - s_active_slot;
  }

  // Check if target slot has a device
  if (!s_slots[other_slot].valid) {
    ESP_LOGW(TAG, "Slot %c is empty, cannot switch", 'A' + other_slot);
    ESP_LOGI(TAG, "Hint: Use Alt+N to discover a new device for Slot %c",
             'A' + other_slot);
    return ESP_ERR_NOT_FOUND;
  }

  char bda_str[18];
  bda_to_string(s_slots[other_slot].bda, bda_str, sizeof(bda_str));
  ESP_LOGI(TAG, "===========================================");
  ESP_LOGI(TAG, "Switching from Slot %c to Slot %c",
           s_is_switching ? 'A' + s_target_slot : 'A' + s_active_slot,
           'A' + other_slot);
  ESP_LOGI(TAG, "Target device: %s", bda_str);
  ESP_LOGI(TAG, "===========================================");

  // 设置切换状态（使用 spinlock 保护）
  portENTER_CRITICAL(&s_state_spinlock);
  s_is_switching = true;
  s_target_slot = other_slot;
  portEXIT_CRITICAL(&s_state_spinlock);

  // 立即禁止报告发送（使用线程安全接口）
  ble_hid_send_enable(false);
  sec_conn = false; // 保留用于 LED 状态显示

  // LED feedback
  if (led_strip) {
    led_control_blink_switching(led_strip);
  }

  // Clear mouse accumulator
  mouse_accumulator_clear();

  // Disconnect current connection if any
  extern hidd_le_env_t hidd_le_env;
  if (hidd_le_env.hidd_clcb[0].in_use) {
    esp_ble_gap_disconnect(hidd_le_env.hidd_clcb[0].remote_bda);
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  // Start advertising
  start_advertising();

  return ESP_OK;
}

/**
 * @brief Start discovering new device (Alt + N)
 *
 * 如果当前正在切换等待目标设备，按 Alt+N 会取消切换并进入发现模式，
 * 新设备将保存到原本的切换目标槽位。
 */
esp_err_t ble_device_manager_discover_new(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Device manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // 如果已经在发现模式，直接返回
  if (s_is_discovering) {
    ESP_LOGW(TAG, "Discovery already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  // 如果正在切换等待目标设备，允许转为发现模式
  int discover_target_slot = s_active_slot; // 默认目标槽位
  if (s_is_switching && s_target_slot >= 0) {
    discover_target_slot = s_target_slot; // 使用切换的目标槽位
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Canceling switch, discovering new device for Slot %c",
             'A' + discover_target_slot);
    ESP_LOGI(TAG, "===========================================");
  } else {
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Discovering new device for Slot %c",
             'A' + discover_target_slot);
    ESP_LOGI(TAG, "===========================================");
  }

  // 设置发现状态（使用 spinlock 保护）
  portENTER_CRITICAL(&s_state_spinlock);
  s_is_switching = false; // 取消切换状态
  s_is_discovering = true;
  s_target_slot = discover_target_slot; // 保存目标槽位（用于 on_connected）
  portEXIT_CRITICAL(&s_state_spinlock);

  // 立即禁止报告发送（使用线程安全接口）
  ble_hid_send_enable(false);
  sec_conn = false; // 保留用于 LED 状态显示

  // LED feedback - different pattern for discovery
  if (led_strip) {
    led_control_blink_switching(led_strip);
  }

  // Clear mouse accumulator
  mouse_accumulator_clear();

  // Disconnect current connection if any
  extern hidd_le_env_t hidd_le_env;
  if (hidd_le_env.hidd_clcb[0].in_use) {
    esp_ble_gap_disconnect(hidd_le_env.hidd_clcb[0].remote_bda);
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  // Start advertising
  start_advertising();

  return ESP_OK;
}

/**
 * @brief Handle device connection
 */
void ble_device_manager_on_connected(const esp_bd_addr_t bda) {
  char bda_str[18];
  bda_to_string(bda, bda_str, sizeof(bda_str));

  // Normal mode - just update current slot if needed
  if (!s_is_switching && !s_is_discovering) {
    // Check if this device is in any slot
    for (int i = 0; i < MAX_DEVICE_SLOTS; i++) {
      if (s_slots[i].valid && bda_equal(s_slots[i].bda, bda)) {
        s_active_slot = i;
        ESP_LOGI(TAG, "Connected to Slot %c device: %s", 'A' + i, bda_str);
        save_slots_to_nvs();
        return;
      }
    }

    // New device in normal mode - add to current slot if empty
    if (!s_slots[s_active_slot].valid) {
      memcpy(s_slots[s_active_slot].bda, bda, sizeof(esp_bd_addr_t));
      s_slots[s_active_slot].valid = true;
      ESP_LOGI(TAG, "New device saved to Slot %c: %s", 'A' + s_active_slot,
               bda_str);
      save_slots_to_nvs();
    } else {
      ESP_LOGI(TAG, "Connected to unknown device: %s (not saved)", bda_str);
    }
    return;
  }

  // Switching mode - only accept target slot's device
  if (s_is_switching && s_target_slot >= 0) {
    if (s_slots[s_target_slot].valid &&
        bda_equal(s_slots[s_target_slot].bda, bda)) {
      // Correct device connected - 清除切换状态（使用 spinlock 保护）
      portENTER_CRITICAL(&s_state_spinlock);
      s_active_slot = s_target_slot;
      s_is_switching = false;
      s_target_slot = -1;
      portEXIT_CRITICAL(&s_state_spinlock);

      // 注意：不在这里启用发送，等待 AUTH_CMPL 事件确认连接完全建立
      // ble_hid_send_enable() 会在 gap_event_handler 的
      // ESP_GAP_BLE_AUTH_CMPL_EVT 中调用
      sec_conn = true; // 保留用于 LED 状态显示
      ESP_LOGI(TAG, "Successfully switched to Slot %c: %s", 'A' + s_active_slot,
               bda_str);
      save_slots_to_nvs();
    } else {
      // Wrong device, reject and continue waiting
      ESP_LOGW(TAG, "Rejecting non-target device: %s, waiting for Slot %c",
               bda_str, 'A' + s_target_slot);
      extern hidd_le_env_t hidd_le_env;
      if (hidd_le_env.hidd_clcb[0].in_use) {
        esp_ble_gap_disconnect(hidd_le_env.hidd_clcb[0].remote_bda);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      start_advertising();
    }
    return;
  }

  // Discovering mode - accept any new device
  if (s_is_discovering) {
    // 使用 s_target_slot 作为目标槽位（如果已设置）
    // 否则使用原有的优先填入空槽位逻辑
    int target_slot;

    if (s_target_slot >= 0 && s_target_slot < MAX_DEVICE_SLOTS) {
      // 使用指定的目标槽位（从切换模式转发现模式时设置）
      target_slot = s_target_slot;
      ESP_LOGI(TAG, "Using specified target Slot %c for new device",
               'A' + target_slot);
    } else {
      // 原有逻辑：优先填入空槽位
      target_slot = s_active_slot; // 默认替换当前槽位
      int other_slot = 1 - s_active_slot;

      if (!s_slots[other_slot].valid) {
        // 另一个槽位为空，填入空槽位
        target_slot = other_slot;
        ESP_LOGI(TAG, "Slot %c is empty, saving new device there",
                 'A' + target_slot);
      } else {
        // 两个槽位都不为空，替换当前槽位
        ESP_LOGI(TAG, "Both slots occupied, replacing current Slot %c",
                 'A' + target_slot);
      }
    }

    // 检查另一个槽位是否已有相同的设备地址（避免同一台主机占用两个槽位）
    int other_slot = 1 - target_slot;
    if (s_slots[other_slot].valid && bda_equal(s_slots[other_slot].bda, bda)) {
      ESP_LOGW(TAG, "Device already exists in Slot %c, clearing duplicate",
               'A' + other_slot);
      memset(s_slots[other_slot].bda, 0, sizeof(esp_bd_addr_t));
      s_slots[other_slot].valid = false;
    }

    memcpy(s_slots[target_slot].bda, bda, sizeof(esp_bd_addr_t));
    s_slots[target_slot].valid = true;

    // 清除发现状态（使用 spinlock 保护）
    portENTER_CRITICAL(&s_state_spinlock);
    s_active_slot = target_slot; // 切换到新设备的槽位
    s_is_discovering = false;
    s_target_slot = -1; // 重置目标槽位
    portEXIT_CRITICAL(&s_state_spinlock);

    // 注意：不在这里启用发送，等待 AUTH_CMPL 事件确认连接完全建立
    // ble_hid_send_enable() 会在 gap_event_handler 的 ESP_GAP_BLE_AUTH_CMPL_EVT
    // 中调用
    sec_conn = true; // 保留用于 LED 状态显示
    ESP_LOGI(TAG, "New device saved to Slot %c: %s", 'A' + target_slot,
             bda_str);
    save_slots_to_nvs();
  }
}

/**
 * @brief Get current active slot
 */
int ble_device_manager_get_active_slot(void) { return s_active_slot; }

/**
 * @brief Check if switching is in progress (线程安全)
 */
bool ble_device_manager_is_switching(void) {
  bool result;
  portENTER_CRITICAL(&s_state_spinlock);
  result = s_is_switching || s_is_discovering;
  portEXIT_CRITICAL(&s_state_spinlock);
  return result;
}

/**
 * @brief Check if discovering new device
 */
bool ble_device_manager_is_discovering(void) { return s_is_discovering; }

/**
 * @brief Get device info for a slot
 */
bool ble_device_manager_get_slot_device(int slot, esp_bd_addr_t out_bda) {
  if (slot < 0 || slot >= MAX_DEVICE_SLOTS) {
    return false;
  }
  if (!s_slots[slot].valid) {
    return false;
  }
  memcpy(out_bda, s_slots[slot].bda, sizeof(esp_bd_addr_t));
  return true;
}

/**
 * @brief Clear all slots
 */
esp_err_t ble_device_manager_clear_all(void) {
  memset(s_slots, 0, sizeof(s_slots));
  s_active_slot = 0;
  s_is_switching = false;
  s_is_discovering = false;
  s_target_slot = -1;

  // Clear NVS
  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(BLE_DEV_MGR_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret == ESP_OK) {
    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
  }

  ESP_LOGI(TAG, "All slots cleared");
  return ESP_OK;
}

/**
 * @brief Start advertising
 */
static void start_advertising(void) {
  esp_ble_gap_stop_advertising();
  vTaskDelay(pdMS_TO_TICKS(50));

  esp_ble_adv_params_t adv_params = {
      .adv_int_min = 0x20,
      .adv_int_max = 0x40,
      .adv_type = ADV_TYPE_IND,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };

  esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start advertising: %s", esp_err_to_name(ret));
    s_is_switching = false;
    s_is_discovering = false;
  } else {
    ESP_LOGI(TAG, "Advertising started...");
  }
}

/**
 * @brief Save slots to NVS
 */
static esp_err_t save_slots_to_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(BLE_DEV_MGR_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
    return ret;
  }

  // Save slot A
  if (s_slots[0].valid) {
    nvs_set_blob(nvs_handle, NVS_KEY_SLOT_A, s_slots[0].bda,
                 sizeof(esp_bd_addr_t));
  } else {
    nvs_erase_key(nvs_handle, NVS_KEY_SLOT_A);
  }

  // Save slot B
  if (s_slots[1].valid) {
    nvs_set_blob(nvs_handle, NVS_KEY_SLOT_B, s_slots[1].bda,
                 sizeof(esp_bd_addr_t));
  } else {
    nvs_erase_key(nvs_handle, NVS_KEY_SLOT_B);
  }

  // Save active slot
  nvs_set_i8(nvs_handle, NVS_KEY_ACTIVE_SLOT, (int8_t)s_active_slot);

  ret = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  ESP_LOGI(TAG, "Slots saved to NVS (active: %c)", 'A' + s_active_slot);
  return ret;
}

/**
 * @brief Load slots from NVS
 */
static esp_err_t load_slots_from_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(BLE_DEV_MGR_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  // Load slot A
  size_t len = sizeof(esp_bd_addr_t);
  ret = nvs_get_blob(nvs_handle, NVS_KEY_SLOT_A, s_slots[0].bda, &len);
  if (ret == ESP_OK && len == sizeof(esp_bd_addr_t)) {
    s_slots[0].valid = true;
  }

  // Load slot B
  len = sizeof(esp_bd_addr_t);
  ret = nvs_get_blob(nvs_handle, NVS_KEY_SLOT_B, s_slots[1].bda, &len);
  if (ret == ESP_OK && len == sizeof(esp_bd_addr_t)) {
    s_slots[1].valid = true;
  }

  // Load active slot
  int8_t active = 0;
  if (nvs_get_i8(nvs_handle, NVS_KEY_ACTIVE_SLOT, &active) == ESP_OK) {
    if (active >= 0 && active < MAX_DEVICE_SLOTS) {
      s_active_slot = active;
    }
  }

  nvs_close(nvs_handle);
  return ESP_OK;
}
