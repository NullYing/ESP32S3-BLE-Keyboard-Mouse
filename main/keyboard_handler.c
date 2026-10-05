/*
 * Keyboard Handler - Implementation
 *
 * 键盘报告处理和热键检测
 */

#include "keyboard_handler.h"
#include "ble_device_manager.h"
#include "ble_hid_send.h"
#include "hid_config.h"
#include "usb_keyboard_report.h"
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "KB_HANDLER";

#define MAX_KEYBOARD_INTERFACES 4

typedef struct {
  hid_host_device_handle_t handle;
  bool boot_protocol;
  bool switch_down, discover_down;
  usb_keyboard_decoder_t decoder;
} keyboard_context_t;

static keyboard_context_t contexts[MAX_KEYBOARD_INTERFACES];

static keyboard_context_t *find_context(hid_host_device_handle_t handle) {
  for (int i = 0; i < MAX_KEYBOARD_INTERFACES; i++)
    if (contexts[i].handle == handle && handle) return &contexts[i];
  return NULL;
}

void keyboard_handler_clear(hid_host_device_handle_t handle) {
  keyboard_context_t *context = find_context(handle);
  if (context) memset(context, 0, sizeof(*context));
}

bool keyboard_handler_configure(hid_host_device_handle_t handle,
                                const uint8_t *descriptor, size_t descriptor_len,
                                bool boot_protocol) {
  if (!handle) return false;
  keyboard_handler_clear(handle);
  for (int i = 0; i < MAX_KEYBOARD_INTERFACES; i++) {
    keyboard_context_t *context = &contexts[i];
    if (context->handle) continue;
    if (!boot_protocol && !usb_keyboard_decoder_init(&context->decoder, descriptor, descriptor_len)) {
      memset(context, 0, sizeof(*context));
      return false;
    }
    context->handle = handle;
    context->boot_protocol = boot_protocol;
    return true;
  }
  return false;
}

/* =================================================================================================
   热键检测
   =================================================================================================
 */

bool check_slot_switch_hotkey(
    const hid_keyboard_input_report_boot_t *kb_report) {
  // Alt 键: Left Alt = 0x04, Right Alt = 0x40
  bool alt_pressed = (kb_report->modifier.val & 0x44) != 0;
  // 反引号键: HID_KEY_GRAVE = 0x35
  bool grave_pressed = false;
  for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
    if (kb_report->key[i] == 0x35) {
      grave_pressed = true;
      break;
    }
  }
  return alt_pressed && grave_pressed;
}

bool check_discover_hotkey(const hid_keyboard_input_report_boot_t *kb_report) {
  // Alt 键: Left Alt = 0x04, Right Alt = 0x40
  bool alt_pressed = (kb_report->modifier.val & 0x44) != 0;
  // N 键: HID_KEY_N = 0x11
  bool n_pressed = false;
  for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
    if (kb_report->key[i] == 0x11) {
      n_pressed = true;
      break;
    }
  }
  return alt_pressed && n_pressed;
}

/* =================================================================================================
   键盘报告回调
   =================================================================================================
 */

void hid_host_keyboard_report_callback(
    hid_host_device_handle_t hid_device_handle, uint8_t *data, int length) {
  keyboard_context_t *context = find_context(hid_device_handle);
  if (!context || !data || length <= 0) return;
  uint8_t decoded[HID_KEYBOARD_IN_RPT_LEN] = {0};
  if (context->boot_protocol) {
    if (length != HID_KEYBOARD_IN_RPT_LEN) return;
    memcpy(decoded, data, sizeof(decoded));
    decoded[1] = 0;
  } else if (!usb_keyboard_decode(&context->decoder, data, (size_t)length, decoded)) {
    return;
  }

  hid_keyboard_input_report_boot_t *kb_report_check =
      (hid_keyboard_input_report_boot_t *)decoded;
  bool switch_down = check_slot_switch_hotkey(kb_report_check);
  bool discover_down = check_discover_hotkey(kb_report_check);
  bool switch_edge = switch_down && !context->switch_down;
  bool discover_edge = discover_down && !context->discover_down;
  context->switch_down = switch_down;
  context->discover_down = discover_down;

  // Alt + ` : 槽位切换
  if (switch_edge) {
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "检测到槽位切换快捷键 (Alt + `)");
    ESP_LOGI(TAG, "============================================");

    // 如果 BLE 就绪，先发送一个空的键盘报告来释放所有按键（避免Alt卡住）
    if (ble_hid_send_is_ready()) {
      uint8_t empty_report[HID_KEYBOARD_IN_RPT_LEN] = {0};
      ble_hid_send_keyboard_report(empty_report, HID_KEYBOARD_IN_RPT_LEN);
    }

    esp_err_t ret = ble_device_manager_switch_slot();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "槽位切换失败: %s", esp_err_to_name(ret));
    }
    return; // 不发送此快捷键
  }

  // Alt + N : 发现新设备
  if (discover_edge) {
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "检测到发现新设备快捷键 (Alt + N)");
    ESP_LOGI(TAG, "============================================");

    // 如果 BLE 就绪，先发送一个空的键盘报告来释放所有按键（避免Alt卡住）
    if (ble_hid_send_is_ready()) {
      uint8_t empty_report[HID_KEYBOARD_IN_RPT_LEN] = {0};
      ble_hid_send_keyboard_report(empty_report, HID_KEYBOARD_IN_RPT_LEN);
    }

    esp_err_t ret = ble_device_manager_discover_new();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "发现新设备失败: %s", esp_err_to_name(ret));
    }
    return; // 不发送此快捷键
  }

  // Repeated held reports remain consumed, even though only the edge acts.
  if (switch_down || discover_down) return;

  // ========================================================================
  // BLE 状态检查 - 只影响普通键盘报告的发送
  // ========================================================================
  if (!ble_hid_send_is_ready()) {
    // 减少日志输出频率
    static uint32_t last_skip_log = 0;
    uint32_t now = xTaskGetTickCount();
    if (now - last_skip_log > pdMS_TO_TICKS(1000)) {
      ESP_LOGW(TAG, "BLE未就绪，跳过键盘报告发送");
      last_skip_log = now;
    }
    return;
  }

  // 发送键盘报告到BLE（使用线程安全接口）
  ESP_LOGD(TAG, "准备发送键盘报告: length=%d, data[0]=0x%02X",
           HID_KEYBOARD_IN_RPT_LEN, decoded[0]);
  esp_err_t ret = ble_hid_send_keyboard_report(decoded, HID_KEYBOARD_IN_RPT_LEN);
  if (ret != ESP_OK) {
    if (ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "发送键盘报告到BLE失败: %s", esp_err_to_name(ret));
    }
  } else {
    ESP_LOGD(TAG, "✓ 键盘报告已发送成功");
  }

  // 调试输出（仅在启用时）
#if defined(CONFIG_DEBUG_KEY_MOUSE_PRESS) && CONFIG_DEBUG_KEY_MOUSE_PRESS
  hid_keyboard_input_report_boot_t *kb_report =
      (hid_keyboard_input_report_boot_t *)decoded;
  if (kb_report->key[0] > 0 || kb_report->modifier.val > 0) {
    putchar('\n');
    if (kb_report->modifier.val > 0) {
      printf("Modifier: ");
      for (int i = 7; i >= 0; --i) {
        putchar((kb_report->modifier.val & (1 << i)) ? '1' : '0');
      }
      putchar('\n');
    }
    if (kb_report->key[0] > 0) {
      printf("Keys: ");
      putchar('\n');
      for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        printf("%02X ", kb_report->key[i]);
      }
      putchar('\n');
    }
  }
#endif
}
