#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_hidd_prf_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "driver/gpio.h"
#include "errno.h"
#include "usb/usb_host.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

#include "nvs_flash.h"

#include "ble_device_manager.h"
#include "ble_hid_callbacks.h"
#include "ble_hid_send.h"
#include "hid_config.h"
#include "hid_dev.h"
#include "hid_device_type_detector.h"
#include "hid_host_example.h"
#include "hid_report_parser_c.h"
#include "keyboard_handler.h"
#include "led_control.h"
#include "mouse_accumulator.h"
#include "usb_hid_types.h"
#include "usb_hid_recovery.h"

/* =================================================================================================
   MACROS
   =================================================================================================
 */
#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))

// 注意：HID 报告长度和配置选项已统一定义在 hid_config.h 中

/* =================================================================================================
   VARIABLES, STRUCTS, ENUMS
   =================================================================================================
 */

// BLE HID
uint16_t ble_hid_conn_id = 0;
bool sec_conn = false;

static const char *TAG_BLE = "BLE";

static uint8_t ble_hid_service_uuid128[] = {
    /* LSB
       <-------------------------------------------------------------------------------->
       MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t ble_hid_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, // slave connection min interval, Time =
                            // min_interval * 1.25 msec = 7.5ms
    .max_interval =
        0x0006, // slave connection max interval, Time = max_interval * 1.25
                // msec = 7.5ms (与min相同以获得固定间隔)
    .appearance = 0x03c0, // HID Generic,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(ble_hid_service_uuid128),
    .p_service_uuid = ble_hid_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t ble_hid_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// USB HOST HID
static const char *TAG_HID = "HID";
static const char *TAG_USB = "USB";

QueueHandle_t app_event_queue = NULL;

// 类型定义已移至 usb_hid_types.h

/**
 * @brief HID Protocol string names
 */
static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE",
};

app_event_queue_t evt_queue;

// USB HID 设备实例（类型定义在 usb_hid_types.h）
static usb_hid_devices_t usb_hid_devices = {0};

// Parsed layouts for the connected mouse reports (filled when descriptor is
// available)
static hid_report_layout_t g_mouse_layouts[MAX_MOUSE_LAYOUTS];
static int g_mouse_layout_count = 0;
static bool g_mouse_boot_protocol;
static uint8_t g_mouse_buttons;

// Recovery requests survive a full event queue. USB callbacks only enqueue;
// the application task performs control transfers and retries.
static portMUX_TYPE recovery_lock = portMUX_INITIALIZER_UNLOCKED;
static hid_host_device_handle_t recovery_handles[2];
static uint32_t recovery_generation[2];
static TickType_t recovery_retry_at[2];
static hid_host_device_handle_t deferred_close_handles[16];

// LED控制
led_strip_handle_t led_strip = NULL;

/* =================================================================================================
   辅助函数：供 mouse_accumulator 模块调用
   =================================================================================================
 */

/**
 * @brief 检查BLE是否已连接且可发送报告
 *
 * 在设备切换期间返回 false，避免发送报告
 */
bool mouse_accumulator_is_ble_connected(void) {
  return ble_hid_send_is_ready();
}

/**
 * @brief 通过BLE发送鼠标报告
 */
esp_err_t mouse_accumulator_send_ble_report(const uint8_t *report,
                                            uint8_t length, uint32_t generation) {
  return ble_hid_send_mouse_report_for_session(report, length, generation);
}

/* =================================================================================================
   FUNCTION PROTOTYPES
   =================================================================================================
 */

// BLE HID 回调函数已移至 ble_hid_callbacks.c/h

// USB HOST HID
void printBinary(uint8_t value);
// 键盘报告回调已移至 keyboard_handler.c/h
static void
hid_host_mouse_report_callback(hid_host_device_handle_t hid_device_handle,
                               uint8_t *data, int length);
void usb_hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_interface_event_t event,
                                     void *arg);
void usb_hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                               const hid_host_driver_event_t event, void *arg);
static void usb_lib_task(void *arg);
void usb_hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg);
static void print_usb_device_info(hid_host_device_handle_t hid_device_handle);

// LED控制辅助函数
static void update_led_color(void);

