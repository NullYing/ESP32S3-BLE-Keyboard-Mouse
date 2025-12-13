#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt.h"

#include "usb/usb_host.h"
#include "errno.h"
#include "driver/gpio.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

#include "nvs_flash.h"

#include "hid_dev.h"
#include "hid_report_parser_c.h"
#include "hid_device_type_detector.h"
#include "hid_host_example.h"
#include "mouse_accumulator.h"

#include "led_strip.h"

/* =================================================================================================
   MACROS
   ================================================================================================= */
#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))
#define HID_KEYBOARD_IN_RPT_LEN 8
// 配置选项：使用16位精度（1）或8位精度（0）
// 注意：Report Map 中 X/Y 定义为 16bit 以兼容 8bit 和 16bit
// 如果 USE_16BIT_MOUSE_PRECISION=1，发送完整的 16bit 数据
// 如果 USE_16BIT_MOUSE_PRECISION=0，发送 8bit 数据但放在 16bit 字段中
// 注意：此宏必须与hid_device_le_prf.c和esp_hidd_prf_api.c中的定义保持一致
#define USE_16BIT_MOUSE_PRECISION 1

// 基于 Zephyr report map: 按钮(1字节: 3位按钮+5位padding) + X(2字节, 16bit) + Y(2字节, 16bit) + Wheel(1字节) = 6字节
// 注意：即使发送 8bit 数据，报告长度仍为 6 字节（8bit 数据放在 16bit 字段的低 8 位）
#define HID_MOUSE_IN_RPT_LEN 6 // 按钮(1) + X(2) + Y(2) + Wheel(1) = 6字节（兼容 8bit 和 16bit）
#define HID_CC_IN_RPT_LEN 2
#define BLE_HID_DEVICE_NAME "BLE HID"

// HID Report ID 相关常量
// 根据 HID 规范，Report ID 通常是 1 字节（8 位）
// Parser 返回的 bit_offset 是相对于报告数据开始（不包括 report_id）的
// 因此如果 report_id 存在，需要调整 8 位以跳过 report_id
#define HID_REPORT_ID_SIZE_BITS 8 // Report ID 的位宽（1 字节 = 8 位）

#define LED_GPIO_PIN 21
#define LED_RMT_RES_HZ (10 * 1000 * 1000)
#define LED_BRIGHTNESS 25

/* =================================================================================================
   VARIABLES, STRUCTS, ENUMS
   ================================================================================================= */

// BLE HID
static uint16_t ble_hid_conn_id = 0;
static bool sec_conn = false;

static const char *TAG_BLE = "BLE";

static uint8_t ble_hid_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    0x12,
    0x18,
    0x00,
    0x00,
};

static esp_ble_adv_data_t ble_hid_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, // slave connection min interval, Time = min_interval * 1.25 msec = 7.5ms
    .max_interval = 0x0006, // slave connection max interval, Time = max_interval * 1.25 msec = 7.5ms (与min相同以获得固定间隔)
    .appearance = 0x03c0,   // HID Generic,
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
static const char *TAG_KEYBOARD = "HID Keyboard";
static const char *TAG_MOUSE = "HID Mouse";
static const char *TAG_GENERIC = "HID Generic";
static const char *TAG_USB = "USB";

QueueHandle_t app_event_queue = NULL;

/**
 * @brief APP event group
 *
 * Application logic can be different. There is a one among other ways to distingiush the
 * event by application event group.
 * In this example we have two event groups:
 * APP_EVENT            - General event, which is APP_QUIT_PIN press event (Generally, it is IO0).
 * APP_EVENT_HID_HOST   - HID Host Driver event, such as device connection/disconnection or input report.
 */
typedef enum
{
  APP_EVENT = 0,
  APP_EVENT_HID_HOST
} app_event_group_t;

/**
 * @brief APP event queue
 *
 * This event is used for delivering the HID Host event from callback to a task.
 */
typedef struct
{
  app_event_group_t event_group;
  /* HID Host - Device related info */
  struct
  {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
  } hid_host_device;
} app_event_queue_t;

/**
 * @brief HID Protocol string names
 */
static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE",
};

app_event_queue_t evt_queue;

// USB HID设备管理（支持同时连接键盘和鼠标）
typedef struct
{
  hid_host_device_handle_t keyboard_handle;
  hid_host_device_handle_t mouse_handle;
} usb_hid_devices_t;

static usb_hid_devices_t usb_hid_devices = {0};

// Parsed layouts for the connected mouse reports (filled when descriptor is available)
#define MAX_MOUSE_LAYOUTS 16
static hid_report_layout_t g_mouse_layouts[MAX_MOUSE_LAYOUTS];
static int g_mouse_layout_count = 0;
// 缓存当前使用的layout指针，避免每次循环查找
static hid_report_layout_t *g_cached_layout = NULL;
static uint8_t g_cached_report_id = 0xFF; // 0xFF表示未缓存

// LED
led_strip_handle_t led_strip;

static const char *TAG_LED = "LED";

/* =================================================================================================
   辅助函数：供 mouse_accumulator 模块调用
   ================================================================================================= */

/**
 * @brief 检查BLE是否已连接
 */
bool mouse_accumulator_is_ble_connected(void)
{
  return sec_conn;
}

/**
 * @brief 通过BLE发送鼠标报告
 */
esp_err_t mouse_accumulator_send_ble_report(const uint8_t *report, uint8_t length)
{
  return hid_dev_send_report(hidd_le_env.gatt_if, ble_hid_conn_id,
                             HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT,
                             length, (uint8_t *)report);
}

/* =================================================================================================
   FUNCTION PROTOTYPES
   ================================================================================================= */

// BLE HID
static void ble_hid_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

// USB HOST HID
void printBinary(uint8_t value);
static void hid_host_keyboard_report_callback(hid_host_device_handle_t hid_device_handle, uint8_t *data, int length);
static void hid_host_mouse_report_callback(hid_host_device_handle_t hid_device_handle, uint8_t *data, int length);
static void hid_host_generic_report_callback(const uint8_t *const data, const int length);
void usb_hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_interface_event_t event,
                                     void *arg);
