/*
 * BLE Device Manager - Dual Slot Switching
 *
 * Manages two device slots (A/B) with hotkey switching:
 * - Alt + ` : Switch between Slot A and Slot B
 * - Alt + N : Discover new device, filling an empty slot first
 */

#ifndef __BLE_DEVICE_MANAGER_H__
#define __BLE_DEVICE_MANAGER_H__

#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Number of device slots (A=0, B=1)
#define MAX_DEVICE_SLOTS 2

// NVS namespace for device manager
#define BLE_DEV_MGR_NVS_NAMESPACE "ble_dev_mgr"

/**
 * @brief Device slot structure
 */
typedef struct {
  esp_bd_addr_t bda; // Device Bluetooth address
  bool valid;        // Whether this slot has a device
} device_slot_t;

/**
 * @brief Initialize the BLE device manager
 */
esp_err_t ble_device_manager_init(void);

/**
 * @brief Switch to the other slot (Alt + `)
 *
 * If current slot is A, switch to B; if B, switch to A.
 * Disconnects current device and connects to the other slot's device.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_device_manager_switch_slot(void);

/**
 * @brief Start discovering new device (Alt + N)
 *
 * Disconnects current device and waits for new device to connect.
 * New device fills an empty slot, or replaces the active slot if both are full.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_device_manager_discover_new(void);

/**
 * @brief Handle device connection
 *
 * Called when a BLE device connects. Determines whether to accept
 * or reject based on current mode (switching/discovering).
 *
 * @param bda Device address
 */
bool ble_device_manager_on_connected(const esp_bd_addr_t bda);

/** Commit the accepted candidate only after successful authentication. */
bool ble_device_manager_on_authenticated(const esp_bd_addr_t bda);

/** Discard the candidate, keeping the discovery/switch target intact. */
void ble_device_manager_on_disconnected(void);

/**
 * @brief Get current active slot (0=A, 1=B)
 */
int ble_device_manager_get_active_slot(void);

/**
 * @brief Check if switching is in progress
 */
bool ble_device_manager_is_switching(void);

/**
 * @brief Check if discovering new device
 */
bool ble_device_manager_is_discovering(void);

/**
 * @brief Get device info for a slot
 *
 * @param slot Slot index (0 or 1)
 * @param out_bda Output buffer for device address
 * @return true if slot has a valid device
 */
bool ble_device_manager_get_slot_device(int slot, esp_bd_addr_t out_bda);

/**
 * @brief Clear all slots
 */
esp_err_t ble_device_manager_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // __BLE_DEVICE_MANAGER_H__