// BLE HID 回调函数实现已移至 ble_hid_callbacks.c

/* =================================================================================================
   USB HID HOST
   =================================================================================================
 */

/**
 * @brief Print binary value
 * @param[in] value  Value to print
 */
void printBinary(uint8_t value) {
  for (int i = 7; i >= 0; --i) { // Iterate over each bit (from MSB to LSB)
    putchar((value & (1 << i)) ? '1'
                               : '0'); // Print '1' if the bit is set, else '0'
  }
}

static int32_t clamp_axis(int32_t value, int32_t minimum, int32_t maximum) {
  return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void hid_host_mouse_report_callback(hid_host_device_handle_t handle,
                                           uint8_t *data, int length) {
  if (handle != usb_hid_devices.mouse_handle || data == NULL || length <= 0)
    return;
  hid_decoded_mouse_report_t report = {0};
  if (g_mouse_boot_protocol) {
    if (length != 3) return;
    report.buttons = data[0];
    report.has_buttons = true;
    report.x = (int8_t)data[1];
    report.y = (int8_t)data[2];
  } else if (!hid_decode_mouse_report(g_mouse_layouts, g_mouse_layout_count,
                                      data, (size_t)length, &report)) {
    return; // Unknown Report ID or incomplete packet must not become input.
  }
  if (report.has_buttons) g_mouse_buttons = report.buttons & 0x1f;
  mouse_accumulator_add((int16_t)clamp_axis(report.x, INT16_MIN, INT16_MAX),
                        (int16_t)clamp_axis(report.y, INT16_MIN, INT16_MAX),
                        (int8_t)clamp_axis(report.wheel, INT8_MIN, INT8_MAX),
                        g_mouse_buttons);
}

static void request_recovery(hid_host_device_handle_t handle) {
  portENTER_CRITICAL(&recovery_lock);
  int slot = -1;
  for (int i = 0; i < 2; i++)
    if (recovery_handles[i] == handle) slot = i;
  if (slot < 0) {
    for (int i = 0; i < 2; i++)
      if (recovery_handles[i] == NULL) { slot = i; break; }
  }
  if (slot >= 0) {
    recovery_handles[slot] = handle;
    recovery_generation[slot]++;
    recovery_retry_at[slot] = 0;
  }
  portEXIT_CRITICAL(&recovery_lock);
  app_event_queue_t event = {.event_group = APP_EVENT_HID_RECOVER};
  event.hid_host_device.handle = handle;
  // The pending table retains the request even if the wake-up queue is full.
  xQueueSend(app_event_queue, &event, 0);
}

static void defer_interface_close(hid_host_device_handle_t handle) {
  bool queued = false;
  portENTER_CRITICAL(&recovery_lock);
  for (int i = 0; i < 16; i++) {
    if (deferred_close_handles[i] == handle || deferred_close_handles[i] == NULL) {
      deferred_close_handles[i] = handle;
      queued = true;
      break;
    }
  }
  portEXIT_CRITICAL(&recovery_lock);
  if (!queued) ESP_LOGE(TAG_HID, "Deferred HID close table full");
}

static void close_disconnected_interfaces(void) {
  // Serialized with recovery on the app task: a disconnect callback cannot
  // free an interface record while stop/start is still using its handle.
  for (int i = 0; i < 16; i++) {
    portENTER_CRITICAL(&recovery_lock);
    hid_host_device_handle_t handle = deferred_close_handles[i];
    deferred_close_handles[i] = NULL;
    portEXIT_CRITICAL(&recovery_lock);
    if (handle != NULL) hid_host_device_close(handle);
  }
}

static void recover_pending_interfaces(void) {
  usb_hid_recovery_poll();
  for (int i = 0; i < 2; i++) {
    portENTER_CRITICAL(&recovery_lock);
    hid_host_device_handle_t handle = recovery_handles[i];
    uint32_t generation = recovery_generation[i];
    TickType_t retry_at = recovery_retry_at[i];
    portEXIT_CRITICAL(&recovery_lock);
    if (handle == NULL || (retry_at != 0 &&
        (int32_t)(xTaskGetTickCount() - retry_at) < 0)) continue;
    bool registered = handle == usb_hid_devices.keyboard_handle ||
                      handle == usb_hid_devices.mouse_handle;
    esp_err_t result = registered ? usb_hid_recover(handle) : ESP_ERR_NOT_FOUND;
    portENTER_CRITICAL(&recovery_lock);
    if (generation == recovery_generation[i]) {
      if (result == ESP_OK || !registered) recovery_handles[i] = NULL;
      else recovery_retry_at[i] = xTaskGetTickCount() + pdMS_TO_TICKS(250);
    }
    portEXIT_CRITICAL(&recovery_lock);
  }
}

/**
 * @brief USB HID Host interface callback
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host interface event
 * @param[in] arg                Pointer to arguments, does not used
 */
void usb_hid_host_interface_callback(hid_host_device_handle_t handle,
                                     const hid_host_interface_event_t event,
                                     void *arg) {
  (void)arg;
  switch (event) {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
    uint8_t data[64];
    size_t length = 0;
    if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data),
                                                 &length) != ESP_OK) return;
    if (handle == usb_hid_devices.keyboard_handle)
      hid_host_keyboard_report_callback(handle, data, (int)length);
    else if (handle == usb_hid_devices.mouse_handle)
      hid_host_mouse_report_callback(handle, data, (int)length);
    break;
  }
  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    if (handle == usb_hid_devices.keyboard_handle) {
      keyboard_handler_clear(handle);
      usb_hid_devices.keyboard_handle = NULL;
      uint8_t released[HID_KEYBOARD_IN_RPT_LEN] = {0};
      ble_hid_send_keyboard_report(released, sizeof(released));
    }
    if (handle == usb_hid_devices.mouse_handle) {
      usb_hid_devices.mouse_handle = NULL;
      g_mouse_layout_count = 0;
      g_mouse_boot_protocol = false;
      g_mouse_buttons = 0;
      mouse_accumulator_add(0, 0, 0, 0);
      mouse_accumulator_clear();
    }
    portENTER_CRITICAL(&recovery_lock);
    for (int i = 0; i < 2; i++) {
      if (recovery_handles[i] == handle) {
        recovery_handles[i] = NULL;
        recovery_generation[i]++;
      }
    }
    portEXIT_CRITICAL(&recovery_lock);
    // usb_host_hid 1.0.4 enters WAIT_USER_DELETION and requires a second
    // application close. Defer that close until app-task recovery has returned.
    defer_interface_close(handle);
    update_led_color();
    break;
  case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
    if (handle == usb_hid_devices.keyboard_handle ||
        handle == usb_hid_devices.mouse_handle) request_recovery(handle);
    break;
  default:
    ESP_LOGW(TAG_HID, "Unhandled HID interface event %d", event);
    break;
  }
}