void usb_hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                               const hid_host_driver_event_t event,
                               void *arg);
static void usb_lib_task(void *arg);
void usb_hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg);
static void print_usb_device_info(hid_host_device_handle_t hid_device_handle);

// LED
led_strip_handle_t configure_led(void);
void set_led_color();

/* =================================================================================================
   BLE HID
   ================================================================================================= */

static void ble_hid_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
  switch (event)
  {
  case ESP_HIDD_EVENT_REG_FINISH:
  {
    if (param->init_finish.state == ESP_HIDD_INIT_OK)
    {
      // esp_bd_addr_t rand_addr = {0x04,0x11,0x11,0x11,0x11,0x05};
      esp_ble_gap_set_device_name(BLE_HID_DEVICE_NAME);
      esp_ble_gap_config_adv_data(&ble_hid_adv_data);
    }
    break;
  }
  case ESP_BAT_EVENT_REG:
  {
    break;
  }
  case ESP_HIDD_EVENT_DEINIT_FINISH:
    break;
  case ESP_HIDD_EVENT_BLE_CONNECT:
  {
    ESP_LOGI(TAG_BLE, "ESP_HID_EVENT_BLE_CONNECT");
    ble_hid_conn_id = param->connect.conn_id;

    // 更新BLE连接参数以提高回报率
    // min_interval = 0x0006 (7.5ms), max_interval = 0x0006 (7.5ms), latency = 0, timeout = 500 (625ms)
    // 这些参数请求更短的连接间隔以获得更高的回报率
    esp_ble_conn_update_params_t conn_params = {0};
    memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    conn_params.min_int = 0x0006; // 7.5ms (最小连接间隔)
    conn_params.max_int = 0x0006; // 7.5ms (最大连接间隔，与min相同以获得固定间隔)
    conn_params.latency = 0;      // 无延迟
    conn_params.timeout = 0x0320; // 800 * 1.25ms = 1000ms 超时
    esp_ble_gap_update_conn_params(&conn_params);
    ESP_LOGI(TAG_BLE, "BLE连接参数已更新: interval=7.5ms, latency=0");
    break;
  }
  case ESP_HIDD_EVENT_BLE_DISCONNECT:
  {
    sec_conn = false;
    ESP_LOGI(TAG_BLE, "ESP_HID_EVENT_BLE_DISCONNECT");

    // 清理鼠标累加器（避免断线重连后发送旧数据）
    mouse_accumulator_clear();

    esp_ble_gap_start_advertising(&ble_hid_adv_params);
    set_led_color();
    break;
  }
  case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT:
  {
    ESP_LOGI(TAG_BLE, "%s, ESP_HID_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
    ESP_LOG_BUFFER_HEX(TAG_BLE, param->vendor_write.data, param->vendor_write.length);
    break;
  }
  case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT:
  {
    ESP_LOGI(TAG_BLE, "ESP_HID_EVENT_BLE_LED_REPORT_WRITE_EVT");
    // 发送LED报告到键盘设备（如果已连接）
    if (usb_hid_devices.keyboard_handle)
    {
      ESP_ERROR_CHECK(hid_class_request_set_report(usb_hid_devices.keyboard_handle, HID_REPORT_TYPE_OUTPUT, 0, param->led_write.data, param->led_write.length));
    }
    ESP_LOG_BUFFER_HEX(TAG_BLE, param->led_write.data, param->led_write.length);
    printBinary(param->led_write.data[0]);
    break;
  }
  default:
    break;
  }
  return;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  switch (event)
  {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    esp_ble_gap_start_advertising(&ble_hid_adv_params);
    break;
  case ESP_GAP_BLE_SEC_REQ_EVT:
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
    {
      ESP_LOGD(TAG_BLE, "%x:", param->ble_security.ble_req.bd_addr[i]);
    }
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    break;
  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    sec_conn = true;
    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
    ESP_LOGI(TAG_BLE, "remote BD_ADDR: %08x%04x",
             (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
             (bd_addr[4] << 8) + bd_addr[5]);
    ESP_LOGI(TAG_BLE, "address type = %d", param->ble_security.auth_cmpl.addr_type);
    ESP_LOGI(TAG_BLE, "pair status = %s", param->ble_security.auth_cmpl.success ? "success" : "fail");
    if (!param->ble_security.auth_cmpl.success)
    {
      ESP_LOGE(TAG_BLE, "fail reason = 0x%x", param->ble_security.auth_cmpl.fail_reason);
    }
    else
    {
      set_led_color();
    }
    break;
  default:
    break;
  }
}

/* =================================================================================================
   USB HID HOST
   ================================================================================================= */

/**
 * @brief Print binary value
 * @param[in] value  Value to print
 */
void printBinary(uint8_t value)
{
  for (int i = 7; i >= 0; --i)
  {                                          // Iterate over each bit (from MSB to LSB)
    putchar((value & (1 << i)) ? '1' : '0'); // Print '1' if the bit is set, else '0'
  }
}

// Helper: extract unsigned bits (little-endian bit order) from a byte buffer
static uint32_t get_bits_u32(const uint8_t *data, int data_len, uint32_t bit_offset, uint32_t bit_size)
{
  if (bit_size == 0 || bit_size > 32)
    return 0;
  uint32_t value = 0;
  for (uint32_t i = 0; i < bit_size; i++)
  {
    uint32_t bit_index = bit_offset + i;
    uint32_t byte_index = bit_index / 8;
    uint32_t bit_in_byte = bit_index % 8;
    if ((int)byte_index >= data_len)
      break;
    uint8_t bit = (data[byte_index] >> bit_in_byte) & 0x1;
    value |= (bit << i);
  }
  return value;
}

// Helper: extract signed bits and sign-extend to 32-bit
static int32_t get_bits_s32(const uint8_t *data, int data_len, uint32_t bit_offset, uint32_t bit_size)
{
  uint32_t u = get_bits_u32(data, data_len, bit_offset, bit_size);
  if (bit_size == 0)
    return 0;
  uint32_t sign_bit = 1u << (bit_size - 1);
  if (u & sign_bit)
  {
    // sign extend
    uint32_t mask = (~0u) << bit_size;
    return (int32_t)(u | mask);
  }
  return (int32_t)u;
}

/**
 * @brief USB HID Host Keyboard Interface report callback handler
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] data    Pointer to input report data buffer
 * @param[in] length  Length of input report data buffer
 */
static void hid_host_keyboard_report_callback(hid_host_device_handle_t hid_device_handle, uint8_t *data, int length)
{

  hid_dev_send_report(hidd_le_env.gatt_if, ble_hid_conn_id, HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, HID_KEYBOARD_IN_RPT_LEN, data);

  hid_keyboard_input_report_boot_t *kb_report = (hid_keyboard_input_report_boot_t *)data;

  if (length < sizeof(hid_keyboard_input_report_boot_t))
  {
    return;
  }

  if (kb_report->key[0] > 0 || kb_report->modifier.val > 0)
  {
    putchar('\n');
    if (kb_report->modifier.val > 0)
    {
      printf("Modifier: ");
      printBinary(kb_report->modifier.val);
      putchar('\n');
    }
    if (kb_report->key[0] > 0)
    {
      printf("Keys: ");
      putchar('\n');
      for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++)
      {
        printf("%02X ", kb_report->key[i]);
      }
      putchar('\n');
    }
  }
}

