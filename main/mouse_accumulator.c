/*
 * Mouse Motion Accumulator - Implementation File (方案A: Ring Buffer + 时间窗重采样)
 *
 * 核心逻辑:
 * - USB侧: push事件到ring buffer (producer)
 * - BLE侧: 同一按钮状态内合并位移，按顺序发送按钮转换
 * - 两阶段提交: 原子快照，notify成功且连接未变再pop
 * - 残差累积: 没有新输入时继续排空饱和后的剩余值
 * - 连接生命周期: 丢弃断线运动，保留并同步当前USB按钮
 */

#include "mouse_accumulator.h"
#include "esp_log.h"
#include "hid_host_example.h"
#include <string.h>

/* =================================================================================================
   内部变量
   ================================================================================================= */

static const char *TAG = "MOUSE_ACC_A";

// 全局累加器实例(方案A)
static mouse_motion_accumulator_t g_acc = {
    .ring = {
        .head = 0,
        .tail = 0,
        .count = 0,
        .overflow_count = 0,
        .spinlock = portMUX_INITIALIZER_UNLOCKED},
    .t_last_send_us = 0,
    .residual_dx = 0,
    .residual_dy = 0,
    .residual_wheel = 0,
    .last_known_buttons = 0,
    .last_usb_buttons = 0,
    .total_events_pushed = 0,
    .total_events_popped = 0,
    .total_packets_sent = 0,
    .total_send_failures = 0};

// BLE发送定时器句柄
static esp_timer_handle_t s_send_timer = NULL;

// 当前BLE发送间隔(微秒),动态更新
static uint32_t s_current_send_interval_us = BLE_SEND_INTERVAL_US_DEFAULT;

/* =================================================================================================
   内部辅助函数
   ================================================================================================= */

/**
 * @brief 夹紧(clamp)到指定范围
 */
static inline int32_t clamp_s32(int32_t value, int32_t min_val, int32_t max_val)
{
  if (value < min_val)
    return min_val;
  if (value > max_val)
    return max_val;
  return value;
}

/**
 * @brief 获取当前时间戳(微秒)
 */
static inline uint64_t get_time_us(void)
{
  return esp_timer_get_time();
}

/* All queue, residual, button and connection state is protected by ring.spinlock.
 * A send snapshots one immutable batch; only the same generation may commit.
 * Overflow drops the newest event, so a producer never moves the consumer tail.
 * last_usb_buttons still tracks dropped input, allowing final button recovery.
 */
static bool s_connected;
static bool s_send_in_progress;
static bool s_sync_buttons_pending;
static uint8_t s_sync_buttons;
static uint32_t s_generation;

static void reset_motion_locked(void)
{
  g_acc.ring.head = 0;
  g_acc.ring.tail = 0;
  g_acc.ring.count = 0;
  g_acc.residual_dx = 0;
  g_acc.residual_dy = 0;
  g_acc.residual_wheel = 0;
  g_acc.t_last_send_us = get_time_us();
  g_acc.last_known_buttons = 0;
  s_sync_buttons = g_acc.last_usb_buttons;
  s_sync_buttons_pending = s_connected;
  ++s_generation;
}

/* =================================================================================================
   公共API实现
   ================================================================================================= */

esp_err_t mouse_accumulator_init(void)
{
  // 初始化时间基准
  g_acc.t_last_send_us = get_time_us();

  // 创建BLE发送定时器
  const esp_timer_create_args_t timer_args = {
      .callback = &mouse_accumulator_timer_callback,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ble_send_timer_a"};

  esp_err_t ret = esp_timer_create(&timer_args, &s_send_timer);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "创建BLE发送定时器失败: %s", esp_err_to_name(ret));
    return ret;
  }

  // 启动定时器(周期性触发)
  ret = esp_timer_start_periodic(s_send_timer, s_current_send_interval_us);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "启动BLE发送定时器失败: %s", esp_err_to_name(ret));
    esp_timer_delete(s_send_timer);
    s_send_timer = NULL;
    return ret;
  }

  ESP_LOGI(TAG, "鼠标累加器初始化成功(方案A: Ring Buffer)");
  ESP_LOGI(TAG, "  - Ring容量: %d条事件", RING_BUFFER_CAPACITY);
  ESP_LOGI(TAG, "  - 发送周期: %u us (约%.1f Hz)", (unsigned int)s_current_send_interval_us, 1000000.0 / s_current_send_interval_us);

  return ESP_OK;
}