/**
 * @brief Print USB device information
 *
 * @param[in] hid_device_handle  HID Device handle
 */
static void print_usb_device_info(hid_host_device_handle_t hid_device_handle) {
  hid_host_dev_params_t dev_params;
  esp_err_t ret = hid_host_device_get_params(hid_device_handle, &dev_params);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_USB, "Failed to get device params: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG_USB, "=========================================");
  ESP_LOGI(TAG_USB, "USB设备已连接");
  ESP_LOGI(TAG_USB, "  设备地址: %d", dev_params.addr);
  ESP_LOGI(TAG_USB, "  接口号: %d", dev_params.iface_num);
  ESP_LOGI(TAG_USB, "  HID子类: 0x%02X", dev_params.sub_class);
  ESP_LOGI(TAG_USB, "  HID协议: %d (%s)", dev_params.proto,
           (dev_params.proto < HID_PROTOCOL_MAX ? hid_proto_name_str[dev_params.proto] : "UNKNOWN"));
  ESP_LOGI(TAG_USB, "=========================================");
}

/**
 * @brief USB HID Host Device event
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host Device event
 * @param[in] arg                Pointer to arguments, (not used)
 */
void usb_hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                               const hid_host_driver_event_t event, void *arg) {
  (void)arg;
  if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;
  hid_host_dev_params_t params;
  if (hid_host_device_get_params(hid_device_handle, &params) != ESP_OK) return;
  print_usb_device_info(hid_device_handle);
  const hid_host_device_config_t config = {
      .callback = usb_hid_host_interface_callback, .callback_arg = NULL};
  if (hid_host_device_open(hid_device_handle, &config) != ESP_OK) return;

  size_t descriptor_length = 0;
  const uint8_t *descriptor = hid_host_get_report_descriptor(
      hid_device_handle, &descriptor_length);
  bool keyboard = false, mouse = false;
  bool detected = hid_device_type_detect(hid_device_handle, &keyboard, &mouse);
  if (!detected || (!keyboard && !mouse) || (keyboard && mouse)) {
    keyboard = params.proto == HID_PROTOCOL_KEYBOARD;
    mouse = params.proto == HID_PROTOCOL_MOUSE;
  }
  bool boot_capable = params.sub_class == HID_SUBCLASS_BOOT_INTERFACE;
  bool report_protocol = !boot_capable || hid_class_request_set_protocol(
      hid_device_handle, HID_REPORT_PROTOCOL_REPORT) == ESP_OK;
  bool configured = false;
  if (keyboard && usb_hid_devices.keyboard_handle == NULL) {
    if (report_protocol)
      configured = keyboard_handler_configure(hid_device_handle, descriptor,
                                               descriptor_length, false);
    if (!configured && boot_capable && params.proto == HID_PROTOCOL_KEYBOARD &&
        hid_class_request_set_protocol(hid_device_handle,
                                        HID_REPORT_PROTOCOL_BOOT) == ESP_OK)
      configured = keyboard_handler_configure(hid_device_handle, NULL, 0, true);
    if (configured) usb_hid_devices.keyboard_handle = hid_device_handle;
  } else if (mouse && usb_hid_devices.mouse_handle == NULL) {
    g_mouse_layout_count = report_protocol && descriptor ?
        parse_hid_report_descriptor_layouts(descriptor, descriptor_length,
                                            g_mouse_layouts, MAX_MOUSE_LAYOUTS) : 0;
    // The parser also tracks other report IDs; at least one actual axis is required.
    for (int i = 0; i < g_mouse_layout_count; i++)
      if (g_mouse_layouts[i].x_size || g_mouse_layouts[i].y_size) configured = true;
    g_mouse_boot_protocol = false;
    if (!configured && boot_capable && params.proto == HID_PROTOCOL_MOUSE &&
        hid_class_request_set_protocol(hid_device_handle,
                                        HID_REPORT_PROTOCOL_BOOT) == ESP_OK) {
      configured = true;
      g_mouse_boot_protocol = true;
    }
    if (configured) {
      g_mouse_buttons = 0;
      usb_hid_devices.mouse_handle = hid_device_handle;
    }
  }
  if (!configured) {
    ESP_LOGW(TAG_HID, "Unsupported or duplicate HID interface %u", params.iface_num);
    hid_host_device_close(hid_device_handle);
    return;
  }
  // SET_IDLE is optional for non-Boot devices. A STALL must not abort startup.
  hid_class_request_set_idle(hid_device_handle, 0, 0);
  esp_err_t result = hid_host_device_start(hid_device_handle);
  if (result != ESP_OK) {
    ESP_LOGE(TAG_HID, "HID interface start failed: %s", esp_err_to_name(result));
    if (usb_hid_devices.keyboard_handle == hid_device_handle) {
      keyboard_handler_clear(hid_device_handle);
      usb_hid_devices.keyboard_handle = NULL;
    }
    if (usb_hid_devices.mouse_handle == hid_device_handle) {
      usb_hid_devices.mouse_handle = NULL;
      g_mouse_layout_count = 0;
      g_mouse_boot_protocol = false;
      g_mouse_buttons = 0;
    }
    hid_host_device_close(hid_device_handle);
  }
  update_led_color();
}

