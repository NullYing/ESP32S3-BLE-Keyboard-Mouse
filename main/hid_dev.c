/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "hid_dev.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"
#include "hidd_le_prf_int.h" // 包含HID_RPT_ID_MOUSE_IN和HID_REPORT_TYPE_INPUT定义

static hid_report_map_t *hid_dev_rpt_tbl;
static uint8_t hid_dev_rpt_tbl_Len;

static hid_report_map_t *hid_dev_rpt_by_id(uint8_t id, uint8_t type)
{
  hid_report_map_t *rpt = hid_dev_rpt_tbl;

  for (uint8_t i = hid_dev_rpt_tbl_Len; i > 0; i--, rpt++)
  {
    if (rpt->id == id && rpt->type == type && rpt->mode == hidProtocolMode)
    {
      return rpt;
    }
  }

  return NULL;
}

void hid_dev_register_reports(uint8_t num_reports, hid_report_map_t *p_report)
{
  hid_dev_rpt_tbl = p_report;
  hid_dev_rpt_tbl_Len = num_reports;
  return;
}

/**
 * @brief Send HID report to the peer device
 *
 * @param gatts_if GATT interface
 * @param conn_id Connection ID
 * @param id Report ID
 * @param type Report type
 * @param length Report length
 * @param data Report data
 */
esp_err_t hid_dev_send_report(esp_gatt_if_t gatts_if, uint16_t conn_id,
                              uint8_t id, uint8_t type, uint8_t length, uint8_t *data)
{
  hid_report_map_t *p_rpt;

  // get att handle for report
  if ((p_rpt = hid_dev_rpt_by_id(id, type)) != NULL)
  {
    // 检查通知是否已启用（通过读取CCCD值）
    // 如果CCCD handle为0，说明是输出报告或特征报告，不需要检查通知
    if (p_rpt->cccdHandle != 0)
    {
      uint16_t cccd_len = 0;
      const uint8_t *cccd_value_ptr = NULL;
      esp_err_t get_ret = esp_ble_gatts_get_attr_value(p_rpt->cccdHandle, &cccd_len, &cccd_value_ptr);

      // 如果无法获取CCCD值，或者通知未启用（CCCD值不等于0x0001），则返回错误
      if (get_ret != ESP_OK || cccd_len < sizeof(uint16_t))
      {
        return ESP_ERR_INVALID_STATE; // 无法获取CCCD值或通知未启用
      }

      // 读取CCCD值（little-endian）
      uint16_t cccd_value = cccd_value_ptr[0] | (cccd_value_ptr[1] << 8);
      if ((cccd_value & 0x0001) == 0)
      {
        return ESP_ERR_INVALID_STATE; // 通知未启用
      }
    }

    // if notifications are enabled
    ESP_LOGD(HID_LE_PRF_TAG, "%s(), send the report, handle = %d", __func__, p_rpt->handle);
    esp_err_t ret = esp_ble_gatts_send_indicate(gatts_if, conn_id, p_rpt->handle, length, data, false);
    return ret;
  }

  return ESP_ERR_NOT_FOUND;
}

void hid_consumer_build_report(uint8_t *buffer, consumer_cmd_t cmd)
{
  if (!buffer)
  {
    ESP_LOGE(HID_LE_PRF_TAG, "%s(), the buffer is NULL, hid build report failed.", __func__);
    return;
  }

  switch (cmd)
  {
  case HID_CONSUMER_CHANNEL_UP:
    HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_UP);
    break;

  case HID_CONSUMER_CHANNEL_DOWN:
    HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_DOWN);
    break;

  case HID_CONSUMER_VOLUME_UP:
    HID_CC_RPT_SET_VOLUME_UP(buffer);
    break;

  case HID_CONSUMER_VOLUME_DOWN:
    HID_CC_RPT_SET_VOLUME_DOWN(buffer);
    break;

  case HID_CONSUMER_MUTE:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_MUTE);
    break;

  case HID_CONSUMER_POWER:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_POWER);
    break;

  case HID_CONSUMER_RECALL_LAST:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_LAST);
    break;

  case HID_CONSUMER_ASSIGN_SEL:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_ASSIGN_SEL);
    break;

  case HID_CONSUMER_PLAY:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PLAY);
    break;

  case HID_CONSUMER_PAUSE:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PAUSE);
    break;

  case HID_CONSUMER_RECORD:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_RECORD);
    break;

  case HID_CONSUMER_FAST_FORWARD:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_FAST_FWD);
    break;

  case HID_CONSUMER_REWIND:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_REWIND);
    break;

  case HID_CONSUMER_SCAN_NEXT_TRK:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_NEXT_TRK);
    break;

  case HID_CONSUMER_SCAN_PREV_TRK:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_PREV_TRK);
    break;

  case HID_CONSUMER_STOP:
    HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_STOP);
    break;

  default:
    break;
  }

  return;
}
