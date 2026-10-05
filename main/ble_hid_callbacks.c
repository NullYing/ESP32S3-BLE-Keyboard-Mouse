/*
 * BLE HID Callbacks - Implementation
 *
 * BLE HID 事件回调函数实现
 */

#include "ble_hid_callbacks.h"
#include "ble_device_manager.h"
#include "ble_hid_send.h"
#include "hid_config.h"
#include "hidd_le_prf_int.h"
#include "mouse_accumulator.h"
#include "led_control.h"
#include "usb/hid_host.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BLE_CB";

/* =================================================================================================
   模块内部状态
   =================================================================================================
 */

// 外部依赖指针（通过 init 函数设置）
static led_strip_handle_t s_led_strip = NULL;
static usb_hid_devices_t *s_usb_devices = NULL;
static esp_ble_adv_data_t *s_adv_data = NULL;
static esp_ble_adv_params_t *s_adv_params = NULL;
static led_update_callback_t s_led_callback = NULL;
static bool s_connection_present = false;
static bool s_connection_accepted = false;
static esp_bd_addr_t s_remote_bda;
static bool s_advertising_requested = false;
static portMUX_TYPE s_advertising_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool s_feedback_active;
static void call_led_update(void);

// 外部变量引用（保持兼容性）
extern uint16_t ble_hid_conn_id;
extern bool sec_conn;

/* =================================================================================================
   初始化
   =================================================================================================
 */

void ble_hid_callbacks_init(led_strip_handle_t led,
                            usb_hid_devices_t *usb_devices,
                            esp_ble_adv_data_t *adv_data,
                            esp_ble_adv_params_t *adv_params) {
  s_led_strip = led;
  s_usb_devices = usb_devices;
  s_adv_data = adv_data;
  s_adv_params = adv_params;
  ESP_LOGI(TAG, "BLE HID 回调模块已初始化");
}

void ble_hid_callbacks_request_advertising(void) {
  portENTER_CRITICAL(&s_advertising_spinlock);
  if (!s_adv_params || s_connection_present || s_advertising_requested) {
    portEXIT_CRITICAL(&s_advertising_spinlock);
    return;
  }
  s_advertising_requested = true;
  portEXIT_CRITICAL(&s_advertising_spinlock);
  esp_err_t ret = esp_ble_gap_start_advertising(s_adv_params);
  if (ret != ESP_OK) {
    portENTER_CRITICAL(&s_advertising_spinlock);
    s_advertising_requested = false;
    portEXIT_CRITICAL(&s_advertising_spinlock);
    ESP_LOGW(TAG, "Advertising request failed: %s", esp_err_to_name(ret));
  }
}

static void switching_feedback_task(void *arg) {
  (void)arg;
  led_control_blink_switching(s_led_strip);
  call_led_update();
  portENTER_CRITICAL(&s_advertising_spinlock);
  s_feedback_active = false;
  portEXIT_CRITICAL(&s_advertising_spinlock);
  vTaskDelete(NULL);
}

void ble_hid_callbacks_show_switching(void) {
  portENTER_CRITICAL(&s_advertising_spinlock);
  if (!s_led_strip || s_feedback_active) {
    portEXIT_CRITICAL(&s_advertising_spinlock);
    return;
  }
  s_feedback_active = true;
  portEXIT_CRITICAL(&s_advertising_spinlock);
  if (xTaskCreate(switching_feedback_task, "ble_led", 2048, NULL, 1, NULL) != pdPASS) {
    portENTER_CRITICAL(&s_advertising_spinlock);
    s_feedback_active = false;
    portEXIT_CRITICAL(&s_advertising_spinlock);
  }
}

void ble_hid_callbacks_set_led_callback(led_update_callback_t callback) {
  s_led_callback = callback;
}

/* =================================================================================================
   内部辅助函数
   =================================================================================================
 */

static void call_led_update(void) {
  if (s_led_callback) {
    s_led_callback();
  }
}

/* =================================================================================================
   BLE HID 事件回调
   =================================================================================================
 */