/**
 * @brief Start USB Host install and handle common USB host library events while
 * app pin not low
 *
 * @param[in] arg  Not used
 */
static void usb_lib_task(void *arg) {
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  ESP_ERROR_CHECK(usb_host_install(&host_config));
  ESP_LOGI(TAG_USB, "USB Host库已初始化");
  xTaskNotifyGive(arg);

  ESP_LOGI(TAG_USB, "USB Host事件处理循环已启动");

  while (true) {
    uint32_t event_flags;
    esp_err_t ret = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG_USB, "usb_host_lib_handle_events failed: %s",
               esp_err_to_name(ret));
      // 如果发生错误，短暂延迟后继续，避免快速重试导致问题
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // 打印事件标志用于调试
    if (event_flags != 0) {
      ESP_LOGI(TAG_USB, "USB Host事件标志: 0x%08" PRIX32,
               (unsigned long)event_flags);
    }

    // In this example, there is only one client registered
    // So, once we deregister the client, this call must succeed with ESP_OK
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_LOGI(TAG_USB, "USB Host: 没有客户端注册，准备关闭");
      ESP_ERROR_CHECK(usb_host_device_free_all());
      break;
    }
  }

  ESP_LOGI(TAG_HID, "USB shutdown");
  // Clean up USB Host
  vTaskDelay(10); // Short delay to allow clients clean-up
  ESP_ERROR_CHECK(usb_host_uninstall());
  vTaskDelete(NULL);
}