void mouse_accumulator_clear(void)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  reset_motion_locked();
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

void mouse_accumulator_set_connected(bool connected)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  if (s_connected != connected)
  {
    s_connected = connected;
    reset_motion_locked();
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

bool mouse_accumulator_session_is_current(uint32_t generation)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  bool current = s_connected && generation == s_generation;
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
  return current;
}

void mouse_accumulator_add(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons)
{
  buttons &= 0x1F;
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  mouse_event_t event = {
      .t_us = get_time_us(), .dx = dx, .dy = dy, .wheel = wheel,
      .buttons = buttons,
      .flags = g_acc.last_usb_buttons != buttons ? EVENT_FLAG_BUTTON_CHANGED : 0};
  g_acc.last_usb_buttons = buttons;
  // Relative input has no destination until the connection is authenticated.
  if (s_connected)
  {
    if (g_acc.ring.count < RING_BUFFER_CAPACITY)
    {
      g_acc.ring.events[g_acc.ring.head & RING_BUFFER_MASK] = event;
      ++g_acc.ring.head;
      ++g_acc.ring.count;
      ++g_acc.total_events_pushed;
    }
    else
    {
      ++g_acc.ring.overflow_count;
    }
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

void mouse_accumulator_timer_callback(void *arg)
{
  (void)arg;
  mouse_accumulator_try_send();
}

void mouse_accumulator_try_send(void)
{
  // Temporary send-lock contention or disabled notifications only postpone work.
  if (!mouse_accumulator_is_ble_connected())
    return;

  uint64_t t_now = get_time_us();
  uint32_t generation, tail, num_to_consume = 0;
  int32_t sum_dx, sum_dy, sum_wheel;
  uint8_t btn;
  bool sync_pending;

  portENTER_CRITICAL(&g_acc.ring.spinlock);
  if (!s_connected || s_send_in_progress)
  {
    portEXIT_CRITICAL(&g_acc.ring.spinlock);
    return;
  }
  generation = s_generation;
  tail = g_acc.ring.tail;
  sum_dx = g_acc.residual_dx;
  sum_dy = g_acc.residual_dy;
  sum_wheel = g_acc.residual_wheel;
  btn = g_acc.last_known_buttons;
  sync_pending = s_sync_buttons_pending;
  bool dirty = sync_pending || sum_dx || sum_dy || sum_wheel;

  if (sync_pending)
  {
    // Synchronize the state at connection time before replaying newer edges.
    btn = s_sync_buttons;
  }
  else if (!dirty)
  {
    // Finish residual motion using its original button state before the next
    // edge. Each subsequent batch has exactly one button state.
    for (uint32_t i = 0; i < g_acc.ring.count; ++i)
    {
      const mouse_event_t *event = &g_acc.ring.events[(tail + i) & RING_BUFFER_MASK];
      if (event->t_us > t_now || (i > 0 && event->buttons != btn))
        break;
      if (i == 0)
        btn = event->buttons;
      sum_dx += event->dx;
      sum_dy += event->dy;
      sum_wheel += event->wheel;
      dirty |= event->flags & EVENT_FLAG_BUTTON_CHANGED;
      ++num_to_consume;
    }
    dirty |= sum_dx || sum_dy || sum_wheel || btn != g_acc.last_known_buttons;
    // Recover the latest button state when overflow dropped a final release.
    if (!num_to_consume && !g_acc.ring.count && btn != g_acc.last_usb_buttons)
    {
      btn = g_acc.last_usb_buttons;
      dirty = true;
    }
  }

  if (!dirty)
  {
    // Consume idle reports too, otherwise they occupy the bounded queue forever.
    g_acc.ring.tail += num_to_consume;
    g_acc.ring.count -= num_to_consume;
    g_acc.total_events_popped += num_to_consume;
    portEXIT_CRITICAL(&g_acc.ring.spinlock);
    return;
  }
  s_send_in_progress = true;
  portEXIT_CRITICAL(&g_acc.ring.spinlock);

  int16_t dx_send = (int16_t)clamp_s32(sum_dx, -32767, 32767);
  int16_t dy_send = (int16_t)clamp_s32(sum_dy, -32767, 32767);
  int8_t wheel_send = (int8_t)clamp_s32(sum_wheel, -127, 127);
  uint8_t report[6] = {
      btn, (uint8_t)dx_send, (uint8_t)((uint16_t)dx_send >> 8),
      (uint8_t)dy_send, (uint8_t)((uint16_t)dy_send >> 8), (uint8_t)wheel_send};
  esp_err_t ret = mouse_accumulator_send_ble_report(report, sizeof(report), generation);

  portENTER_CRITICAL(&g_acc.ring.spinlock);
  if (generation == s_generation && tail == g_acc.ring.tail)
  {
    if (ret == ESP_OK)
    {
      g_acc.ring.tail += num_to_consume;
      g_acc.ring.count -= num_to_consume;
      g_acc.total_events_popped += num_to_consume;
      g_acc.residual_dx = sum_dx - dx_send;
      g_acc.residual_dy = sum_dy - dy_send;
      g_acc.residual_wheel = sum_wheel - wheel_send;
      g_acc.last_known_buttons = btn;
      g_acc.t_last_send_us = t_now;
      if (sync_pending)
        s_sync_buttons_pending = false;
      ++g_acc.total_packets_sent;
    }
    else
    {
      ++g_acc.total_send_failures;
    }
  }
  // A concurrent clear/connection change invalidates every part of the batch.
  s_send_in_progress = false;
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

void mouse_accumulator_get_stats(uint32_t *events_in_ring,
                                 uint32_t *overflow_count,
                                 uint32_t *total_pushed,
                                 uint32_t *total_popped,
                                 uint32_t *total_sent,
                                 uint32_t *total_failures)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    if (events_in_ring)
      *events_in_ring = g_acc.ring.count;
    if (overflow_count)
      *overflow_count = g_acc.ring.overflow_count;
    if (total_pushed)
      *total_pushed = g_acc.total_events_pushed;
    if (total_popped)
      *total_popped = g_acc.total_events_popped;
    if (total_sent)
      *total_sent = g_acc.total_packets_sent;
    if (total_failures)
      *total_failures = g_acc.total_send_failures;
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

esp_err_t mouse_accumulator_update_send_interval(uint16_t conn_interval_units)
{
  if (s_send_timer == NULL)
  {
    ESP_LOGE(TAG, "定时器未初始化,无法更新发送间隔");
    return ESP_ERR_INVALID_STATE;
  }

  // BLE连接间隔单位: 1.25ms
  // 转换为微秒: conn_interval_units * 1.25 * 1000
  // 为了避免浮点运算: conn_interval_units * 1250 / 1000
  // 简化: conn_interval_units * 5 / 4 (因为 1.25 = 5/4)
  uint32_t new_interval_us = (uint32_t)conn_interval_units * 1250;

  // 限制范围: 最小1ms,最大100ms
  if (new_interval_us < 1000)
  {
    new_interval_us = 1000;
    ESP_LOGW(TAG, "连接间隔过小,限制为1ms");
  }
  else if (new_interval_us > 100000)
  {
    new_interval_us = 100000;
    ESP_LOGW(TAG, "连接间隔过大,限制为100ms");
  }

  // 如果间隔没有变化,直接返回
  if (new_interval_us == s_current_send_interval_us)
  {
    ESP_LOGD(TAG, "发送间隔未变化: %u us", (unsigned int)s_current_send_interval_us);
    return ESP_OK;
  }

  // 停止当前定时器
  esp_err_t ret = esp_timer_stop(s_send_timer);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "停止定时器失败: %s", esp_err_to_name(ret));
    return ret;
  }

  // 更新间隔
  uint32_t old_interval_us = s_current_send_interval_us;
  s_current_send_interval_us = new_interval_us;

  // 重新启动定时器
  ret = esp_timer_start_periodic(s_send_timer, s_current_send_interval_us);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "重启定时器失败: %s,恢复原间隔", esp_err_to_name(ret));
    s_current_send_interval_us = old_interval_us;
    esp_timer_start_periodic(s_send_timer, old_interval_us);
    return ret;
  }

  ESP_LOGI(TAG, "BLE发送间隔已更新: %u us -> %u us (连接间隔: %u * 1.25ms = %.2f ms, 频率: %.1f Hz)",
           (unsigned int)old_interval_us, (unsigned int)s_current_send_interval_us,
           (unsigned int)conn_interval_units, (float)conn_interval_units * 1.25f,
           1000000.0f / s_current_send_interval_us);

  return ESP_OK;
}
