/*
 * Mouse Motion Accumulator - Implementation File
 *
 * 用于解决BLE与USB回报率不一致的问题
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

static const char *TAG = "MOUSE_ACC";

// 全局累加器实例
static mouse_motion_accumulator_t g_accumulator = {
    .acc_dx = 0,
    .acc_dy = 0,
    .acc_wheel = 0,
    .buttons = 0,
    .motion_dirty = false,
    .buttons_dirty = false,
    .spinlock = portMUX_INITIALIZER_UNLOCKED};

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

/* =================================================================================================
   公共API实现
   ================================================================================================= */

esp_err_t mouse_accumulator_init(void)
{
  // 创建BLE发送定时器
  const esp_timer_create_args_t timer_args = {
      .callback = &mouse_accumulator_timer_callback,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ble_send_timer"};

  esp_err_t ret = esp_timer_create(&timer_args, &s_send_timer);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "创建BLE发送定时器失败: %s", esp_err_to_name(ret));
    return ret;
  }

  // 启动定时器（周期性触发）
  ret = esp_timer_start_periodic(s_send_timer, BLE_SEND_INTERVAL_US);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "启动BLE发送定时器失败: %s", esp_err_to_name(ret));
    esp_timer_delete(s_send_timer);
    s_send_timer = NULL;
    return ret;
  }

  ESP_LOGI(TAG, "鼠标累加器初始化成功，发送周期: %d us (约%.1f Hz)",
           BLE_SEND_INTERVAL_US, 1000000.0 / BLE_SEND_INTERVAL_US);

  return ESP_OK;
}

void mouse_accumulator_clear(void)
{
  portENTER_CRITICAL(&g_accumulator.spinlock);
  {
    g_accumulator.acc_dx = 0;
    g_accumulator.acc_dy = 0;
    g_accumulator.acc_wheel = 0;
    g_accumulator.buttons = 0;
    g_accumulator.motion_dirty = false;
    g_accumulator.buttons_dirty = false;
  }
  portEXIT_CRITICAL(&g_accumulator.spinlock);

  ESP_LOGI(TAG, "累加器已清零");
}

void mouse_accumulator_add(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons)
{
  portENTER_CRITICAL(&g_accumulator.spinlock);
  {
    // 累加位移和滚轮
    g_accumulator.acc_dx += (int32_t)dx;
    g_accumulator.acc_dy += (int32_t)dy;
    g_accumulator.acc_wheel += (int32_t)wheel;

    // 检查是否有运动
    if (dx != 0 || dy != 0 || wheel != 0)
    {
      g_accumulator.motion_dirty = true;
    }

    // 检查按钮变化
    if (g_accumulator.buttons != buttons)
    {
      g_accumulator.buttons = buttons;
      g_accumulator.buttons_dirty = true;
    }
  }
  portEXIT_CRITICAL(&g_accumulator.spinlock);

  // 调试日志（高频数据，正式版应关闭）
  // ESP_LOGV(TAG, "USB累加: dx=%d, dy=%d, wheel=%d, buttons=0x%02X",
  //          dx, dy, wheel, buttons);
}

void mouse_accumulator_timer_callback(void *arg)
{
  mouse_accumulator_try_send();
}