/**
 * @brief USB HID Host Mouse Interface report callback handler
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] data    Pointer to input report data buffer
 * @param[in] length  Length of input report data buffer
 */
static void hid_host_mouse_report_callback(hid_host_device_handle_t hid_device_handle, uint8_t *data, int length)
{
  // USB Boot Protocol 鼠标报告格式：按钮(1字节) + X位移(1字节) + Y位移(1字节) = 3字节
  // USB Report Protocol 鼠标报告格式：长度可变，可能包含 Report ID
  //   常见格式1：按钮(1) + X(1) + Y(1) + 滚轮(1) = 4字节
  //   常见格式2：Report ID(1) + 按钮(1) + X(1) + Y(1) + 滚轮(1) + 其他(3) = 8字节（macOS常见）
  // BLE鼠标报告格式（基于 Zephyr report map，兼容 8bit 和 16bit）：
  //   按钮(1字节: 3位按钮+5位padding) + X(16位,2字节) + Y(16位,2字节) + Wheel(8位,1字节) = 6字节

  if (length < 3)
  {
    ESP_LOGW(TAG_MOUSE, "Mouse report too short: %d bytes (minimum 3)", length);
    return;
  }

  uint8_t buttons;
  int16_t x, y; // 改为16位以支持高精度
  int8_t wheel = 0;
  static uint8_t last_buttons = 0;
  buttons = last_buttons; // default to last known buttons state

  // 声明变量供后续打包使用
  uint32_t buttons_u = 0;                 // buttons的完整值（可能超过8位）
  hid_report_layout_t *use_layout = NULL; // 用于判断是否从use_layout路径获取数据

  // 调试日志已移除以提高鼠标回报率性能

  // 根据报告长度自动判断协议类型和格式
  if (length == sizeof(hid_mouse_input_report_boot_t))
  {
    // Boot Protocol 格式：3字节（按钮+X+Y）
    hid_mouse_input_report_boot_t *mouse_report = (hid_mouse_input_report_boot_t *)data;
    buttons = mouse_report->buttons.val;
    // 8位数据扩展为16位
    x = (int16_t)mouse_report->x_displacement;
    y = (int16_t)mouse_report->y_displacement;
    wheel = 0; // Boot Protocol 不支持滚轮
    ESP_LOGD(TAG_MOUSE, "Parsed as Boot Protocol (3 bytes)");
  }
  else if (length >= 5) // 支持基于 Zephyr report map 的5字节格式，以及其他更长的格式
  {
    // First, if we have parsed layouts, try to find one matching this packet (by Report ID or size)
    // use_layout已在函数开始处声明
    if (g_mouse_layout_count > 0)
    {
      uint8_t pid = data[0];

      // 尝试使用缓存的layout（性能优化）
      if (g_cached_layout != NULL && g_cached_report_id == pid)
      {
        // 验证缓存的layout仍然有效
        if ((uint32_t)length * 8 >= g_cached_layout->report_size_bits)
        {
          use_layout = g_cached_layout;
        }
        else
        {
          // 缓存失效，清除缓存
          g_cached_layout = NULL;
          g_cached_report_id = 0xFF;
        }
      }

      // 如果缓存未命中，进行查找
      if (!use_layout)
      {
        // try exact report_id match first
        for (int i = 0; i < g_mouse_layout_count; i++)
        {
          if (g_mouse_layouts[i].report_id != 0 && pid == g_mouse_layouts[i].report_id)
          {
            // ensure packet has enough bits
            if ((uint32_t)length * 8 >= g_mouse_layouts[i].report_size_bits)
            {
              use_layout = &g_mouse_layouts[i];
              // 缓存找到的layout
              g_cached_layout = use_layout;
              g_cached_report_id = pid;
              break;
            }
          }
        }
        // try report_id == 0 layouts (no report id)
        if (!use_layout)
        {
          for (int i = 0; i < g_mouse_layout_count; i++)
          {
            if (g_mouse_layouts[i].report_id == 0 && (uint32_t)length * 8 >= g_mouse_layouts[i].report_size_bits)
            {
              use_layout = &g_mouse_layouts[i];
              // 缓存找到的layout
              g_cached_layout = use_layout;
              g_cached_report_id = 0; // report_id为0的情况
              break;
            }
          }
        }
      }
    }

    if (use_layout)
    {
      // ========================================================================
      // Bit Offset 调整说明：
      // ========================================================================
      // HID Report Parser 返回的 bit_offset 是相对于报告数据开始计算的，
      // 不包括 report_id。因此：
      // - 如果 report_id 存在（非 0）：需要调整 bit_offset，跳过 report_id
      // - 如果 report_id 不存在（0）：不需要调整，bit_offset 从数据开始计算
      //
      // 根据 HID 规范，Report ID 通常是 1 字节（8 位）
      // 参考：hid_report_parser.c 中，当遇到新的 report_id 时，
      //       current_bit_offset 会被重置为 0（从报告数据开始）
      // ========================================================================
      uint32_t bit_offset_adjust = 0;
      if (use_layout->report_id != 0)
      {
        // Report ID 存在，需要跳过 report_id（1 字节 = 8 位）
        bit_offset_adjust = HID_REPORT_ID_SIZE_BITS;

        // 验证数据长度是否足够（至少包含 report_id + 最小数据）
        if (length < 2) // 至少需要 report_id(1) + 最小数据(1)
        {
          ESP_LOGW(TAG_MOUSE, "Report data too short: length=%d, expected at least 2 bytes (report_id + data)", length);
          return;
        }
      }
      // else: report_id == 0，不需要调整，bit_offset 从数据开始计算

      // ========================================================================
      // 调试：打印解析出的字段偏移量（仅打印一次）
      // ========================================================================
      static bool offset_printed = false;
      if (!offset_printed)
      {
        ESP_LOGI(TAG_MOUSE, "========== Parsed Mouse Layout Offsets ==========");
        ESP_LOGI(TAG_MOUSE, "Report ID: %u", use_layout->report_id);
        ESP_LOGI(TAG_MOUSE, "Bit offset adjust: %" PRIu32 " bits", bit_offset_adjust);
        ESP_LOGI(TAG_MOUSE, "Buttons: offset=%" PRIu32 " bits, count=%" PRIu32,
                 use_layout->buttons_bit_offset + bit_offset_adjust, use_layout->buttons_count);
        ESP_LOGI(TAG_MOUSE, "X: offset=%" PRIu32 " bits, size=%" PRIu32 " bits",
                 use_layout->x_bit_offset + bit_offset_adjust, use_layout->x_size);
        ESP_LOGI(TAG_MOUSE, "Y: offset=%" PRIu32 " bits, size=%" PRIu32 " bits",
                 use_layout->y_bit_offset + bit_offset_adjust, use_layout->y_size);
        ESP_LOGI(TAG_MOUSE, "Wheel: offset=%" PRIu32 " bits, size=%" PRIu32 " bits",
                 use_layout->wheel_bit_offset + bit_offset_adjust, use_layout->wheel_size);
        ESP_LOGI(TAG_MOUSE, "================================================");
        offset_printed = true;
      }

      // ========================================================================
      // Buttons 数据提取（参考 asterics 仓库逻辑）
      // ========================================================================
      // 直接提取完整的 buttons 值，不进行中间转换
      // 在打包时直接使用 buttons_u & 0x07 获取低3位
      // ========================================================================
      buttons_u = get_bits_u32(data, length, use_layout->buttons_bit_offset + bit_offset_adjust, use_layout->buttons_count);
      int32_t x_raw = use_layout->x_size ? get_bits_s32(data, length, use_layout->x_bit_offset + bit_offset_adjust, use_layout->x_size) : 0;
      int32_t y_raw = use_layout->y_size ? get_bits_s32(data, length, use_layout->y_bit_offset + bit_offset_adjust, use_layout->y_size) : 0;
      int32_t wheel_raw = use_layout->wheel_size ? get_bits_s32(data, length, use_layout->wheel_bit_offset + bit_offset_adjust, use_layout->wheel_size) : 0;

      // ========================================================================
      // X/Y/Wheel 数据转换（参考 asterics 仓库逻辑）
      // ========================================================================
      // 依赖类型转换自动处理，不进行显式 clamp
      // get_bits_s32() 已经进行了符号扩展，直接转换为目标类型即可
      // 类型转换会自动处理溢出（截断到目标类型的范围）
      // ========================================================================
      x = use_layout->x_size ? (int16_t)x_raw : 0;
      y = use_layout->y_size ? (int16_t)y_raw : 0;
      wheel = use_layout->wheel_size ? (int8_t)wheel_raw : 0;

      // 调试日志已禁用以提高鼠标回报率性能
      // if (x_raw != 0 || y_raw != 0 || wheel_raw != 0)
      // {
      //   ESP_LOGI(TAG_MOUSE, "[USB->BLE 数据提取] x_raw=%d -> x=%d, y_raw=%d -> y=%d, wheel_raw=%d -> wheel=%d",
      //            (int)x_raw, (int)x, (int)y_raw, (int)y, (int)wheel_raw, (int)wheel);
      // }

      // 注意：pan（水平滚动）数据在 BLE 报告中不被使用，因此不提取
      // 参考 asterics 仓库逻辑，不处理 pan 数据
    }
    else
    {
      // ========================================================================
      // 回退逻辑：基本的固定偏移解析（参考 asterics 仓库逻辑）
      // ========================================================================
      // 简化回退逻辑，只支持基本的固定偏移解析
      // 不支持 12 位数据的特殊处理，统一使用 8 位数据解析
      // ========================================================================
      if (data[0] > 0 && data[0] <= 0x0F)
      {
        // 包含 Report ID 的格式：Report ID(1) + Buttons(1) + X(1) + Y(1) + Wheel(1)
        if (length >= 5)
        {
          buttons = data[1];
          x = (int16_t)(int8_t)data[2]; // 8位数据扩展为16位
          y = (int16_t)(int8_t)data[3]; // 8位数据扩展为16位
          wheel = (int8_t)data[4];
        }
        else
        {
          ESP_LOGW(TAG_MOUSE, "Report with ID 0x%02X too short: length=%d, expected at least 5 bytes", data[0], length);
          return;
        }
      }
      else
      {
        // 不包含 Report ID 的格式：Buttons(1) + X(1) + Y(1) + Wheel(1)
        if (length >= 4)
        {
          buttons = data[0];
          x = (int16_t)(int8_t)data[1]; // 8位数据扩展为16位
          y = (int16_t)(int8_t)data[2]; // 8位数据扩展为16位
          wheel = (int8_t)data[3];
        }
        else
        {
          ESP_LOGW(TAG_MOUSE, "Report without ID too short: length=%d, expected at least 4 bytes", length);
          return;
        }
      }
    }
  }
  else
  {
    // ========================================================================
    // 其他长度的 Report Protocol 格式处理（参考 asterics 仓库逻辑）
    // ========================================================================
    // 简化处理逻辑，统一使用基本的固定偏移解析
    // ========================================================================
    if (length > 3 && data[0] > 0 && data[0] <= 0x0F)
    {
      // 包含 Report ID 的格式：Report ID(1) + Buttons(1) + X(1) + Y(1) + Wheel(1)
      if (length >= 5)
      {
        buttons = data[1];
        x = (int16_t)(int8_t)data[2]; // 8位数据扩展为16位
        y = (int16_t)(int8_t)data[3]; // 8位数据扩展为16位
        wheel = (int8_t)data[4];
      }
      else if (length >= 4)
      {
        // 至少包含 Report ID + Buttons + X + Y
        buttons = data[1];
        x = (int16_t)(int8_t)data[2];
        y = (int16_t)(int8_t)data[3];
        wheel = 0; // 无滚轮数据
      }
      else
      {
        ESP_LOGW(TAG_MOUSE, "Report Protocol with ID 0x%02X too short: length=%d, expected at least 4 bytes", data[0], length);
        return;
      }
    }
    else
    {
      // 不包含 Report ID 的格式：Buttons(1) + X(1) + Y(1) + Wheel(1)
      if (length >= 4)
      {
        buttons = data[0];
        x = (int16_t)(int8_t)data[1]; // 8位数据扩展为16位
        y = (int16_t)(int8_t)data[2]; // 8位数据扩展为16位
        wheel = (int8_t)data[3];
      }
      else if (length >= 3)
      {
        // 至少包含 Buttons + X + Y
        buttons = data[0];
        x = (int16_t)(int8_t)data[1];
        y = (int16_t)(int8_t)data[2];
        wheel = 0; // 无滚轮数据
      }
      else
      {
        ESP_LOGW(TAG_MOUSE, "Report Protocol without ID too short: length=%d, expected at least 3 bytes", length);
        return;
      }
    }
  }

  // ========================================================================
  // 新方案：只累加数据到累加器，不直接发送BLE
  // BLE发送由定时器节拍触发，实现USB输入和BLE发送的彻底解耦
  // ========================================================================

  // 确定最终的按钮值（取低3位）
  uint8_t buttons_final;
  if (use_layout != NULL && use_layout->buttons_count > 0)
  {
    // 从 use_layout 路径：直接使用 buttons_u 的低3位
    buttons_final = (uint8_t)(buttons_u & 0x07);
  }
  else
  {
    // 回退路径：使用 buttons 的低3位
    buttons_final = buttons & 0x07;
  }

  // 累加到全局累加器（线程安全）
  mouse_accumulator_add(x, y, wheel, buttons_final);

  // 保存按钮状态供下次比较
  last_buttons = buttons_final;

  // 注释：原来的直接发送代码已移除
  // 现在所有的BLE发送都在 ble_try_send_mouse() 中完成
  // 该函数由定时器按固定节拍调用，确保发送速率稳定
}

