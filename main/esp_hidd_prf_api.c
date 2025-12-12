/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_hidd_prf_api.h"
#include "hidd_le_prf_int.h"
#include "hid_dev.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

// HID keyboard input report length
#define HID_KEYBOARD_IN_RPT_LEN 8

// HID LED output report length
#define HID_LED_OUT_RPT_LEN 1

// HID mouse input report length
// 配置选项：使用16位精度（1）或8位精度（0）
// 注意：Report Map 中 X/Y 定义为 16bit 以兼容 8bit 和 16bit
// 如果 USE_16BIT_MOUSE_PRECISION=1，发送完整的 16bit 数据
// 如果 USE_16BIT_MOUSE_PRECISION=0，发送 8bit 数据但放在 16bit 字段中
// 注意：此宏必须与hid_device_le_prf.c和hid_host_example.c中的定义保持一致
#ifndef USE_16BIT_MOUSE_PRECISION
#define USE_16BIT_MOUSE_PRECISION 1
#endif

// 基于 Zephyr report map: 按钮(1字节: 3位按钮+5位padding) + X(2字节, 16bit) + Y(2字节, 16bit) + Wheel(1字节) = 6字节
// 注意：即使发送 8bit 数据，报告长度仍为 6 字节（8bit 数据放在 16bit 字段的低 8 位）
#define HID_MOUSE_IN_RPT_LEN 6 // 按钮(1) + X(2) + Y(2) + Wheel(1) = 6字节（兼容 8bit 和 16bit）

// HID consumer control input report length
#define HID_CC_IN_RPT_LEN 2

esp_err_t esp_hidd_register_callbacks(esp_hidd_event_cb_t callbacks)
{
  esp_err_t hidd_status;

  if (callbacks != NULL)
  {
    hidd_le_env.hidd_cb = callbacks;
  }
  else
  {
    return ESP_FAIL;
  }

  if ((hidd_status = hidd_register_cb()) != ESP_OK)
  {
    return hidd_status;
  }

  esp_ble_gatts_app_register(BATTRAY_APP_ID);

  if ((hidd_status = esp_ble_gatts_app_register(HIDD_APP_ID)) != ESP_OK)
  {
    return hidd_status;
  }

  return hidd_status;
}

esp_err_t esp_hidd_profile_init(void)
{
  if (hidd_le_env.enabled)
  {
    ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already initialized");
    return ESP_FAIL;
  }
  // Reset the hid device target environment
  memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
  hidd_le_env.enabled = true;
  return ESP_OK;
}

esp_err_t esp_hidd_profile_deinit(void)
{
  uint16_t hidd_svc_hdl = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC];
  if (!hidd_le_env.enabled)
  {
    ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already initialized");
    return ESP_OK;
  }

  if (hidd_svc_hdl != 0)
  {
    esp_ble_gatts_stop_service(hidd_svc_hdl);
    esp_ble_gatts_delete_service(hidd_svc_hdl);
  }
  else
  {
    return ESP_FAIL;
  }

  /* register the HID device profile to the BTA_GATTS module*/
  esp_ble_gatts_app_unregister(hidd_le_env.gatt_if);

  return ESP_OK;
}

uint16_t esp_hidd_get_version(void)
{
  return HIDD_VERSION;
}

void esp_hidd_send_consumer_value(uint16_t conn_id, uint8_t key_cmd, bool key_pressed)
{
  uint8_t buffer[HID_CC_IN_RPT_LEN] = {0, 0};
  if (key_pressed)
  {
    ESP_LOGD(HID_LE_PRF_TAG, "hid_consumer_build_report");
    hid_consumer_build_report(buffer, key_cmd);
  }
  ESP_LOGD(HID_LE_PRF_TAG, "buffer[0] = %x, buffer[1] = %x", buffer[0], buffer[1]);
  hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                      HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT, HID_CC_IN_RPT_LEN, buffer);
  return;
}

/**
 * @brief Send keyboard value
 *
 * @param conn_id Connection ID
 * @param special_key_mask Modifier byte
 * @param keyboard_cmd key array
 * @param num_key length of key array
 */
void esp_hidd_send_keyboard_value(uint16_t conn_id, key_mask_t special_key_mask, uint8_t *keyboard_cmd, uint8_t num_key)
{
  if (num_key > HID_KEYBOARD_IN_RPT_LEN - 2)
  {
    ESP_LOGE(HID_LE_PRF_TAG, "%s(), the number key should not be more than %d", __func__, HID_KEYBOARD_IN_RPT_LEN);
    return;
  }

  uint8_t buffer[HID_KEYBOARD_IN_RPT_LEN] = {0};

  buffer[0] = special_key_mask;

  for (int i = 0; i < num_key; i++)
  {
    buffer[i + 2] = keyboard_cmd[i];
  }

  ESP_LOGD(HID_LE_PRF_TAG, "the key vaule = %d,%d,%d, %d, %d, %d,%d, %d", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
  hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                      HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, HID_KEYBOARD_IN_RPT_LEN, buffer);
  return;
}

void esp_hidd_send_mouse_value(uint16_t conn_id, uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y)
{
  uint8_t buffer[HID_MOUSE_IN_RPT_LEN] = {0};

  // 按钮：只使用低3位（左、右、中键），高5位为padding（自动为0）
  buffer[0] = mouse_button & 0x07; // 只保留低3位，符合 Zephyr report map 的3个按钮定义

#if USE_16BIT_MOUSE_PRECISION
  // 16位格式：发送完整的16位数据
  // X位移（16位，little-endian）
  int16_t x_16 = (int16_t)mickeys_x;
  buffer[1] = (uint8_t)(x_16 & 0xFF);
  buffer[2] = (uint8_t)((x_16 >> 8) & 0xFF);

  // Y位移（16位，little-endian）
  int16_t y_16 = (int16_t)mickeys_y;
  buffer[3] = (uint8_t)(y_16 & 0xFF);
  buffer[4] = (uint8_t)((y_16 >> 8) & 0xFF);
#else
  // 8位格式：将8位数据放在16位字段的低8位，高8位为0（符号扩展）
  // X位移（16位，little-endian）- 8位数据放在低8位
  int16_t x_16 = (int16_t)(int8_t)mickeys_x; // 符号扩展
  buffer[1] = (uint8_t)(x_16 & 0xFF);
  buffer[2] = (uint8_t)((x_16 >> 8) & 0xFF);

  // Y位移（16位，little-endian）- 8位数据放在低8位
  int16_t y_16 = (int16_t)(int8_t)mickeys_y; // 符号扩展
  buffer[3] = (uint8_t)(y_16 & 0xFF);
  buffer[4] = (uint8_t)((y_16 >> 8) & 0xFF);
#endif

  // Wheel字段（字节5），默认为0（此函数不支持滚轮参数，需要单独的函数）
  buffer[5] = 0;

  hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
                      HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, HID_MOUSE_IN_RPT_LEN, buffer);
  return;
}
