/*
 * Mouse Motion Accumulator - Header File
 *
 * 用于解决BLE与USB回报率不一致的问题
 * 核心思想：USB输入与BLE发送彻底解耦
 * - USB侧：只累加数据到全局累加器
 * - BLE侧：按固定节拍从累加器取数并发送
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

// BLE发送节拍周期（微秒）
// 7.5ms = 7500us，对应约133Hz的发送频率，与BLE连接间隔匹配
#define BLE_SEND_INTERVAL_US 7500

   /* =================================================================================================
      类型定义
      ================================================================================================= */

   /**
    * @brief 鼠标运动累加器结构体
    *
    * 用于累积USB输入并在BLE节拍点统一发送
    * 使用int32避免高速移动时溢出
    */
   typedef struct
   {
      // 累积的位移（使用int32来避免溢出）
      int32_t acc_dx;
      int32_t acc_dy;
      int32_t acc_wheel;

      // 按钮状态（低3位：左、右、中键）
      uint8_t buttons;

      // 标志位
      bool motion_dirty;  // 是否有位移/滚轮数据待发送
      bool buttons_dirty; // 是否有按钮变化待发送

      // 互斥锁（保护并发访问）
      portMUX_TYPE spinlock;
   } mouse_motion_accumulator_t;

   /* =================================================================================================
      函数声明
      ================================================================================================= */

   /**
    * @brief 初始化鼠标累加器模块
    *
    * 创建并启动BLE发送定时器
    *
    * @return ESP_OK 成功，其他值表示失败
    */
   esp_err_t mouse_accumulator_init(void);

   /**
    * @brief 清理鼠标累加器（用于断线重连等场景）
    *
    * 清零所有累加值和标志位
    */
   void mouse_accumulator_clear(void);

   /**
    * @brief USB鼠标数据累加函数（线程安全）
    *
    * 只负责累加数据，不触发BLE发送
    * BLE发送由定时器按固定节拍触发
    *
    * @param dx X轴位移（int16）
    * @param dy Y轴位移（int16）
    * @param wheel 滚轮位移（int8）
    * @param buttons 按钮状态（低3位有效）
    */
   void mouse_accumulator_add(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons);

   /**
    * @brief BLE发送定时器回调函数（内部使用）
    *
    * 由esp_timer定期调用，触发节拍发送
    *
    * @param arg 未使用
    */
   void mouse_accumulator_timer_callback(void *arg);

   /**
    * @brief 从累加器取数并通过BLE发送（内部使用）
    *
    * 核心发送逻辑：
    * 1. 检查是否有数据需要发送
    * 2. 从累加器取出数据并清零
    * 3. 饱和处理：超出范围的部分写回累加器
    * 4. 发送失败时回滚数据
    *
    * 此函数由定时器回调或connection event触发
    * 外部不应直接调用
    */
   void mouse_accumulator_try_send(void);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_ACCUMULATOR_H__ */