/**
 * @brief USB HID Host Generic Interface report callback handler
 *
 * 'generic' means anything else than mouse or keyboard
 *
 * @param[in] data    Pointer to input report data buffer
 * @param[in] length  Length of input report data buffer
 */
static void hid_host_generic_report_callback(const uint8_t *const data, const int length)
{
  int report_length_without_report_id = length - 1;
  if (report_length_without_report_id <= 2)
  {
    uint8_t report_data_without_report_id[2] = {0, 0};
    memcpy(report_data_without_report_id, &data[1], report_length_without_report_id);
    printf("Maybe Consumer Report\n");
    hid_dev_send_report(hidd_le_env.gatt_if, ble_hid_conn_id, HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT, report_length_without_report_id, report_data_without_report_id);
  }
  for (int i = 0; i < length; i++)
  {
    printf("%02X ", data[i]);
  }
  putchar('\n');
}

/**
 * @brief USB HID Host interface callback
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host interface event
 * @param[in] arg                Pointer to arguments, does not used
 */
void usb_hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_interface_event_t event,
                                     void *arg)
{
  uint8_t data[64] = {0};
  size_t data_length = 0;
  hid_host_dev_params_t dev_params;
  ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

  // putchar('\n');
  // ESP_LOGI(TAG_HID, "Interface: %d", dev_params.iface_num);

  // size_t report_desc_len = 0;
  // uint8_t *report_desc = hid_host_get_report_descriptor(hid_device_handle, &report_desc_len);

  // putchar('\n');
  // ESP_LOGI(TAG_HID, "Report descriptor:");
  // for (size_t i = 0; i < report_desc_len; i++)
  // {
  //   printf("%02X ", report_desc[i]);
  // }
  // putchar('\n');

  switch (event)
  {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
    ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                              data,
                                                              64,
                                                              &data_length));

    // 根据协议类型和报告长度自动判断协议模式
    // Boot Protocol 鼠标：3字节（按钮+X+Y）
    // Boot Protocol 键盘：8字节（修饰键+保留+6个按键）
    // Report Protocol：长度可变，通常>=4字节

    if (HID_PROTOCOL_KEYBOARD == dev_params.proto)
    {
      // 键盘：Boot Protocol 固定8字节，Report Protocol 可能不同长度
      if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class && data_length == 8)
      {
        ESP_LOGI(TAG_KEYBOARD, "Keyboard Event (Boot Protocol, len=%d)", data_length);
      }
      else
      {
        ESP_LOGI(TAG_KEYBOARD, "Keyboard Event (Report Protocol, len=%d)", data_length);
      }
      hid_host_keyboard_report_callback(hid_device_handle, data, data_length);
    }
    else if (HID_PROTOCOL_MOUSE == dev_params.proto)
    {
      // 鼠标：根据报告长度自动判断协议类型
      // Boot Protocol: 3字节（按钮+X+Y）
      // Report Protocol: 4字节或更多（可能包含滚轮、额外按钮等）

      // 已禁用日志以提高性能
      // bool is_boot_protocol = (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class && data_length == 3);
      // if (is_boot_protocol)
      // {
      //   ESP_LOGD(TAG_MOUSE, "Mouse Event (Boot Protocol, len=%d)", data_length);
      // }
      // else
      // {
      //   ESP_LOGD(TAG_MOUSE, "Mouse Event (Report Protocol, len=%d)", data_length);
      // }
      hid_host_mouse_report_callback(hid_device_handle, data, data_length);
    }
    else
    {
      // 其他协议类型
      if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class)
      {
        ESP_LOGI(TAG_GENERIC, "Generic Boot Interface Event (len=%d)", data_length);
      }
      else
      {
        ESP_LOGI(TAG_GENERIC, "Generic Event (Report Protocol, len=%d)", data_length);
      }
      hid_host_generic_report_callback(data, data_length);
    }

    break;
  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    ESP_LOGI(TAG_USB, "=========================================");
    ESP_LOGI(TAG_USB, "USB HID接口已断开");
    ESP_LOGI(TAG_USB, "  设备地址: %d", dev_params.addr);
    ESP_LOGI(TAG_USB, "  接口号: %d", dev_params.iface_num);
    ESP_LOGI(TAG_USB, "  协议: %s", hid_proto_name_str[dev_params.proto]);
    ESP_LOGI(TAG_USB, "=========================================");
    ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));

    // 从设备列表中移除对应的设备
    if (dev_params.proto == HID_PROTOCOL_KEYBOARD)
    {
      usb_hid_devices.keyboard_handle = NULL;
    }
    else if (dev_params.proto == HID_PROTOCOL_MOUSE)
    {
      usb_hid_devices.mouse_handle = NULL;
      // 清除layout缓存
      g_cached_layout = NULL;
      g_cached_report_id = 0xFF;
    }

    set_led_color();
    break;
  case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
    ESP_LOGI(TAG_HID, "HID Device, interface %d protocol '%s' TRANSFER_ERROR",
             dev_params.iface_num, hid_proto_name_str[dev_params.proto]);
    break;
  default:
    ESP_LOGE(TAG_HID, "HID Device, interface %d protocol '%s' Unhandled event",
             dev_params.iface_num, hid_proto_name_str[dev_params.proto]);
    break;
  }
}

