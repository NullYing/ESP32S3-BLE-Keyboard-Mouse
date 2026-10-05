/*
 * Mouse Motion Accumulator - Header File (方案A: Ring Buffer + 时间窗重采样)
 *
 * 核心思想:
 * - USB输入看成带时间戳的事件流,存入ring buffer
 * - BLE发送时,对[last_send_t, now_t]时间窗内的事件积分求和
 * - 使用两阶段提交保证数据不丢失
 * - 使用残差累积器处理饱和后的剩余
 */

#ifndef MOUSE_ACCUMULATOR_H__
#define MOUSE_ACCUMULATOR_H__

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* =================================================================================================
   常量定义
   ================================================================================================= */

// BLE发送节拍周期(微秒)
// 7.5ms = 7500us,对应约133Hz的发送频率,与BLE连接间隔匹配
// 注意: 实际值会根据BLE连接参数更新事件动态调整
#define BLE_SEND_INTERVAL_US_DEFAULT 7500

// Ring buffer容量 (必须是2的幂,方便取模优化)
// 128条事件可以覆盖最坏情况: 1000Hz * 0.1s = 100条 + 余量
#define RING_BUFFER_CAPACITY 128
#define RING_BUFFER_MASK (RING_BUFFER_CAPACITY - 1)

   /* =================================================================================================
      类型定义
      ================================================================================================= */

   /**
    * @brief Ring buffer中的单个鼠标事件
    *
    * 每个USB报告对应一条事件记录
    */
   typedef struct
   {
      uint64_t t_us;   // 时间戳(微秒)
      int16_t dx;      // X位移(原始值)
      int16_t dy;      // Y位移(原始值)
      int8_t wheel;    // 滚轮位移
      uint8_t buttons; // 按钮状态(低5位: 左右中及侧键)
      uint8_t flags;   // 标志位: bit0=button_changed
   } mouse_event_t;

// 事件标志位定义
#define EVENT_FLAG_BUTTON_CHANGED 0x01

   /**
    * @brief Ring buffer结构体
    *
    * 生产者(USB task): 写入head位置,head++（满时丢弃新事件）
    * 消费者(BLE task): 读取tail位置,tail++
    * 满条件: count == CAPACITY
    * 空条件: count == 0
    */
   typedef struct
   {
      mouse_event_t events[RING_BUFFER_CAPACITY]; // 事件数组
      volatile uint32_t head;                     // 写入索引(生产者)
      volatile uint32_t tail;                     // 读取索引(消费者)
      volatile uint32_t count;                    // 当前事件数
      volatile uint32_t overflow_count;           // 溢出计数(调试用)
      portMUX_TYPE spinlock;                      // 保护并发访问
   } mouse_event_ring_t;

   /**
    * @brief 鼠标累加器主结构(方案A)
    *
    * 包含ring buffer和发送状态管理
    */
   typedef struct
   {
      // Ring buffer
      mouse_event_ring_t ring;

      // 发送时间窗管理
      uint64_t t_last_send_us; // 上次发送的截止时间(微秒)

      // 残差累积器(处理饱和后的剩余)
      int32_t residual_dx;
      int32_t residual_dy;
      int32_t residual_wheel;

      // 按钮状态管理
      uint8_t last_known_buttons; // 最后一次发送的按钮状态
      uint8_t last_usb_buttons;   // 最后一次USB报告的按钮状态

      // 统计信息(调试用)
      uint32_t total_events_pushed; // 总推入事件数
      uint32_t total_events_popped; // 总弹出事件数
      uint32_t total_packets_sent;  // 总发送包数
      uint32_t total_send_failures; // 总发送失败数

   } mouse_motion_accumulator_t;

   /* =================================================================================================
      函数声明
      ================================================================================================= */

   /**
    * @brief 初始化鼠标累加器模块(方案A)
    *
    * 创建ring buffer和BLE发送定时器
    *
    * @return ESP_OK 成功,其他值表示失败
    */
   esp_err_t mouse_accumulator_init(void);

   /**
    * @brief 清理鼠标累加器(用于断线重连等场景)
    *
    * 清空ring buffer和残差累积器，保留USB当前按钮状态
    */
   void mouse_accumulator_clear(void);

   /**
    * @brief Set the authenticated connection lifecycle state.
    *
    * Disconnect clears relative motion while USB button state remains current.
    * Reconnect synchronizes that state before accepting queued button edges.
    * Do not call this for transient send-lock contention or notification readiness.
    */
   void mouse_accumulator_set_connected(bool connected);

   /** Validate a snapshot while BLE's send mutex protects its destination. */
   bool mouse_accumulator_session_is_current(uint32_t generation);

   /**
    * @brief USB鼠标数据推入ring buffer(Producer,线程安全)
    *
    * 只负责push事件到ring,不触发BLE发送
    * BLE发送由定时器按固定节拍触发
    *
    * @param dx X轴位移(int16)
    * @param dy Y轴位移(int16)
    * @param wheel 滚轮位移(int8)
    * @param buttons 按钮状态(低5位有效)
    */
   void mouse_accumulator_add(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons);

   /**
    * @brief BLE发送定时器回调函数(内部使用)
    *
    * 由esp_timer定期调用,触发节拍发送
    *
    * @param arg 未使用
    */
   void mouse_accumulator_timer_callback(void *arg);

   /**
    * @brief 从ring buffer取事件并通过BLE发送(Consumer,内部使用)
    *
    * 核心发送逻辑(方案A):
    * 1. 在锁内快照同一按钮状态的事件批次，遇到下一按钮转换停止
    * 2. 先排空前一批的残差，再发送下一按钮转换
    * 3. 饱和处理: clamp到int16/int8范围，保留新残差
    * 4. 锁外尝试notify，临时未就绪保持队列
    * 5. 成功且连接generation未变: pop快照批次并提交残差
    * 6. 失败或并发clear: 不提交，防止误删新事件
    *
    * 此函数由定时器回调触发
    */
   void mouse_accumulator_try_send(void);

   /**
    * @brief 获取ring buffer统计信息(调试用)
    */
   void mouse_accumulator_get_stats(uint32_t *events_in_ring,
                                    uint32_t *overflow_count,
                                    uint32_t *total_pushed,
                                    uint32_t *total_popped,
                                    uint32_t *total_sent,
                                    uint32_t *total_failures);

   /**
    * @brief 根据实际BLE连接间隔动态更新发送间隔
    *
    * 在BLE连接参数更新完成后调用，根据实际协商的连接间隔调整发送定时器
    *
    * @param conn_interval_units BLE连接间隔(单位: 1.25ms)
    *                            例如: 0x0006 = 6 * 1.25ms = 7.5ms
    * @return ESP_OK 成功,其他值表示失败
    */
   esp_err_t mouse_accumulator_update_send_interval(uint16_t conn_interval_units);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_ACCUMULATOR_H__ */