void ble_hid_event_callback(esp_hidd_cb_event_t event,
                            esp_hidd_cb_param_t *param) {
  switch (event) {
  case ESP_HIDD_EVENT_REG_FINISH: {
    if (param->init_finish.state == ESP_HIDD_INIT_OK) {
      esp_ble_gap_set_device_name(BLE_HID_DEVICE_NAME);
      if (s_adv_data) {
        esp_ble_gap_config_adv_data(s_adv_data);
      }
    }
    break;
  }
  case ESP_BAT_EVENT_REG: {
    break;
  }
  case ESP_HIDD_EVENT_DEINIT_FINISH:
    break;
  case ESP_HIDD_EVENT_BLE_CONNECT: {
    ESP_LOGI(TAG, "ESP_HID_EVENT_BLE_CONNECT");
    ble_hid_send_enable(false);
    mouse_accumulator_set_connected(false);
    sec_conn = false;
    ble_hid_conn_id = param->connect.conn_id; // Connection ID 0 is valid.
    portENTER_CRITICAL(&s_advertising_spinlock);
    s_connection_present = true;
    s_advertising_requested = false;
    portEXIT_CRITICAL(&s_advertising_spinlock);
    memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    s_connection_accepted = ble_device_manager_on_connected(s_remote_bda);
    if (!s_connection_accepted) {
      esp_ble_gap_disconnect(s_remote_bda);
      call_led_update();
      break;
    }
    call_led_update();

    // 更新BLE连接参数以提高回报率
    esp_ble_conn_update_params_t conn_params = {0};
    memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    conn_params.min_int = 0x0006; // 7.5ms
    conn_params.max_int = 0x0006; // 7.5ms
    conn_params.latency = 0;
    conn_params.timeout = 0x0320; // 1000ms
    esp_ble_gap_update_conn_params(&conn_params);
    ESP_LOGI(TAG, "BLE连接参数已更新: interval=7.5ms, latency=0");
    break;
  }
  case ESP_HIDD_EVENT_BLE_DISCONNECT: {
    ble_hid_send_enable(false);
    sec_conn = false;
    ble_hid_conn_id = 0;
    portENTER_CRITICAL(&s_advertising_spinlock);
    s_connection_present = false;
    portEXIT_CRITICAL(&s_advertising_spinlock);
    s_connection_accepted = false;
    ble_device_manager_on_disconnected();
    ESP_LOGI(TAG, "ESP_HID_EVENT_BLE_DISCONNECT");

    // 清理鼠标累加器
    mouse_accumulator_set_connected(false);
    ble_hid_callbacks_request_advertising();
    call_led_update();
    break;
  }
  case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
    ESP_LOGI(TAG, "%s, ESP_HID_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
    ESP_LOG_BUFFER_HEX(TAG, param->vendor_write.data,
                       param->vendor_write.length);
    break;
  }
  case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
    if (param->led_write.length < 1) {
      ESP_LOGW(TAG, "LED报告数据长度不足: %d < 1", param->led_write.length);
      break;
    }

    uint8_t led_state = param->led_write.data[0];
    bool num_lock = (led_state & 0x01) != 0;
    bool caps_lock = (led_state & 0x02) != 0;
    bool scroll_lock = (led_state & 0x04) != 0;

    ESP_LOGI(TAG, "收到LED报告: 0x%02X (Num:%s Caps:%s Scroll:%s)", led_state,
             num_lock ? "ON" : "OFF", caps_lock ? "ON" : "OFF",
             scroll_lock ? "ON" : "OFF");

    // 转发LED报告到USB键盘
    if (s_usb_devices && s_usb_devices->keyboard_handle) {
      uint8_t led_data = param->led_write.data[0];
      esp_err_t ret =
          hid_class_request_set_report(s_usb_devices->keyboard_handle,
                                       HID_REPORT_TYPE_OUTPUT, 0, &led_data, 1);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED转发失败: %s", esp_err_to_name(ret));
      } else {
        ESP_LOGI(TAG, "LED转发成功: 0x%02X -> USB键盘", led_data);
      }
    } else {
      ESP_LOGW(TAG, "USB键盘未连接，无法转发LED报告");
    }
    break;
  }
  default:
    break;
  }
}

