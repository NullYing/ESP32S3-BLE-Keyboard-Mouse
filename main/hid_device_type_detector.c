/*
 * SPDX-License-Identifier: MIT
 * HID Device Type Detector Implementation
 */

#include "hid_device_type_detector.h"
#include "esp_log.h"
#include "usb/hid_host.h"
#include "hid_report_parser_c.h"
#include <inttypes.h>

static const char *TAG = "HID_DEV_TYPE";

bool hid_device_type_detect(hid_host_device_handle_t hid_device_handle, bool *is_keyboard, bool *is_mouse)
{
  *is_keyboard = false;
  *is_mouse = false;

  size_t report_desc_len = 0;
  const uint8_t *report_desc = hid_host_get_report_descriptor(hid_device_handle, &report_desc_len);

  if (report_desc == NULL || report_desc_len == 0)
  {
    ESP_LOGW(TAG, "无法获取 HID Report Descriptor，将使用协议字段判断设备类型");
    return false;
  }

  // 方法1：尝试解析鼠标布局，如果成功则认为是鼠标设备
  hid_report_layout_t layout = {0};
  if (parse_hid_report_descriptor_layout(report_desc, report_desc_len, &layout) == 0)
  {
    // 成功解析出鼠标布局，检查是否有鼠标相关的字段（X/Y坐标）
    if (layout.x_size > 0 && layout.y_size > 0)
    {
      *is_mouse = true;
      ESP_LOGI(TAG, "通过Report Descriptor解析检测到鼠标设备 (X/Y坐标字段存在)");
      return true;
    }
  }

  // 方法2：解析 HID Report Descriptor，查找 Application Collection 中的 Usage 和实际字段
  // 对于键盘：需要检查是否有 Key Codes Page (0x07) 的输入字段
  // 对于鼠标：需要检查是否有 X/Y 坐标字段（已在方法1中检查）
  size_t offset = 0;
  uint8_t current_usage_page = PAGE_GENERIC_DESKTOP; // 默认值
  bool in_application_collection = false;
  uint8_t collection_usage = 0;
  bool found_keyboard_usage = false;
  bool found_key_codes_input = false; // 键盘需要 Key Codes Page (0x07) 的输入字段
  uint32_t key_codes_input_count = 0; // Key Codes输入字段的数量（真正的键盘应该有多个按键）
  bool found_mouse_usage = false;
  bool found_xy_input = false;         // 鼠标需要 X/Y 坐标输入字段
  bool in_keyboard_collection = false; // 当前是否在键盘Collection中
  bool in_mouse_collection = false;    // 当前是否在鼠标Collection中
  uint32_t report_count = 0;           // 当前字段的报告数量

  while (offset < report_desc_len)
  {
    uint8_t item = report_desc[offset];
    uint8_t item_type = (item >> 2) & 0x03; // Bits 2-3: item type
    uint8_t item_tag = (item >> 4) & 0x0F;  // Bits 4-7: item tag
    uint8_t item_size = item & 0x03;        // Bits 0-1: item size

    if (item_size == 3)
    {
      // Long item - 需要特殊处理
      if (offset + 1 < report_desc_len)
      {
        uint8_t long_item_size = report_desc[offset + 1];
        offset += 2 + long_item_size; // Skip long item
        continue;
      }
      else
      {
        break;
      }
    }

    offset++; // Skip item byte

    // Read item data (最多2字节)
    uint16_t item_data = 0;
    if (item_size > 0 && offset + item_size <= report_desc_len)
    {
      for (uint8_t i = 0; i < item_size && i < 2; i++)
      {
        item_data |= ((uint16_t)report_desc[offset + i]) << (i * 8);
      }
    }

    // Process item
    if (item_type == 1) // Global item
    {
      if (item_tag == 0) // Usage Page
      {
        current_usage_page = (uint8_t)item_data;
      }
      else if (item_tag == 9) // Report Count (0x94 = 1001 0100)
      {
        report_count = item_data;
      }
    }
    else if (item_type == 2) // Local item
    {
      if (item_tag == 0) // Usage
      {
        if (in_application_collection)
        {
          if (current_usage_page == PAGE_GENERIC_DESKTOP)
          {
            if (item_data == USAGE_MOUSE)
            {
              found_mouse_usage = true;
              in_mouse_collection = true;
            }
            else if (item_data == 0x06) // USAGE_KEYBOARD
            {
              found_keyboard_usage = true;
              in_keyboard_collection = true;
            }
            // 检查是否有 X/Y 坐标 Usage（鼠标）
            else if (item_data == 0x30 || item_data == 0x31) // USAGE_X (0x30) or USAGE_Y (0x31)
            {
              if (in_mouse_collection)
              {
                found_xy_input = true;
              }
            }
          }
        }
        // 保存Collection的Usage（在Collection之前）
        if (!in_application_collection)
        {
          collection_usage = (uint8_t)item_data;
        }
      }
    }
    else if (item_type == 0) // Main item
    {
      if (item_tag == 8) // Input (0x80 = 1000 0000)
      {
        // 检查是否有 Key Codes Page 的输入字段（键盘）
        if (in_keyboard_collection && current_usage_page == 0x07) // Key Codes Page
        {
          found_key_codes_input = true;
          // 累计按键数量（真正的键盘应该有多个按键，通常至少6个）
          key_codes_input_count += report_count;
        }
        // 检查是否有 X/Y 坐标输入字段（鼠标，如果还没通过layout检测到）
        if (in_mouse_collection && current_usage_page == PAGE_GENERIC_DESKTOP)
        {
          // 如果当前在鼠标Collection中，且有输入字段，可能是X/Y坐标
          found_xy_input = true;
        }
      }
      else if (item_tag == 10) // Collection (0xA0 = 1010 0000)
      {
        if (item_data == COLLECTION_TYPE_APPLICATION)
        {
          in_application_collection = true;
          // 检查Collection的Usage（在Collection之前设置的）
          if (current_usage_page == PAGE_GENERIC_DESKTOP)
          {
            if (collection_usage == USAGE_MOUSE)
            {
              found_mouse_usage = true;
              in_mouse_collection = true;
            }
            else if (collection_usage == 0x06) // USAGE_KEYBOARD
            {
              found_keyboard_usage = true;
              in_keyboard_collection = true;
            }
          }
          collection_usage = 0; // Reset
        }
      }
      else if (item_tag == 12) // End Collection (0xC0 = 1100 0000)
      {
        in_application_collection = false;
        in_keyboard_collection = false;
        in_mouse_collection = false;
        collection_usage = 0;
      }
    }

    offset += item_size;
  }

  // 最终判断：需要同时有Usage和实际字段
  // 键盘：需要 Keyboard Usage 和 Key Codes Page 的输入字段，且按键数量足够（至少3个，标准键盘通常有6个）
  // 这样可以排除Hub等设备，它们可能有Keyboard Usage但按键数量很少
  if (found_keyboard_usage && found_key_codes_input)
  {
    if (key_codes_input_count >= 3)
    {
      *is_keyboard = true;
      ESP_LOGI(TAG, "检测到键盘设备 (Usage: Keyboard + Key Codes输入字段, 按键数: %" PRIu32 ")", key_codes_input_count);
    }
    else
    {
      ESP_LOGW(TAG, "检测到Keyboard Usage和Key Codes输入字段，但按键数量太少(%" PRIu32 ")，可能是Hub或其他设备，不注册为键盘", key_codes_input_count);
    }
  }
  else if (found_keyboard_usage && !found_key_codes_input)
  {
    ESP_LOGW(TAG, "检测到Keyboard Usage但缺少Key Codes输入字段，可能是Hub或其他设备，不注册为键盘");
  }

  // 鼠标：需要 Mouse Usage 和 X/Y 坐标字段（或已通过layout检测到）
  if (found_mouse_usage && found_xy_input)
  {
    *is_mouse = true;
    ESP_LOGI(TAG, "检测到鼠标设备 (Usage: Mouse + X/Y坐标字段)");
  }

  return true;
}
