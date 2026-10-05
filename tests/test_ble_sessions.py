#!/usr/bin/env python3
"""Compile the production BLE modules with deterministic SDK/NVS substitutes."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#ifndef TEST_SDK_H
#define TEST_SDK_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NVS_NOT_FOUND 5
#define ESP_BD_ADDR_LEN 6
typedef uint8_t esp_bd_addr_t[6];
typedef void *led_strip_handle_t;
typedef void *hid_host_device_handle_t;
typedef int hid_host_driver_event_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef void *SemaphoreHandle_t;
#define pdTRUE 1
#define pdPASS 1
static inline int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, void *handle) { (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)handle; return pdPASS; }
static inline void vTaskDelete(void *handle) { (void)handle; }
static inline void led_control_blink_switching(void *led) { (void)led; }
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffffu
extern bool test_mutex_busy;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t m, uint32_t delay) { (void)m; return !test_mutex_busy || delay == portMAX_DELAY; }
static inline void xSemaphoreGive(SemaphoreHandle_t m) { (void)m; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
#define BLE_HID_DEVICE_NAME "Test HID"
#define ESP_BT_STATUS_SUCCESS 0
#define HID_REPORT_TYPE_INPUT 1
#define HID_REPORT_TYPE_OUTPUT 2
#define HID_RPT_ID_MOUSE_IN 1
#define HID_RPT_ID_KEY_IN 2
#define HID_RPT_ID_CC_IN 3
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_get_i8(nvs_handle_t, const char *, int8_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_set_i8(nvs_handle_t, const char *, int8_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
typedef struct { int dummy; } esp_ble_adv_data_t;
typedef struct { int dummy; } esp_ble_adv_params_t;
typedef struct { esp_bd_addr_t bda; int min_int, max_int, latency, timeout; } esp_ble_conn_update_params_t;
typedef struct { bool in_use; uint16_t conn_id; esp_bd_addr_t remote_bda; } hidd_clcb_t;
typedef struct { int gatt_if; hidd_clcb_t hidd_clcb[1]; } hidd_le_env_t;
extern hidd_le_env_t hidd_le_env;
enum { ESP_HIDD_EVENT_REG_FINISH, ESP_BAT_EVENT_REG, ESP_HIDD_EVENT_DEINIT_FINISH, ESP_HIDD_EVENT_BLE_CONNECT, ESP_HIDD_EVENT_BLE_DISCONNECT, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT, ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT };
typedef int esp_hidd_cb_event_t;
#define ESP_HIDD_INIT_OK 0
typedef struct {
 struct { int state; } init_finish;
 struct { uint16_t conn_id; esp_bd_addr_t remote_bda; } connect;
 struct { uint8_t *data; uint16_t length; } vendor_write, led_write;
} esp_hidd_cb_param_t;
enum { ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, ESP_GAP_BLE_ADV_START_COMPLETE_EVT, ESP_GAP_BLE_SEC_REQ_EVT, ESP_GAP_BLE_AUTH_CMPL_EVT, ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT };
typedef int esp_gap_ble_cb_event_t;
typedef struct {
 struct { int status; } adv_start_cmpl;
 struct {
  struct { esp_bd_addr_t bd_addr; } ble_req;
  struct { esp_bd_addr_t bd_addr; bool success; int addr_type, fail_reason; } auth_cmpl;
 } ble_security;
 struct { int status; uint16_t conn_int, latency, timeout; } update_conn_params;
} esp_ble_gap_cb_param_t;
esp_err_t esp_ble_gap_disconnect(const uint8_t *);
esp_err_t esp_ble_gap_start_advertising(esp_ble_adv_params_t *);
esp_err_t esp_ble_gap_set_device_name(const char *);
esp_err_t esp_ble_gap_config_adv_data(esp_ble_adv_data_t *);
esp_err_t esp_ble_gap_update_conn_params(esp_ble_conn_update_params_t *);
esp_err_t esp_ble_gap_security_rsp(const uint8_t *, bool);
esp_err_t hid_class_request_set_report(void *, int, int, uint8_t *, int);
esp_err_t hid_dev_send_report(int, uint16_t, uint8_t, uint8_t, uint8_t, uint8_t *);
void mouse_accumulator_set_connected(bool);
bool mouse_accumulator_session_is_current(uint32_t);
esp_err_t mouse_accumulator_update_send_interval(uint16_t);
#endif
'''
HARNESS = r'''
#include "test_sdk.h"
bool test_mutex_busy = false;
hidd_le_env_t hidd_le_env;
uint16_t ble_hid_conn_id;
bool sec_conn;
led_strip_handle_t led_strip;
static bool mouse_connected;
static uint32_t mouse_generation;
static int writes, disconnects, advertisements, sends;
static int adv_configurations, device_name_updates, usb_led_writes, led_updates;
static esp_ble_adv_data_t *expected_adv_data;
static esp_ble_adv_params_t *expected_adv_params;
static hid_host_device_handle_t expected_usb_keyboard;
static uint8_t expected_led_byte;
static void update_led(void) { led_updates++; }
static uint16_t last_send_conn;
static esp_bd_addr_t persisted[2];
static bool persisted_valid[2];
esp_err_t nvs_open(const char *key, int mode, nvs_handle_t *handle) { (void)key; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) { (void)h; int i = strcmp(key, "slot_a") ? 1 : 0; if (!persisted_valid[i]) return ESP_ERR_NVS_NOT_FOUND; memcpy(out, persisted[i], 6); *len = 6; return ESP_OK; }
esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *v) { (void)h; (void)k; *v = 0; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *in, size_t len) { (void)h; assert(len == 6); int i = strcmp(key, "slot_a") ? 1 : 0; memcpy(persisted[i], in, 6); persisted_valid[i] = true; writes++; return ESP_OK; }
esp_err_t nvs_set_i8(nvs_handle_t h, const char *key, int8_t v) { (void)h; (void)key; (void)v; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; persisted_valid[strcmp(key, "slot_a") ? 1 : 0] = false; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; memset(persisted_valid, 0, sizeof(persisted_valid)); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t esp_ble_gap_disconnect(const uint8_t *bda) { (void)bda; disconnects++; return ESP_OK; }
esp_err_t esp_ble_gap_start_advertising(esp_ble_adv_params_t *p) { assert(p == expected_adv_params); advertisements++; return ESP_OK; }
esp_err_t esp_ble_gap_set_device_name(const char *n) { assert(strcmp(n, BLE_HID_DEVICE_NAME) == 0); device_name_updates++; return ESP_OK; }
esp_err_t esp_ble_gap_config_adv_data(esp_ble_adv_data_t *p) { assert(p == expected_adv_data); adv_configurations++; return ESP_OK; }
esp_err_t esp_ble_gap_update_conn_params(esp_ble_conn_update_params_t *p) { (void)p; return ESP_OK; }
esp_err_t esp_ble_gap_security_rsp(const uint8_t *bda, bool ok) { (void)bda; (void)ok; return ESP_OK; }
esp_err_t hid_class_request_set_report(void *h, int t, int i, uint8_t *d, int n) { assert(h == expected_usb_keyboard); assert(t == HID_REPORT_TYPE_OUTPUT && i == 0 && n == 1 && d[0] == expected_led_byte); usb_led_writes++; return ESP_OK; }
esp_err_t hid_dev_send_report(int g, uint16_t c, uint8_t i, uint8_t t, uint8_t n, uint8_t *r) { (void)g; (void)i; (void)t; (void)n; (void)r; sends++; last_send_conn = c; return ESP_OK; }
void mouse_accumulator_set_connected(bool c) { mouse_connected = c; mouse_generation++; }
bool mouse_accumulator_session_is_current(uint32_t g) { return mouse_connected && g == mouse_generation; }
esp_err_t mouse_accumulator_update_send_interval(uint16_t n) { (void)n; return ESP_OK; }
static const esp_bd_addr_t A = {1,2,3,4,5,6}, B = {2,3,4,5,6,7}, C = {3,4,5,6,7,8};
static void connect_peer(const uint8_t *bda, uint16_t id) {
 hidd_le_env.hidd_clcb[0].in_use = true;
 hidd_le_env.hidd_clcb[0].conn_id = id;
 memcpy(hidd_le_env.hidd_clcb[0].remote_bda, bda, 6);
 esp_hidd_cb_param_t p = {0}; p.connect.conn_id = id; memcpy(p.connect.remote_bda, bda, 6);
 ble_hid_event_callback(ESP_HIDD_EVENT_BLE_CONNECT, &p);
}
static void auth_peer(const uint8_t *bda, bool success) {
 esp_ble_gap_cb_param_t p = {0}; memcpy(p.ble_security.auth_cmpl.bd_addr, bda, 6); p.ble_security.auth_cmpl.success = success;
 gap_event_handler(ESP_GAP_BLE_AUTH_CMPL_EVT, &p);
}
static void disconnect_peer(void) {
 // Production DISCONNECT callback happens before profile deallocation.
 ble_hid_event_callback(ESP_HIDD_EVENT_BLE_DISCONNECT, NULL);
 hidd_le_env.hidd_clcb[0].in_use = false;
}
static void assert_slot(int slot, const uint8_t *bda) {
 esp_bd_addr_t actual; assert(ble_device_manager_get_slot_device(slot, actual)); assert(memcmp(actual,bda,6) == 0);
}
int main(void) {
 esp_ble_adv_data_t data = {0}; esp_ble_adv_params_t params = {0}; usb_hid_devices_t usb = {0};
 assert(ble_hid_send_init() == ESP_OK); assert(ble_device_manager_init() == ESP_OK);
 expected_adv_data = &data; expected_adv_params = &params;
 expected_usb_keyboard = (void *)0x1234; usb.keyboard_handle = expected_usb_keyboard;
 ble_hid_callbacks_init(NULL, &usb, &data, &params);
 ble_hid_callbacks_set_led_callback(update_led);
 esp_hidd_cb_param_t registration = {0}; registration.init_finish.state = ESP_HIDD_INIT_OK;
 ble_hid_event_callback(ESP_HIDD_EVENT_REG_FINISH, &registration);
 assert(device_name_updates == 1 && adv_configurations == 1 && advertisements == 0);
 esp_ble_gap_cb_param_t adv_complete = {0};
 gap_event_handler(ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, &adv_complete);
 ble_hid_callbacks_request_advertising(); assert(advertisements == 1);
 uint8_t led_byte = 0x07; expected_led_byte = led_byte;
 esp_hidd_cb_param_t led_write = {0}; led_write.led_write.data = &led_byte; led_write.led_write.length = 1;
 ble_hid_event_callback(ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT, &led_write); assert(usb_led_writes == 1);
 led_write.led_write.length = 0;
 ble_hid_event_callback(ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT, &led_write); assert(usb_led_writes == 1);
 usb.keyboard_handle = NULL; led_write.led_write.length = 1;
 ble_hid_event_callback(ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT, &led_write); assert(usb_led_writes == 1);
 usb.keyboard_handle = expected_usb_keyboard;
 connect_peer(A, 0); assert(led_updates == 1); assert(!ble_hid_send_is_ready()); assert(!mouse_connected); assert(writes == 0);
 auth_peer(A, true); assert(led_updates == 2); assert_slot(0,A); assert(sec_conn && mouse_connected); assert(ble_hid_send_is_ready());
 uint8_t report[8] = {0}; assert(ble_hid_send_keyboard_report(report,8) == ESP_OK); assert(last_send_conn == 0);
 uint32_t original_generation = mouse_generation;
 assert(ble_hid_send_mouse_report_for_session(report,8,original_generation) == ESP_OK);
 test_mutex_busy = true; assert(ble_hid_send_is_ready()); assert(ble_hid_send_mouse_report(report,8) == ESP_ERR_TIMEOUT); test_mutex_busy = false;
 int before = writes; assert(ble_device_manager_discover_new() == ESP_OK); assert(!mouse_connected); assert(!ble_hid_send_is_ready());
 int adv_count = advertisements; int led_count = led_updates; disconnect_peer(); assert(advertisements == adv_count + 1 && led_updates == led_count + 1); connect_peer(A,1); auth_peer(A,true); assert(ble_device_manager_is_discovering()); assert(writes == before); assert_slot(0,A);
 disconnect_peer(); connect_peer(B,1); assert(ble_device_manager_is_discovering()); assert(writes == before); assert_slot(0,A);
 auth_peer(A,true); assert(writes == before); assert(!ble_hid_send_is_ready());
 auth_peer(B,false); assert(ble_device_manager_is_discovering()); assert(writes == before); assert_slot(0,A); assert(!persisted_valid[1]);
 disconnect_peer(); connect_peer(B,2); auth_peer(B,true); assert_slot(0,A); assert_slot(1,B); assert(!ble_device_manager_is_discovering()); assert(ble_device_manager_get_active_slot() == 1); assert(mouse_connected);
 int send_count = sends; assert(ble_hid_send_mouse_report_for_session(report,8,original_generation) == ESP_ERR_INVALID_STATE); assert(sends == send_count);
 assert(ble_device_manager_switch_slot() == ESP_OK); disconnect_peer(); before = writes; int rejected = disconnects;
 connect_peer(C,3); assert(disconnects == rejected + 1); auth_peer(C,true); assert(ble_device_manager_is_switching()); assert(writes == before); assert(!ble_hid_send_is_ready());
 disconnect_peer(); connect_peer(A,4); assert(ble_device_manager_is_switching()); auth_peer(B,true); assert(ble_device_manager_is_switching()); assert(!ble_hid_send_is_ready());
 auth_peer(A,true); assert(ble_device_manager_get_active_slot() == 0); assert(ble_hid_send_is_ready());
 // Explicit switch target survives the switch -> discovery transition.
 assert(ble_device_manager_switch_slot() == ESP_OK); assert(ble_device_manager_discover_new() == ESP_OK); disconnect_peer();
 before = writes; connect_peer(C,5); auth_peer(C,false); assert(writes == before); assert_slot(0,A); assert_slot(1,B);
 disconnect_peer(); connect_peer(C,6); auth_peer(C,true); assert_slot(0,A); assert_slot(1,C);
 disconnect_peer(); before = writes; auth_peer(C,true); assert(!ble_hid_send_is_ready()); assert(writes == before);
 puts("PASS BLE callbacks: registration advertising chain, disconnect advertising, injected USB LED forwarding, LED update callback");
 puts("PASS BLE sessions: AUTH commits, empty slots, failed/stale AUTH, rejected target, conn_id=0, retry contention, advertising deduplication");
 return 0;
}
'''

def strip_includes(source):
    return re.sub(r'^\s*#include[^\n]*\n', '', source, flags=re.MULTILINE)

# Guard the integration order responsible for review #8. Callback registration
# can invoke REG_FINISH immediately, so every injected dependency must exist first.
app_source = (ROOT / 'main' / 'hid_host_example.c').read_text()
app_main = app_source[app_source.index('void app_main(void)'):]
registration = min(app_main.index('esp_ble_gap_register_callback('),
                   app_main.index('esp_hidd_register_callbacks('))
for dependency in ['ble_device_manager_init(', 'ble_hid_send_init(',
                   'led_strip = led_control_init(', 'mouse_accumulator_init(',
                   'ble_hid_callbacks_init(', 'ble_hid_callbacks_set_led_callback(']:
    assert app_main.index(dependency) < registration, f'{dependency} must precede BLE callback registration'
assert app_main.index('led_strip = led_control_init(') < app_main.index('ble_hid_callbacks_init(')
assert re.search(r'ble_hid_callbacks_init\(\s*led_strip,\s*&usb_hid_devices,\s*&ble_hid_adv_data,\s*&ble_hid_adv_params\)', app_main)
assert 'ble_hid_callbacks_set_led_callback(update_led_color)' in app_main
print('PASS BLE app integration: sender, mouse, device manager, LED and injected callbacks initialized before registration', flush=True)

with tempfile.TemporaryDirectory(prefix='ble-session-test-') as directory:
    build = Path(directory)
    sdk = SDK + '\n'.join(strip_includes((ROOT / 'main' / name).read_text()) for name in [
        'usb_hid_types.h', 'ble_device_manager.h', 'ble_hid_send.h', 'ble_hid_callbacks.h'])
    (build / 'test_sdk.h').write_text(sdk)
    sources = []
    for name in ['ble_device_manager.c', 'ble_hid_callbacks.c', 'ble_hid_send.c']:
        output = build / name
        output.write_text('#include "test_sdk.h"\n' + strip_includes((ROOT / 'main' / name).read_text()))
        sources.append(str(output))
    (build / 'test.c').write_text(HARNESS)
    executable = build / 'ble-test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-Wno-unused-variable', '-Wno-unused-function', '-I', str(build), *sources, str(build / 'test.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