/**
 * @brief Print USB device information
 *
 * @param[in] hid_device_handle  HID Device handle
 */
static void print_usb_device_info(hid_host_device_handle_t hid_device_handle)
{
  hid_host_dev_params_t dev_params;
  esp_err_t ret = hid_host_device_get_params(hid_device_handle, &dev_params);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_USB, "Failed to get device params: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG_USB, "=========================================");
  ESP_LOGI(TAG_USB, "USB设备已连接");
  ESP_LOGI(TAG_USB, "  设备地址: %d", dev_params.addr);
  ESP_LOGI(TAG_USB, "  接口号: %d", dev_params.iface_num);
  ESP_LOGI(TAG_USB, "  HID子类: 0x%02X", dev_params.sub_class);
  ESP_LOGI(TAG_USB, "  HID协议: %d (%s)", dev_params.proto, hid_proto_name_str[dev_params.proto]);
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
                               const hid_host_driver_event_t event,
                               void *arg)
{
  hid_host_dev_params_t dev_params;
  ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

  switch (event)
  {
  case HID_HOST_DRIVER_EVENT_CONNECTED:
    ESP_LOGI(TAG_HID, "HID Device Connected");
    print_usb_device_info(hid_device_handle);
    printf("address: %d, interface: %d, subclass: %d, protocol: %d %s\n",
           dev_params.addr, dev_params.iface_num, dev_params.sub_class, dev_params.proto, hid_proto_name_str[dev_params.proto]);

    const hid_host_device_config_t dev_config = {
        .callback = usb_hid_host_interface_callback,
        .callback_arg = NULL};

    ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

    // macOS使用Report Protocol，对所有Boot Interface设备都设置为Report Protocol
    if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class)
    {
      // 强制使用Report Protocol（macOS兼容）
      ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_REPORT));
      ESP_LOGI(TAG_HID, "已设置USB设备为Report Protocol模式（macOS兼容）");
    }
    // 非Boot Interface设备默认使用Report Protocol，无需设置

    // 根据协议类型和Report Descriptor验证设备类型
    // 优先使用Report Descriptor检测，因为它更准确
    bool is_keyboard_from_desc = false;
    bool is_mouse_from_desc = false;
    bool desc_check_success = hid_device_type_detect(hid_device_handle, &is_keyboard_from_desc, &is_mouse_from_desc);

    // 决定设备类型：优先使用Descriptor检测结果，如果失败则回退到协议字段
    bool should_register_as_keyboard = false;
    bool should_register_as_mouse = false;

    if (desc_check_success)
    {
      // 使用Descriptor检测结果
      should_register_as_keyboard = is_keyboard_from_desc && !is_mouse_from_desc;
      should_register_as_mouse = is_mouse_from_desc;

      if (is_keyboard_from_desc && is_mouse_from_desc)
      {
        // 如果同时检测到键盘和鼠标，优先使用协议字段
        ESP_LOGW(TAG_HID, "设备同时包含键盘和鼠标功能，使用协议字段判断");
        should_register_as_keyboard = (HID_PROTOCOL_KEYBOARD == dev_params.proto);
        should_register_as_mouse = (HID_PROTOCOL_MOUSE == dev_params.proto);
      }
    }
    else
    {
      // Descriptor检测失败，回退到协议字段
      should_register_as_keyboard = (HID_PROTOCOL_KEYBOARD == dev_params.proto);
      should_register_as_mouse = (HID_PROTOCOL_MOUSE == dev_params.proto);
    }

    // 如果Descriptor检测结果与协议字段不一致，记录警告
    if (desc_check_success)
    {
      if (is_mouse_from_desc && HID_PROTOCOL_KEYBOARD == dev_params.proto)
      {
        ESP_LOGW(TAG_HID, "警告：协议字段显示为键盘，但Report Descriptor检测为鼠标，将注册为鼠标");
        should_register_as_keyboard = false;
        should_register_as_mouse = true;
      }
      else if (is_keyboard_from_desc && HID_PROTOCOL_MOUSE == dev_params.proto)
      {
        ESP_LOGW(TAG_HID, "警告：协议字段显示为鼠标，但Report Descriptor检测为键盘，将注册为键盘");
        should_register_as_keyboard = true;
        should_register_as_mouse = false;
      }
    }

    // 注册设备
    if (should_register_as_keyboard)
    {
      ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
      // 保存键盘设备句柄
      usb_hid_devices.keyboard_handle = hid_device_handle;
      ESP_LOGI(TAG_KEYBOARD, "键盘设备已注册");
    }
    else if (should_register_as_mouse)
    {
      ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
      // 保存鼠标设备句柄
      usb_hid_devices.mouse_handle = hid_device_handle;
      ESP_LOGI(TAG_MOUSE, "鼠标设备已注册");

      // 获取并打印鼠标的 HID Report Descriptor
      size_t report_desc_len = 0;
      const uint8_t *report_desc = hid_host_get_report_descriptor(hid_device_handle, &report_desc_len);

      if (report_desc != NULL && report_desc_len > 0)
      {

        if (report_desc_len % 16 != 0)
        {
          putchar('\n');
        }
        ESP_LOGI(TAG_MOUSE, "=========================================");

        // 解析并生成简单的report layout以便后续自动解析数据
        g_mouse_layout_count = parse_hid_report_descriptor_layouts(report_desc, report_desc_len, g_mouse_layouts, MAX_MOUSE_LAYOUTS);
        if (g_mouse_layout_count > 0)
        {
          for (int i = 0; i < g_mouse_layout_count; i++)
          {
            hid_report_layout_t *l = &g_mouse_layouts[i];
            ESP_LOGI(TAG_MOUSE, "Parsed mouse layout[%d]: report_id=%u, buttons=%u, buttons_bit_offset=%u, x: bit=%u size=%u, y: bit=%u size=%u, wheel: bit=%u size=%u, pan: bit=%u size=%u",
                     i,
                     (unsigned int)l->report_id,
                     (unsigned int)l->buttons_count,
                     (unsigned int)l->buttons_bit_offset,
                     (unsigned int)l->x_bit_offset, (unsigned int)l->x_size,
                     (unsigned int)l->y_bit_offset, (unsigned int)l->y_size,
                     (unsigned int)l->wheel_bit_offset, (unsigned int)l->wheel_size,
                     (unsigned int)l->pan_bit_offset, (unsigned int)l->pan_size);
          }
        }
        else
        {
          ESP_LOGW(TAG_MOUSE, "未能解析到鼠标布局，仍将使用默认/兼容解析逻辑");
        }
        // Also run single-layout parser to show what the simpler heuristic returns
        {
          hid_report_layout_t single_layout = {0};
          int r = parse_hid_report_descriptor_layout(report_desc, report_desc_len, &single_layout);
          if (r == 0)
          {
            ESP_LOGI(TAG_MOUSE, "parse_hid_report_descriptor_layout -> SUCCESS: report_id=%u, buttons=%u, buttons_bit_offset=%u, x: bit=%u size=%u, y: bit=%u size=%u, wheel: bit=%u size=%u, pan: bit=%u size=%u, report_size_bits=%u",
                     (unsigned int)single_layout.report_id,
                     (unsigned int)single_layout.buttons_count,
                     (unsigned int)single_layout.buttons_bit_offset,
                     (unsigned int)single_layout.x_bit_offset, (unsigned int)single_layout.x_size,
                     (unsigned int)single_layout.y_bit_offset, (unsigned int)single_layout.y_size,
                     (unsigned int)single_layout.wheel_bit_offset, (unsigned int)single_layout.wheel_size,
                     (unsigned int)single_layout.pan_bit_offset, (unsigned int)single_layout.pan_size,
                     (unsigned int)single_layout.report_size_bits);
          }
          else
          {
            ESP_LOGW(TAG_MOUSE, "parse_hid_report_descriptor_layout -> no suitable mouse-like report found, first fallback layout: report_id=%u, buttons=%u, x_size=%u, y_size=%u, wheel_size=%u, pan_size=%u, report_size_bits=%u",
                     (unsigned int)single_layout.report_id,
                     (unsigned int)single_layout.buttons_count,
                     (unsigned int)single_layout.x_size,
                     (unsigned int)single_layout.y_size,
                     (unsigned int)single_layout.wheel_size,
                     (unsigned int)single_layout.pan_size,
                     (unsigned int)single_layout.report_size_bits);
          }
        }
      }
      else
      {
        ESP_LOGW(TAG_MOUSE, "无法获取 HID Report Descriptor (长度: %zu)", report_desc_len);
      }
    }

    ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
    set_led_color();
    break;
  default:
    break;
  }
}