/**
 * @brief HID Host Device callback
 *
 * Puts new HID Device event to the queue
 *
 * @param[in] hid_device_handle HID Device handle
 * @param[in] event             HID Device event
 * @param[in] arg               Not used
 */
void usb_hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg) {
  ESP_LOGI(TAG_USB, "HID Host设备回调被调用，事件类型: %d", event);

  const app_event_queue_t evt_queue = {.event_group = APP_EVENT_HID_HOST,
                                       // HID Host Device related info
                                       .hid_host_device.handle =
                                           hid_device_handle,
                                       .hid_host_device.event = event,
                                       .hid_host_device.arg = arg};

  if (app_event_queue) {
    BaseType_t ret = xQueueSend(app_event_queue, &evt_queue, 0);
    if (ret != pdTRUE) {
      ESP_LOGW(TAG_USB, "Failed to send event to queue (queue full?)");
    } else {
      ESP_LOGI(TAG_USB, "事件已加入队列");
    }
  } else {
    ESP_LOGE(TAG_USB, "事件队列未初始化！");
  }
}

/* =================================================================================================
   LED控制辅助函数
   =================================================================================================
 */

/**
 * @brief 更新LED颜色（根据当前连接状态）
 */
static void update_led_color(void) {
  if (led_strip == NULL) {
    return;
  }

  bool usb_keyboard_connected = (usb_hid_devices.keyboard_handle != NULL);
  bool usb_mouse_connected = (usb_hid_devices.mouse_handle != NULL);
  led_control_set_color(led_strip, usb_keyboard_connected, usb_mouse_connected,
                        sec_conn);
}

