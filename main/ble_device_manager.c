/*
 * BLE Device Manager - Dual Slot Switching Implementation
 *
 * Manages two device slots (A/B) with hotkey switching.
 */

#include "ble_device_manager.h"
#include "ble_hid_send.h"
#include "ble_hid_callbacks.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "hidd_le_prf_int.h"
#include "mouse_accumulator.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

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
// 注意: sec_conn 仅用于 LED 状态显示，发送控制使用 ble_hid_send_enable()
extern bool sec_conn;

// Forward declarations
static esp_err_t save_slots_to_nvs(void);
static esp_err_t load_slots_from_nvs(void);
static bool s_pending_connection = false;
static esp_bd_addr_t s_pending_bda;

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

/* Change modes before disabling sending; AUTH for an earlier candidate must
 * never complete the new operation. GAP callbacks are asynchronous, so only
 * DISCONNECT restarts advertising when a physical link exists. */
static esp_err_t reconnect_for_mode(void) {
  ble_hid_send_enable(false);
  sec_conn = false;
  mouse_accumulator_set_connected(false);
  ble_hid_callbacks_show_switching();
  extern hidd_le_env_t hidd_le_env;
  if (hidd_le_env.hidd_clcb[0].in_use) {
    return esp_ble_gap_disconnect(hidd_le_env.hidd_clcb[0].remote_bda);
  }
  ble_hid_callbacks_request_advertising();
  return ESP_OK;
}

esp_err_t ble_device_manager_switch_slot(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  if (!s_initialized || s_is_discovering) {
    portEXIT_CRITICAL(&s_state_spinlock);
    return ESP_ERR_INVALID_STATE;
  }
  int target = 1 - (s_is_switching ? s_target_slot : s_active_slot);
  if (!s_slots[target].valid) {
    portEXIT_CRITICAL(&s_state_spinlock);
    return ESP_ERR_NOT_FOUND;
  }
  s_is_switching = true;
  s_target_slot = target;
  s_pending_connection = false;
  portEXIT_CRITICAL(&s_state_spinlock);
  ESP_LOGI(TAG, "Waiting for Slot %c authentication", 'A' + target);
  return reconnect_for_mode();
}

esp_err_t ble_device_manager_discover_new(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  if (!s_initialized || s_is_discovering) {
    portEXIT_CRITICAL(&s_state_spinlock);
    return ESP_ERR_INVALID_STATE;
  }
  /* Only an explicit switch keeps its target. Otherwise fill an empty slot
   * first, and replace the active slot when both slots are occupied. */
  int target = s_active_slot;
  if (s_is_switching && s_target_slot >= 0) {
    target = s_target_slot;
  } else if (!s_slots[s_active_slot].valid) {
    target = s_active_slot;
  } else if (!s_slots[1 - s_active_slot].valid) {
    target = 1 - s_active_slot;
  }
  s_target_slot = target;
  s_is_switching = false;
  s_is_discovering = true;
  s_pending_connection = false;
  portEXIT_CRITICAL(&s_state_spinlock);
  ESP_LOGI(TAG, "Discovering device for Slot %c", 'A' + target);
  return reconnect_for_mode();
}

bool ble_device_manager_on_connected(const esp_bd_addr_t bda) {
  portENTER_CRITICAL(&s_state_spinlock);
  bool accept = s_initialized;
  if (s_is_switching) {
    accept = s_target_slot >= 0 && s_slots[s_target_slot].valid &&
             bda_equal(s_slots[s_target_slot].bda, bda);
  } else if (s_is_discovering && s_target_slot >= 0) {
    int other = 1 - s_target_slot;
    /* Automatic reconnection from the retained host must not occupy the
     * empty slot intended for a second host. */
    accept = !(s_slots[other].valid && bda_equal(s_slots[other].bda, bda));
  }
  s_pending_connection = accept;
  if (accept) {
    memcpy(s_pending_bda, bda, sizeof(esp_bd_addr_t));
  }
  portEXIT_CRITICAL(&s_state_spinlock);
  return accept;
}

void ble_device_manager_on_disconnected(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  s_pending_connection = false;
  portEXIT_CRITICAL(&s_state_spinlock);
}