/**
 * @brief Start USB Host install and handle common USB host library events while app pin not low
 *
 * @param[in] arg  Not used
 */
static void usb_lib_task(void *arg)
{
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  ESP_ERROR_CHECK(usb_host_install(&host_config));
  ESP_LOGI(TAG_USB, "USB Host库已初始化");
  xTaskNotifyGive(arg);

  ESP_LOGI(TAG_USB, "USB Host事件处理循环已启动");

  while (true)
  {
    uint32_t event_flags;
    esp_err_t ret = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (ret != ESP_OK)
    {
      ESP_LOGE(TAG_USB, "usb_host_lib_handle_events failed: %s", esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // 打印事件标志用于调试
    if (event_flags != 0)
    {
      ESP_LOGI(TAG_USB, "USB Host事件标志: 0x%08" PRIX32, (unsigned long)event_flags);
    }

    // In this example, there is only one client registered
    // So, once we deregister the client, this call must succeed with ESP_OK
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
    {
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
                                  void *arg)
{
  ESP_LOGI(TAG_USB, "HID Host设备回调被调用，事件类型: %d", event);

  const app_event_queue_t evt_queue = {
      .event_group = APP_EVENT_HID_HOST,
      // HID Host Device related info
      .hid_host_device.handle = hid_device_handle,
      .hid_host_device.event = event,
      .hid_host_device.arg = arg};

  if (app_event_queue)
  {
    BaseType_t ret = xQueueSend(app_event_queue, &evt_queue, 0);
    if (ret != pdTRUE)
    {
      ESP_LOGW(TAG_USB, "Failed to send event to queue (queue full?)");
    }
    else
    {
      ESP_LOGI(TAG_USB, "事件已加入队列");
    }
  }
  else
  {
    ESP_LOGE(TAG_USB, "事件队列未初始化！");
  }
}

/* =================================================================================================
   LED
   ================================================================================================= */

led_strip_handle_t configure_led(void)
{
  // LED strip general initialization, according to your led board design
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_GPIO_PIN,                              // The GPIO that connected to the LED strip's data line
      .max_leds = 1,                                               // The number of LEDs in the strip,
      .led_model = LED_MODEL_WS2812,                               // LED strip model
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB, // The color order of the strip: GRB
      .flags = {
          .invert_out = false, // don't invert the output signal
      }};

  // LED strip backend configuration: RMT
  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,  // different clock source can lead to different power consumption
      .resolution_hz = LED_RMT_RES_HZ, // RMT counter clock frequency
      .mem_block_symbols = 64,         // the memory size of each RMT channel, in words (4 bytes)
      .flags = {
          .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
      }};

  // LED Strip object handle
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  ESP_ERROR_CHECK(led_strip_clear(led_strip));
  ESP_LOGI(TAG_LED, "Created LED strip object with RMT backend");
  return led_strip;
}

/**
 * @brief Set LED color based on USB and BLE HID connection status
 */
void set_led_color()
{
  bool usb_device_connected = (usb_hid_devices.keyboard_handle != NULL || usb_hid_devices.mouse_handle != NULL);
  printf("USB HID: %s (键盘:%s, 鼠标:%s), BLE HID: %s\n",
         usb_device_connected ? "已连接" : "未连接",
         usb_hid_devices.keyboard_handle != NULL ? "是" : "否",
         usb_hid_devices.mouse_handle != NULL ? "是" : "否",
         sec_conn ? "已连接" : "未连接");

  if (usb_device_connected && sec_conn)
  {
    // USB设备已连接且BLE已连接 - 白色
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, LED_BRIGHTNESS, LED_BRIGHTNESS, LED_BRIGHTNESS));
  }
  else if (usb_device_connected)
  {
    // USB设备已连接但BLE未连接 - 绿色
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, LED_BRIGHTNESS, 0));
  }
  else if (sec_conn)
  {
    // BLE已连接但USB设备未连接 - 蓝色
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, LED_BRIGHTNESS));
  }
  else
  {
    // 都未连接 - 红色
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, LED_BRIGHTNESS, 0, 0));
  }
  ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

