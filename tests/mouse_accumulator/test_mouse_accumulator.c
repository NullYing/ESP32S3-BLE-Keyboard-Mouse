#include "mouse_accumulator.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t reports[1024][6];
static unsigned report_count;
static bool ready = true;
static esp_err_t result = ESP_OK;
static void (*send_hook)(void);
static void (*before_send_hook)(void);
static int64_t clock_us;

int64_t esp_timer_get_time(void) { return ++clock_us; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle)
{ (void)args; *handle = (void *)1; return ESP_OK; }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t interval)
{ (void)timer; (void)interval; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t timer) { (void)timer; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t timer) { (void)timer; return ESP_OK; }
bool mouse_accumulator_is_ble_connected(void) { return ready; }
esp_err_t mouse_accumulator_send_ble_report(const uint8_t *report, uint8_t length, uint32_t generation)
{
  assert(length == 6);
  if (before_send_hook)
  {
    void (*hook)(void) = before_send_hook;
    before_send_hook = NULL;
    hook();
  }
  // The real BLE helper performs this check under the destination send mutex.
  if (!mouse_accumulator_session_is_current(generation))
    return ESP_ERR_INVALID_STATE;
  if (result == ESP_OK)
  {
    assert(report_count < sizeof(reports) / sizeof(reports[0]));
    memcpy(reports[report_count++], report, 6);
  }
  if (send_hook)
  {
    void (*hook)(void) = send_hook;
    send_hook = NULL;
    hook();
  }
  return result;
}
static int x(unsigned i) { return (int16_t)(reports[i][1] | reports[i][2] << 8); }
static int y(unsigned i) { return (int16_t)(reports[i][3] | reports[i][4] << 8); }
static int wheel(unsigned i) { return (int8_t)reports[i][5]; }
static unsigned count(void)
{
  uint32_t n;
  mouse_accumulator_get_stats(&n, NULL, NULL, NULL, NULL, NULL);
  return n;
}
static void fresh(void)
{
  result = ESP_OK;
  ready = true;
  send_hook = NULL;
  before_send_hook = NULL;
  mouse_accumulator_set_connected(false);
  mouse_accumulator_add(0, 0, 0, 0);
  mouse_accumulator_set_connected(true);
  mouse_accumulator_try_send(); // Initial button synchronization.
  report_count = 0;
}
static void test_clicks_and_retry(void)
{
  fresh();
  mouse_accumulator_add(10, 0, 0, 1);
  mouse_accumulator_add(20, 0, 0, 1);
  mouse_accumulator_add(0, 0, 0, 0);
  mouse_accumulator_add(0, 0, 0, 1);
  mouse_accumulator_add(0, 0, 0, 0);
  result = ESP_FAIL;
  mouse_accumulator_try_send();
  assert(count() == 5 && report_count == 0);
  result = ESP_OK;
  for (int i = 0; i < 4; ++i) mouse_accumulator_try_send();
  assert(report_count == 4 && count() == 0);
  assert(reports[0][0] == 1 && x(0) == 30);
  assert(reports[1][0] == 0 && reports[2][0] == 1 && reports[3][0] == 0);
}
static void test_residuals_and_button_order(void)
{
  fresh();
  mouse_accumulator_add(30000, -30000, 120, 1);
  mouse_accumulator_add(30000, -30000, 120, 1);
  mouse_accumulator_add(0, 0, 0, 0);
  for (int i = 0; i < 3; ++i) mouse_accumulator_try_send();
  assert(report_count == 3 && count() == 0);
  assert(x(0) == 32767 && x(1) == 27233);
  assert(y(0) == -32767 && y(1) == -27233);
  assert(wheel(0) == 127 && wheel(1) == 113);
  assert(reports[0][0] == 1 && reports[1][0] == 1 && reports[2][0] == 0);
  mouse_accumulator_try_send();
  assert(report_count == 3);
}
static void test_empty_queue_residual(void)
{
  fresh();
  mouse_accumulator_add(30000, 30000, -120, 0);
  mouse_accumulator_add(30000, 30000, -120, 0);
  mouse_accumulator_try_send();
  assert(count() == 0 && report_count == 1);
  mouse_accumulator_try_send();
  assert(count() == 0 && report_count == 2);
  assert(x(0) + x(1) == 60000 && y(0) + y(1) == 60000);
  assert(wheel(0) == -127 && wheel(1) == -113);
  mouse_accumulator_try_send();
  assert(report_count == 2);
}
static void test_disconnect_and_readiness(void)
{
  fresh();
  mouse_accumulator_add(30000, 0, 0, 0);
  mouse_accumulator_add(30000, 0, 0, 0);
  mouse_accumulator_try_send(); // Residual exists at disconnect.
  mouse_accumulator_set_connected(false);
  mouse_accumulator_add(123, 456, 100, 1);
  mouse_accumulator_add(789, 0, 0, 2);
  mouse_accumulator_try_send();
  assert(report_count == 1 && count() == 0);
  mouse_accumulator_set_connected(true);
  mouse_accumulator_add(7, 0, 0, 0);
  mouse_accumulator_try_send();
  assert(report_count == 2 && reports[1][0] == 2 && x(1) == 0 && y(1) == 0 && wheel(1) == 0);
  ready = false; // Notification readiness is NOT a connection lifecycle reset.
  mouse_accumulator_add(8, 0, 0, 0);
  mouse_accumulator_try_send();
  assert(count() == 2);
  ready = true;
  mouse_accumulator_try_send();
  assert(report_count == 3 && x(2) == 15 && reports[2][0] == 0);
}
static void add_during_send(void) { mouse_accumulator_add(5, 0, 0, 0); }
static void clear_during_send(void)
{
  mouse_accumulator_clear();
  mouse_accumulator_add(9, 0, 0, 0);
  mouse_accumulator_try_send(); // Reentrant caller cannot send an overlapping batch.
}
static void overflow_during_send(void)
{
  for (int i = 0; i < RING_BUFFER_CAPACITY; ++i)
    mouse_accumulator_add(1, 0, 0, 0);
}
static void test_transaction_interleavings(void)
{
  fresh();
  mouse_accumulator_add(3, 0, 0, 0);
  send_hook = add_during_send;
  mouse_accumulator_try_send();
  assert(count() == 1);
  mouse_accumulator_try_send();
  assert(report_count == 2 && x(0) == 3 && x(1) == 5);

  fresh();
  mouse_accumulator_add(30000, 0, 0, 0);
  mouse_accumulator_add(30000, 0, 0, 0);
  send_hook = clear_during_send;
  mouse_accumulator_try_send();
  assert(count() == 1); // Old batch must not pop the new event or restore residual.
  mouse_accumulator_try_send(); // Synchronize buttons after clear.
  mouse_accumulator_try_send();
  assert(count() == 0 && report_count == 3 && x(2) == 9);

  fresh();
  mouse_accumulator_add(3, 0, 0, 0);
  send_hook = overflow_during_send;
  mouse_accumulator_try_send();
  assert(count() == RING_BUFFER_CAPACITY - 1);
  mouse_accumulator_try_send();
  assert(report_count == 2 && x(0) == 3 && x(1) == RING_BUFFER_CAPACITY - 1);
}
static void reconnect_before_notify(void)
{
  mouse_accumulator_set_connected(false);
  mouse_accumulator_add(400, 0, 0, 1);
  mouse_accumulator_set_connected(true);
  mouse_accumulator_add(9, 0, 0, 0);
}
static void test_usb_detach_release(void)
{
  fresh();
  mouse_accumulator_add(3, 0, 0, 1);
  mouse_accumulator_try_send();
  mouse_accumulator_add(5, 0, 0, 1);
  // The USB detach integration updates physical state before clearing motion.
  mouse_accumulator_add(0, 0, 0, 0);
  mouse_accumulator_clear();
  result = ESP_FAIL;
  mouse_accumulator_try_send();
  assert(report_count == 1);
  result = ESP_OK;
  mouse_accumulator_try_send();
  assert(report_count == 2 && reports[1][0] == 0 && x(1) == 0 && count() == 0);
  mouse_accumulator_try_send();
  assert(report_count == 2);
}
static void test_session_guard(void)
{
  fresh();
  mouse_accumulator_add(123, 0, 0, 0);
  before_send_hook = reconnect_before_notify;
  mouse_accumulator_try_send();
  assert(report_count == 0 && count() == 1); // Old movement never reaches the new host.
  mouse_accumulator_try_send();
  mouse_accumulator_try_send();
  assert(report_count == 2 && reports[0][0] == 1 && x(0) == 0);
  assert(reports[1][0] == 0 && x(1) == 9);
}
static void test_overflow_release_and_idle(void)
{
  fresh();
  for (int i = 0; i < RING_BUFFER_CAPACITY; ++i)
    mouse_accumulator_add(0, 0, 0, 1);
  mouse_accumulator_add(0, 0, 0, 0); // Queue full; latest physical state must survive.
  mouse_accumulator_try_send();
  mouse_accumulator_try_send();
  assert(report_count == 2 && reports[0][0] == 1 && reports[1][0] == 0);
  for (int i = 0; i < RING_BUFFER_CAPACITY; ++i)
    mouse_accumulator_add(0, 0, 0, 0);
  mouse_accumulator_try_send();
  assert(count() == 0 && report_count == 2);
}
int main(void)
{
  assert(mouse_accumulator_init() == ESP_OK);
  test_clicks_and_retry();
  test_residuals_and_button_order();
  test_disconnect_and_readiness();
  test_empty_queue_residual();
  test_transaction_interleavings();
  test_overflow_release_and_idle();
  test_session_guard();
  test_usb_detach_release();
  puts("mouse accumulator regression: PASS");
}
