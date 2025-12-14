/*
 * Mouse Motion Accumulator - Implementation File (方案A: Ring Buffer + 时间窗重采样)
 *
 * 核心逻辑:
 * - USB侧: push事件到ring buffer (producer)
 * - BLE侧: 对时间窗内事件积分后发送 (consumer)
 * - 两阶段提交: 先预览计算,notify成功后再真正pop
 * - 残差累积: 处理饱和后的剩余值
 */

#include "mouse_accumulator.h"
#include "esp_log.h"
#include "hid_dev.h"
#include "hidd_le_prf_int.h"
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

/**
 * @brief Ring buffer push (Producer端,线程安全)
 *
 * @return true 成功, false 失败(满)
 */
static bool ring_push(mouse_event_t *event)
{
  bool success = false;

  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    if (g_acc.ring.count < RING_BUFFER_CAPACITY)
    {
      // 未满,直接写入
      uint32_t write_idx = g_acc.ring.head & RING_BUFFER_MASK;
      g_acc.ring.events[write_idx] = *event;
      g_acc.ring.head++;
      g_acc.ring.count++;
      g_acc.total_events_pushed++;
      success = true;
    }
    else
    {
      // 满了,丢弃最老的事件(保证新数据不丢)
      // 推荐策略: 丢最老,防止当前甩动丢失
      uint32_t write_idx = g_acc.ring.head & RING_BUFFER_MASK;
      g_acc.ring.events[write_idx] = *event;
      g_acc.ring.head++;
      g_acc.ring.tail++; // 同时前移tail,覆盖最老
      g_acc.ring.overflow_count++;
      g_acc.total_events_pushed++;
      success = true; // 仍然算成功(已写入)

      // 注意: count保持不变(仍然是CAPACITY)
    }
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);

  return success;
}

/**
 * @brief Ring buffer peek (Consumer端预览,不修改tail)
 *
 * 用于两阶段提交的第一阶段
 *
 * @param idx 要peek的索引(相对tail的偏移)
 * @param out_event 输出事件指针
 * @return true 成功, false 越界
 */
static bool ring_peek(uint32_t idx, mouse_event_t *out_event)
{
  bool success = false;

  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    if (idx < g_acc.ring.count)
    {
      uint32_t read_idx = (g_acc.ring.tail + idx) & RING_BUFFER_MASK;
      *out_event = g_acc.ring.events[read_idx];
      success = true;
    }
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);

  return success;
}

/**
 * @brief Ring buffer批量pop (Consumer端,两阶段提交的第二阶段)
 *
 * @param num_to_pop 要弹出的事件数量
 */
static void ring_pop_batch(uint32_t num_to_pop)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    if (num_to_pop > g_acc.ring.count)
    {
      num_to_pop = g_acc.ring.count;
    }

    g_acc.ring.tail += num_to_pop;
    g_acc.ring.count -= num_to_pop;
    g_acc.total_events_popped += num_to_pop;
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
}

/**
 * @brief 获取ring buffer当前事件数(线程安全)
 */
static uint32_t ring_get_count(void)
{
  uint32_t count;
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    count = g_acc.ring.count;
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);
  return count;
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
  ret = esp_timer_start_periodic(s_send_timer, BLE_SEND_INTERVAL_US);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "启动BLE发送定时器失败: %s", esp_err_to_name(ret));
    esp_timer_delete(s_send_timer);
    s_send_timer = NULL;
    return ret;
  }

  ESP_LOGI(TAG, "鼠标累加器初始化成功(方案A: Ring Buffer)");
  ESP_LOGI(TAG, "  - Ring容量: %d条事件", RING_BUFFER_CAPACITY);
  ESP_LOGI(TAG, "  - 发送周期: %d us (约%.1f Hz)", BLE_SEND_INTERVAL_US, 1000000.0 / BLE_SEND_INTERVAL_US);

  return ESP_OK;
}