void app_main(void) {
  esp_err_t ret;

  // Initialize NVS.
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret) {
    ESP_LOGE(TAG_BLE, "%s initialize controller failed", __func__);
    return;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret) {
    ESP_LOGE(TAG_BLE, "%s enable controller failed", __func__);
    return;
  }

  ret = esp_bluedroid_init();
  if (ret) {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
    return;
  }

  ret = esp_bluedroid_enable();
  if (ret) {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
    return;
  }

  if ((ret = esp_hidd_profile_init()) != ESP_OK) {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
  }

  /* set the security iocap & auth_req & key size & init key response key
   * parameters to the stack*/
  esp_ble_auth_req_t auth_req =
      ESP_LE_AUTH_BOND; // bonding with peer device after authentication
  esp_ble_io_cap_t iocap =
      ESP_IO_CAP_NONE;   // set the IO capability to No output No input
  uint8_t key_size = 16; // the key size should be 7~16 bytes
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req,
                                 sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap,
                                 sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size,
                                 sizeof(uint8_t));
  /* If your BLE device act as a Slave, the init_key means you hope which types
  of key of the master should distribute to you, and the response key means
  which key you can distribute to the Master; If your BLE device act as a
  master, the response key means you hope which types of key of the slave should
  distribute to you, and the init key means which key you can distribute to the
  slave. */
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key,
                                 sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key,
                                 sizeof(uint8_t));

  // 初始化 BLE 设备管理器（双槽位切换支持）
  ret = ble_device_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_BLE, "BLE设备管理器初始化失败: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG_BLE, "BLE设备管理器初始化成功，当前活动槽位: %c",
             'A' + ble_device_manager_get_active_slot());
  }

  // 初始化 BLE HID 发送管理器（线程安全的发送接口）
  ret = ble_hid_send_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_BLE, "BLE HID发送管理器初始化失败: %s", esp_err_to_name(ret));
    return;
  }

  // Populate callback dependencies before registration can emit any events.
  led_strip = led_control_init();
  update_led_color();
  ESP_ERROR_CHECK(mouse_accumulator_init());
  ble_hid_callbacks_init(led_strip, &usb_hid_devices, &ble_hid_adv_data,
                         &ble_hid_adv_params);
  ble_hid_callbacks_set_led_callback(update_led_color);
  ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
  ESP_ERROR_CHECK(esp_hidd_register_callbacks(ble_hid_event_callback));

  app_event_queue = xQueueCreate(16, sizeof(app_event_queue_t));
  if (app_event_queue == NULL) {
    ESP_LOGE(TAG_HID, "Failed to create event queue");
    return;
  }

  BaseType_t task_created;
  ESP_LOGI(TAG_HID, "HID Host example");

  /*
   * Create usb_lib_task to:
   * - initialize USB Host library
   * - Handle USB Host events while APP pin in in HIGH state
   */
  task_created =
      xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096,
                              xTaskGetCurrentTaskHandle(), 2, NULL, 0);
  assert(task_created == pdTRUE);

  // Wait for notification from usb_lib_task to proceed
  ulTaskNotifyTake(false, portMAX_DELAY);
  ESP_ERROR_CHECK(usb_hid_recovery_init());

  /*
   * HID host driver configuration
   * - create background task for handling low level event inside the HID driver
   * - provide the device callback to get new HID Device connection event
   */
  const hid_host_driver_config_t hid_host_driver_config = {
      .create_background_task = true,
      .task_priority = 5,
      .stack_size = 4096,
      .core_id = 0,
      .callback = usb_hid_host_device_callback,
      .callback_arg = NULL};

  ret = hid_host_install(&hid_host_driver_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_HID, "Failed to install HID host driver: %s",
             esp_err_to_name(ret));
    return;
  }
  ESP_LOGI(TAG_HID, "HID Host驱动已安装");

  ESP_LOGI(TAG_HID, "等待USB HID设备连接...");
  ESP_LOGI(TAG_USB, "提示: 请插入USB键盘或鼠标设备");

  TickType_t last_heartbeat = xTaskGetTickCount();
  const TickType_t heartbeat_interval = pdMS_TO_TICKS(5000); // 5秒心跳

  while (1) {
    // Wait queue with timeout for heartbeat
    TickType_t timeout = pdMS_TO_TICKS(20);
    if (xQueueReceive(app_event_queue, &evt_queue, timeout)) {
      if (APP_EVENT_HID_HOST == evt_queue.event_group) {
        ESP_LOGI(TAG_USB, "收到HID Host事件，处理中...");
        usb_hid_host_device_event(evt_queue.hid_host_device.handle,
                                  evt_queue.hid_host_device.event,
                                  evt_queue.hid_host_device.arg);
      }
    }

    recover_pending_interfaces();
    close_disconnected_interfaces();

    // 心跳日志，确认程序在运行
    TickType_t now = xTaskGetTickCount();
    if ((now - last_heartbeat) >= heartbeat_interval) {
      ESP_LOGI(TAG_USB,
               "USB: 系统运行中，等待USB设备... (USB键盘: %s, USB鼠标: %s, BLE "
               "HID: %s)",
               usb_hid_devices.keyboard_handle != NULL ? "已连接" : "未连接",
               usb_hid_devices.mouse_handle != NULL ? "已连接" : "未连接",
               sec_conn ? "已连接" : "未连接");
      last_heartbeat = now;
    }
  }

  ESP_LOGI(TAG_HID, "HID Driver uninstall");
  ESP_ERROR_CHECK(hid_host_uninstall());
  xQueueReset(app_event_queue);
  vQueueDelete(app_event_queue);
}
