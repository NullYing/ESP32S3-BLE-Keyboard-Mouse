/*
 * HID Configuration - Unified Macro Definitions
 *
 * 集中定义 HID 相关的配置宏，避免多文件重复定义导致的维护风险
 * 所有需要这些配置的模块都应该包含此头文件
 */

#ifndef HID_CONFIG_H
#define HID_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================================================
   鼠标精度配置
   =================================================================================================
 */

/**
 * @brief 使用16位精度（1）或8位精度（0）
 *
 * 注意：Report Map 中 X/Y 定义为 16bit 以兼容 8bit 和 16bit
 * - USE_16BIT_MOUSE_PRECISION=1：发送完整的 16bit 数据
 * - USE_16BIT_MOUSE_PRECISION=0：发送 8bit 数据但放在 16bit 字段中
 */
#ifndef USE_16BIT_MOUSE_PRECISION
#define USE_16BIT_MOUSE_PRECISION 1
#endif

/* =================================================================================================
   HID 报告长度定义
   =================================================================================================
 */

/**
 * @brief 键盘输入报告长度
 *
 * 标准 HID 键盘报告：修饰键(1) + 保留(1) + 按键(6) = 8字节
 */
#define HID_KEYBOARD_IN_RPT_LEN 8

/**
 * @brief 鼠标输入报告长度
 *
 * 基于 report map：按钮(1字节: 5位按钮+3位padding) + X(2字节, 16bit) +
 * Y(2字节, 16bit) + Wheel(1字节) = 6字节
 *
 * 注意：即使发送 8bit 数据，报告长度仍为 6 字节（8bit 数据放在 16bit 字段的低 8
 * 位）
 */
#define HID_MOUSE_IN_RPT_LEN 6

/**
 * @brief Consumer Control 输入报告长度
 */
#define HID_CC_IN_RPT_LEN 2

/**
 * @brief LED 输出报告长度
 */
#define HID_LED_OUT_RPT_LEN 1

/* =================================================================================================
   HID Report ID 定义
   =================================================================================================
 */

/**
 * @brief Report ID 的位宽（1 字节 = 8 位）
 *
 * 根据 HID 规范，Report ID 通常是 1 字节（8 位）
 * Parser 返回的 bit_offset 是相对于报告数据开始（不包括 report_id）的
 * 因此如果 report_id 存在，需要调整 8 位以跳过 report_id
 */
#define HID_REPORT_ID_SIZE_BITS 8

/* =================================================================================================
   BLE HID 设备配置
   =================================================================================================
 */

/**
 * @brief BLE HID 设备名称
 */
#define BLE_HID_DEVICE_NAME "BLE HID"

/**
 * @brief 最大鼠标布局数量
 */
#define MAX_MOUSE_LAYOUTS 16

#ifdef __cplusplus
}
#endif

#endif // HID_CONFIG_H