void mouse_accumulator_clear(void)
{
  portENTER_CRITICAL(&g_acc.ring.spinlock);
  {
    g_acc.ring.head = 0;
    g_acc.ring.tail = 0;
    g_acc.ring.count = 0;
    // overflow_count不清零,保留统计

    g_acc.t_last_send_us = get_time_us();
    g_acc.residual_dx = 0;
    g_acc.residual_dy = 0;
    g_acc.residual_wheel = 0;
    g_acc.last_known_buttons = 0;
    g_acc.last_usb_buttons = 0;
  }
  portEXIT_CRITICAL(&g_acc.ring.spinlock);

  ESP_LOGI(TAG, "累加器已清空(Ring + 残差)");
}

void mouse_accumulator_add(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons)
{
  // 构建事件
  mouse_event_t event;
  event.t_us = get_time_us();
  event.dx = dx;
  event.dy = dy;
  event.wheel = wheel;
  event.buttons = buttons & 0x1F; // 只保留低5位（支持侧键）

  // 检测按钮变化
  event.flags = 0;
  if (g_acc.last_usb_buttons != buttons)
  {
    event.flags |= EVENT_FLAG_BUTTON_CHANGED;
    // 调试：打印按钮变化事件
    if (buttons & 0x18) // Button 4 或 Button 5
    {
      ESP_LOGI(TAG, "[事件添加] 按钮变化: 0x%02X -> 0x%02X, flags=0x%02X (Button4=%d, Button5=%d)",
               g_acc.last_usb_buttons, buttons, event.flags,
               (buttons & 0x08) ? 1 : 0, (buttons & 0x10) ? 1 : 0);
    }
    g_acc.last_usb_buttons = buttons;
  }

  // Push到ring buffer
  bool success = ring_push(&event);

  if (!success)
  {
    // 理论上不会失败(因为满了会覆盖最老)
    // 但保留错误处理
    ESP_LOGW(TAG, "Ring buffer push失败(不应发生)");
  }

  // 调试日志(高频,正式版应关闭)
  // ESP_LOGV(TAG, "USB事件入队: t=%llu, dx=%d, dy=%d, wheel=%d, btn=0x%02X, flags=0x%02X",
  //          event.t_us, dx, dy, wheel, buttons, event.flags);
}

void mouse_accumulator_timer_callback(void *arg)
{
  mouse_accumulator_try_send();
}