bool ble_device_manager_on_authenticated(const esp_bd_addr_t bda) {
  portENTER_CRITICAL(&s_state_spinlock);
  if (!s_pending_connection || !bda_equal(s_pending_bda, bda)) {
    portEXIT_CRITICAL(&s_state_spinlock);
    return false;
  }
  int target = s_active_slot;
  if (s_is_switching) {
    target = s_target_slot;
    if (target < 0 || !s_slots[target].valid ||
        !bda_equal(s_slots[target].bda, bda)) {
      portEXIT_CRITICAL(&s_state_spinlock);
      return false;
    }
  } else if (s_is_discovering) {
    target = s_target_slot;
    int other = 1 - target;
    if (s_slots[other].valid && bda_equal(s_slots[other].bda, bda)) {
      memset(&s_slots[other], 0, sizeof(s_slots[other]));
    }
    memcpy(s_slots[target].bda, bda, sizeof(esp_bd_addr_t));
    s_slots[target].valid = true;
  } else {
    bool known = false;
    for (int i = 0; i < MAX_DEVICE_SLOTS; i++) {
      if (s_slots[i].valid && bda_equal(s_slots[i].bda, bda)) {
        target = i;
        known = true;
        break;
      }
    }
    if (!known && !s_slots[target].valid) {
      memcpy(s_slots[target].bda, bda, sizeof(esp_bd_addr_t));
      s_slots[target].valid = true;
    }
  }
  s_active_slot = target;
  s_is_switching = false;
  s_is_discovering = false;
  s_target_slot = -1;
  s_pending_connection = false;
  portEXIT_CRITICAL(&s_state_spinlock);
  esp_err_t ret = save_slots_to_nvs();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Could not persist authenticated device: %s", esp_err_to_name(ret));
  }
  return true;
}

/**
 * @brief Get current active slot
 */
int ble_device_manager_get_active_slot(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  int result = s_active_slot;
  portEXIT_CRITICAL(&s_state_spinlock);
  return result;
}

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
bool ble_device_manager_is_discovering(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  bool result = s_is_discovering;
  portEXIT_CRITICAL(&s_state_spinlock);
  return result;
}

/**
 * @brief Get device info for a slot
 */
bool ble_device_manager_get_slot_device(int slot, esp_bd_addr_t out_bda) {
  if (slot < 0 || slot >= MAX_DEVICE_SLOTS) {
    return false;
  }
  portENTER_CRITICAL(&s_state_spinlock);
  bool valid = s_slots[slot].valid;
  if (valid && out_bda) {
    memcpy(out_bda, s_slots[slot].bda, sizeof(esp_bd_addr_t));
  }
  portEXIT_CRITICAL(&s_state_spinlock);
  return valid;
}

/**
 * @brief Clear all slots
 */
esp_err_t ble_device_manager_clear_all(void) {
  portENTER_CRITICAL(&s_state_spinlock);
  memset(s_slots, 0, sizeof(s_slots));
  s_active_slot = 0;
  s_is_switching = false;
  s_is_discovering = false;
  s_target_slot = -1;
  s_pending_connection = false;
  portEXIT_CRITICAL(&s_state_spinlock);

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

  device_slot_t slots[MAX_DEVICE_SLOTS];
  portENTER_CRITICAL(&s_state_spinlock);
  memcpy(slots, s_slots, sizeof(slots));
  int active_slot = s_active_slot;
  portEXIT_CRITICAL(&s_state_spinlock);

  // Save slot A
  if (slots[0].valid) {
    nvs_set_blob(nvs_handle, NVS_KEY_SLOT_A, slots[0].bda,
                 sizeof(esp_bd_addr_t));
  } else {
    nvs_erase_key(nvs_handle, NVS_KEY_SLOT_A);
  }

  // Save slot B
  if (slots[1].valid) {
    nvs_set_blob(nvs_handle, NVS_KEY_SLOT_B, slots[1].bda,
                 sizeof(esp_bd_addr_t));
  } else {
    nvs_erase_key(nvs_handle, NVS_KEY_SLOT_B);
  }

  // Save active slot
  nvs_set_i8(nvs_handle, NVS_KEY_ACTIVE_SLOT, (int8_t)active_slot);

  ret = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  ESP_LOGI(TAG, "Slots saved to NVS (active: %c)", 'A' + active_slot);
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