/* =================================================================================================
   GAP 事件处理器
   =================================================================================================
 */

void gap_event_handler(esp_gap_ble_cb_event_t event,
                       esp_ble_gap_cb_param_t *param) {
  switch (event) {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ble_hid_callbacks_request_advertising();
    break;
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      portENTER_CRITICAL(&s_advertising_spinlock);
      s_advertising_requested = false;
      portEXIT_CRITICAL(&s_advertising_spinlock);
      ESP_LOGW(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
    }
    break;
  case ESP_GAP_BLE_SEC_REQ_EVT:
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
      ESP_LOGD(TAG, "%x:", param->ble_security.ble_req.bd_addr[i]);
    }
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr,
        s_connection_present && s_connection_accepted &&
        memcmp(s_remote_bda, param->ble_security.ble_req.bd_addr,
               sizeof(esp_bd_addr_t)) == 0);
    break;
  case ESP_GAP_BLE_AUTH_CMPL_EVT: {
    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr,
           sizeof(esp_bd_addr_t));
    ESP_LOGI(TAG, "remote BD_ADDR: %08x%04x",
             (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) +
                 bd_addr[3],
             (bd_addr[4] << 8) + bd_addr[5]);
    ESP_LOGI(TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
    ESP_LOGI(TAG, "pair status = %s",
             param->ble_security.auth_cmpl.success ? "success" : "fail");
    /* AUTH carries an address, not a conn_id. Require it to belong to the
     * current accepted physical connection before touching slots or sending. */
    if (!s_connection_present || !s_connection_accepted ||
        memcmp(s_remote_bda, bd_addr, sizeof(esp_bd_addr_t)) != 0 ||
        !hidd_le_env.hidd_clcb[0].in_use ||
        hidd_le_env.hidd_clcb[0].conn_id != ble_hid_conn_id ||
        memcmp(hidd_le_env.hidd_clcb[0].remote_bda, bd_addr,
               sizeof(esp_bd_addr_t)) != 0) {
      ESP_LOGW(TAG, "Ignoring AUTH for a stale or rejected connection");
      break;
    }
    if (!param->ble_security.auth_cmpl.success) {
      ESP_LOGE(TAG, "fail reason = 0x%x",
               param->ble_security.auth_cmpl.fail_reason);
      s_connection_accepted = false;
      ble_hid_send_enable(false);
      sec_conn = false;
      mouse_accumulator_set_connected(false);
      ble_device_manager_on_disconnected();
      esp_ble_gap_disconnect(bd_addr);
    } else if (ble_device_manager_on_authenticated(bd_addr) &&
               !ble_device_manager_is_switching()) {
      ble_hid_send_enable(true);
      sec_conn = ble_hid_send_is_ready();
      mouse_accumulator_set_connected(sec_conn);
    }
    call_led_update();
    break;
  }
  case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
    if (param->update_conn_params.status == ESP_BT_STATUS_SUCCESS) {
      uint16_t conn_int = param->update_conn_params.conn_int;
      uint16_t conn_latency = param->update_conn_params.latency;
      uint16_t conn_timeout = param->update_conn_params.timeout;

      ESP_LOGI(TAG,
               "BLE连接参数更新完成: interval=%d (%.2f ms), latency=%d, "
               "timeout=%d (%.2f ms)",
               conn_int, (float)conn_int * 1.25f, conn_latency, conn_timeout,
               (float)conn_timeout * 1.25f);

      esp_err_t ret = mouse_accumulator_update_send_interval(conn_int);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "更新鼠标发送间隔失败: %s", esp_err_to_name(ret));
      }
    } else {
      ESP_LOGW(TAG, "BLE连接参数更新失败,状态: 0x%02x",
               param->update_conn_params.status);
    }
    break;
  default:
    break;
  }
}