void mouse_accumulator_try_send(void)
{
  // ========== 1. 前置检查 ==========

  // 检查BLE连接状态
  if (!mouse_accumulator_is_ble_connected())
  {
    return; // BLE未连接,直接返回
  }

  // 获取当前时间
  uint64_t t_now = get_time_us();

  // ========== 2. 预览阶段: 遍历ring,计算时间窗积分 ==========

  int32_t sum_dx = g_acc.residual_dx; // 先加上残差
  int32_t sum_dy = g_acc.residual_dy;
  int32_t sum_wheel = g_acc.residual_wheel;

  uint8_t btn = g_acc.last_known_buttons; // 按钮初始值
  bool button_dirty = false;              // 窗内是否有按钮变化
  bool motion_dirty = false;              // 窗内是否有运动

  uint32_t num_to_consume = 0; // 要消费的事件数量

  // 遍历ring中的事件
  uint32_t ring_count = ring_get_count();
  for (uint32_t i = 0; i < ring_count; i++)
  {
    mouse_event_t event;
    if (!ring_peek(i, &event))
    {
      break; // 不应该失败
    }

    // 检查事件是否在当前时间窗内
    if (event.t_us <= t_now)
    {
      // 属于窗内,累加
      sum_dx += (int32_t)event.dx;
      sum_dy += (int32_t)event.dy;
      sum_wheel += (int32_t)event.wheel;

      // 更新按钮状态(以最后一条为准)
      btn = event.buttons;

      // 检查标志
      if (event.flags & EVENT_FLAG_BUTTON_CHANGED)
      {
        button_dirty = true;
      }
      if (event.dx != 0 || event.dy != 0 || event.wheel != 0)
      {
        motion_dirty = true;
      }

      // 标记要消费
      num_to_consume++;
    }
    else
    {
      // 事件在未来,停止遍历(理论上不会,除非时钟问题)
      break;
    }
  }

  // ========== 3. 判断是否需要发送 ==========

  // 只在有运动或按钮变化时发送
  if (!motion_dirty && !button_dirty)
  {
    // 没有数据需要发送
    return;
  }

  // ========== 4. 饱和处理(clamp到int16/int8范围) ==========

  // X轴: int16范围是 -32767..32767
  int16_t dx_send = (int16_t)clamp_s32(sum_dx, -32767, 32767);
  int32_t new_residual_dx = sum_dx - dx_send;

  // Y轴: int16范围是 -32767..32767
  int16_t dy_send = (int16_t)clamp_s32(sum_dy, -32767, 32767);
  int32_t new_residual_dy = sum_dy - dy_send;

  // 滚轮: int8范围是 -127..127
  int8_t wheel_send = (int8_t)clamp_s32(sum_wheel, -127, 127);
  int32_t new_residual_wheel = sum_wheel - wheel_send;

  // ========== 5. 构建BLE鼠标报告 ==========

  // 6字节报告: 按钮1 + X低1 + X高1 + Y低1 + Y高1 + 滚轮1
  uint8_t ble_mouse_report[6] = {0};

  // 字节0: 按钮(低5位，支持侧键)
  ble_mouse_report[0] = btn & 0x1F;

  // 字节1-2: X位移(16位, little-endian)
  ble_mouse_report[1] = (uint8_t)(dx_send & 0xFF);
  ble_mouse_report[2] = (uint8_t)((dx_send >> 8) & 0xFF);

  // 字节3-4: Y位移(16位, little-endian)
  ble_mouse_report[3] = (uint8_t)(dy_send & 0xFF);
  ble_mouse_report[4] = (uint8_t)((dy_send >> 8) & 0xFF);

  // 字节5: 滚轮(8位)
  ble_mouse_report[5] = (uint8_t)wheel_send;

  // ========== 6. 尝试BLE notify ==========

  // 调试：打印按钮状态变化时的 BLE 报告内容
  static uint8_t last_btn_sent = 0;
  if (button_dirty || (btn != last_btn_sent))
  {
    ESP_LOGI(TAG, "[BLE 发送] 按钮状态: 0x%02X -> 0x%02X, button_dirty=%d, 报告: [0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X]",
             last_btn_sent, btn, button_dirty,
             ble_mouse_report[0], ble_mouse_report[1], ble_mouse_report[2],
             ble_mouse_report[3], ble_mouse_report[4], ble_mouse_report[5]);
    if (btn & 0x08)
      ESP_LOGI(TAG, "  -> Button 4 (侧键1) 在报告中");
    if (btn & 0x10)
      ESP_LOGI(TAG, "  -> Button 5 (侧键2) 在报告中");
    last_btn_sent = btn;
  }

  esp_err_t ret = mouse_accumulator_send_ble_report(ble_mouse_report, 6);

  if (ret == ESP_OK)
  {
    // ========== 7. 发送成功: 提交阶段(两阶段提交的第二阶段) ==========

    // 真正pop掉消费的事件
    if (num_to_consume > 0)
    {
      ring_pop_batch(num_to_consume);
    }

    // 更新时间窗截止时间
    g_acc.t_last_send_us = t_now;

    // 更新残差
    g_acc.residual_dx = new_residual_dx;
    g_acc.residual_dy = new_residual_dy;
    g_acc.residual_wheel = new_residual_wheel;

    // 更新按钮状态
    g_acc.last_known_buttons = btn;

    // 统计
    g_acc.total_packets_sent++;

    // 调试日志
    ESP_LOGD(TAG, "BLE发送成功: dx=%d, dy=%d, wh=%d, btn=0x%02X | 消费%lu事件, 残差[%ld,%ld,%ld]",
             dx_send, dy_send, wheel_send, btn,
             (unsigned long)num_to_consume,
             (long)new_residual_dx, (long)new_residual_dy, (long)new_residual_wheel);
  }
  else
  {
    // ========== 8. 发送失败: 不提交,保持状态不变 ==========

    // 不pop事件,不更新t_last_send,不更新residual
    // 下次tick重试时,会重新计算窗口(可能包含更多事件)

    g_acc.total_send_failures++;

    // 对于通知未启用的情况,不记录警告(这是正常状态)
    if (ret != ESP_ERR_INVALID_STATE)
    {
      ESP_LOGW(TAG, "BLE发送失败(错误: %s), 保持状态不变,下次重试", esp_err_to_name(ret));
    }
  }
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