void app_main(void)
{
  esp_err_t ret;

  // Initialize NVS.
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret)
  {
    ESP_LOGE(TAG_BLE, "%s initialize controller failed", __func__);
    return;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret)
  {
    ESP_LOGE(TAG_BLE, "%s enable controller failed", __func__);
    return;
  }

  ret = esp_bluedroid_init();
  if (ret)
  {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
    return;
  }

  ret = esp_bluedroid_enable();
  if (ret)
  {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
    return;
  }

  if ((ret = esp_hidd_profile_init()) != ESP_OK)
  {
    ESP_LOGE(TAG_BLE, "%s init bluedroid failed", __func__);
  }

  /// register the callback function to the gap module
  esp_ble_gap_register_callback(gap_event_handler);
  esp_hidd_register_callbacks(ble_hid_event_callback);

  /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND; // bonding with peer device after authentication
  esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;       // set the IO capability to No output No input
  uint8_t key_size = 16;                          // the key size should be 7~16 bytes
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
  /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
  and the response key means which key you can distribute to the Master;
  If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
  and the init key means which key you can distribute to the slave. */
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

  BaseType_t task_created;
  ESP_LOGI(TAG_HID, "HID Host example");

  /*
   * Create usb_lib_task to:
   * - initialize USB Host library
   * - Handle USB Host events while APP pin in in HIGH state
   */
  task_created = xTaskCreatePinnedToCore(usb_lib_task,
                                         "usb_events",
                                         4096,
                                         xTaskGetCurrentTaskHandle(),
                                         2, NULL, 0);
  assert(task_created == pdTRUE);

  // Wait for notification from usb_lib_task to proceed
  ulTaskNotifyTake(false, 1000);

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
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG_HID, "Failed to install HID host driver: %s", esp_err_to_name(ret));
    return;
  }
  ESP_LOGI(TAG_HID, "HID Host驱动已安装");

  // Create queue
  app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));
  if (app_event_queue == NULL)
  {
    ESP_LOGE(TAG_HID, "Failed to create event queue");
    return;
  }

  ESP_LOGI(TAG_HID, "等待USB HID设备连接...");
  ESP_LOGI(TAG_USB, "提示: 请插入USB键盘或鼠标设备");

  led_strip = configure_led();
  set_led_color();

  // 初始化鼠标累加器模块（创建BLE发送定时器）
  ESP_ERROR_CHECK(mouse_accumulator_init());

  TickType_t last_heartbeat = xTaskGetTickCount();
  const TickType_t heartbeat_interval = pdMS_TO_TICKS(5000); // 5秒心跳

  while (1)
  {
    // Wait queue with timeout for heartbeat
    TickType_t timeout = pdMS_TO_TICKS(1000); // 1秒超时
    if (xQueueReceive(app_event_queue, &evt_queue, timeout))
    {
      if (APP_EVENT_HID_HOST == evt_queue.event_group)
      {
        ESP_LOGI(TAG_USB, "收到HID Host事件，处理中...");
        usb_hid_host_device_event(evt_queue.hid_host_device.handle,
                                  evt_queue.hid_host_device.event,
                                  evt_queue.hid_host_device.arg);
      }
    }

    // 心跳日志，确认程序在运行
    TickType_t now = xTaskGetTickCount();
    if ((now - last_heartbeat) >= heartbeat_interval)
    {
      ESP_LOGI(TAG_USB, "USB: 系统运行中，等待USB设备... (USB键盘: %s, USB鼠标: %s, BLE HID: %s)",
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