void mouse_accumulator_try_send(void)
{
  // 条件检查：必须已连接且已订阅notify
  if (!mouse_accumulator_is_ble_connected())
  {
    return; // BLE未连接，直接返回
  }

  // 临界区：读取并清空累加器
  int32_t dx_total, dy_total, wheel_total;
  uint8_t buttons_current;
  bool has_motion, has_buttons_change;

  portENTER_CRITICAL(&g_accumulator.spinlock);
  {
    has_motion = g_accumulator.motion_dirty;
    has_buttons_change = g_accumulator.buttons_dirty;

    // 如果没有任何数据需要发送，直接返回
    if (!has_motion && !has_buttons_change)
    {
      portEXIT_CRITICAL(&g_accumulator.spinlock);
      return;
    }

    // 取出累积的数据
    dx_total = g_accumulator.acc_dx;
    dy_total = g_accumulator.acc_dy;
    wheel_total = g_accumulator.acc_wheel;
    buttons_current = g_accumulator.buttons;

    // 清零累加器（稍后可能会写回剩余部分）
    g_accumulator.acc_dx = 0;
    g_accumulator.acc_dy = 0;
    g_accumulator.acc_wheel = 0;
    g_accumulator.motion_dirty = false;
    g_accumulator.buttons_dirty = false;
  }
  portEXIT_CRITICAL(&g_accumulator.spinlock);

  // 饱和处理：将超出范围的部分限制到int16/int8范围，剩余部分写回
  // X轴：int16范围是 -32767..32767
  int16_t dx_send = (int16_t)clamp_s32(dx_total, -32767, 32767);
  int32_t dx_remain = dx_total - dx_send;

  // Y轴：int16范围是 -32767..32767
  int16_t dy_send = (int16_t)clamp_s32(dy_total, -32767, 32767);
  int32_t dy_remain = dy_total - dy_send;

  // 滚轮：int8范围是 -127..127
  int8_t wheel_send = (int8_t)clamp_s32(wheel_total, -127, 127);
  int32_t wheel_remain = wheel_total - wheel_send;

  // 如果有剩余，写回累加器
  if (dx_remain != 0 || dy_remain != 0 || wheel_remain != 0)
  {
    portENTER_CRITICAL(&g_accumulator.spinlock);
    {
      g_accumulator.acc_dx += dx_remain;
      g_accumulator.acc_dy += dy_remain;
      g_accumulator.acc_wheel += wheel_remain;
      g_accumulator.motion_dirty = true; // 标记还有剩余数据
    }
    portEXIT_CRITICAL(&g_accumulator.spinlock);
  }

  // 构建BLE鼠标报告（6字节：按钮1 + X低1 + X高1 + Y低1 + Y高1 + 滚轮1）
  uint8_t ble_mouse_report[6] = {0}; // HID_MOUSE_IN_RPT_LEN

  // 字节0：按钮（低3位）
  ble_mouse_report[0] = buttons_current & 0x07;

  // 字节1-2：X位移（16位，little-endian）
  ble_mouse_report[1] = (uint8_t)(dx_send & 0xFF);
  ble_mouse_report[2] = (uint8_t)((dx_send >> 8) & 0xFF);

  // 字节3-4：Y位移（16位，little-endian）
  ble_mouse_report[3] = (uint8_t)(dy_send & 0xFF);
  ble_mouse_report[4] = (uint8_t)((dy_send >> 8) & 0xFF);

  // 字节5：滚轮（8位）
  ble_mouse_report[5] = (uint8_t)wheel_send;

  // 发送到BLE（现在会返回发送状态）
  esp_err_t ret = mouse_accumulator_send_ble_report(ble_mouse_report, 6);

  // 如果发送失败，回滚数据到累加器
  if (ret != ESP_OK)
  {
    ESP_LOGW(TAG, "BLE发送失败 (错误: %s)，回滚数据到累加器", esp_err_to_name(ret));

    portENTER_CRITICAL(&g_accumulator.spinlock);
    {
      // 将本次要发送的数据加回累加器
      g_accumulator.acc_dx += dx_send;
      g_accumulator.acc_dy += dy_send;
      g_accumulator.acc_wheel += wheel_send;

      // 恢复按钮状态和dirty标志
      g_accumulator.buttons = buttons_current;
      g_accumulator.motion_dirty = true;
      g_accumulator.buttons_dirty = true;
    }
    portEXIT_CRITICAL(&g_accumulator.spinlock);
  }
  else
  {
    // 发送成功，记录日志（可选）
    if (dx_send != 0 || dy_send != 0 || wheel_send != 0 || has_buttons_change)
    {
      ESP_LOGD(TAG, "BLE发送成功: dx=%d, dy=%d, wheel=%d, buttons=0x%02X (剩余: dx=%ld, dy=%ld, wheel=%ld)",
               dx_send, dy_send, wheel_send, buttons_current,
               (long)dx_remain, (long)dy_remain, (long)wheel_remain);
    }
  }
}
