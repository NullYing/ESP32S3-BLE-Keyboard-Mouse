# BLE与USB回报率不一致问题的解决方案

## 问题描述

原始代码中,USB鼠标输入到来时会立即通过BLE发送,导致"USB来一包就BLE发一包"。这会造成:
- BLE堆栈排队/合并/抖动
- 手感忽快忽慢,体感差异明显
- 快速移动鼠标时位移可能丢失

## 解决方案核心思想

**USB输入与BLE发送彻底解耦**：
- **USB侧**：只负责累加位移数据到全局累加器
- **BLE侧**：按照固定节拍(7.5ms)从累加器取数并发送
- **结果**：运动位移不丢失(积分一致) + 发送节拍稳定

## 核心修改内容

### 1. 新增运动累加器模块 (Motion Accumulator)

**文件**: `main/mouse_accumulator.h` 和 `main/mouse_accumulator.c`

```c
typedef struct {
    int32_t acc_dx;      // 累积X位移 (int32避免溢出)
    int32_t acc_dy;      // 累积Y位移
    int32_t acc_wheel;   // 累积滚轮
    uint8_t buttons;     // 按钮状态(低3位)
    bool motion_dirty;   // 是否有位移数据待发送
    bool buttons_dirty;  // 是否有按钮变化待发送
    portMUX_TYPE spinlock; // 线程安全保护
} mouse_motion_accumulator_t;
```

**关键特性**:
- 使用 `int32_t` 累加,避免高速移动时溢出
- 使用 spinlock 保证线程安全
- dirty 标志用于判断是否需要发送

### 2. USB侧改造：只累加不发送

**修改的函数**: `hid_host_mouse_report_callback()` in `main/hid_host_example.c`

**改动前**:
```c
// 直接发送到BLE
hid_dev_send_report(hidd_le_env.gatt_if, ble_hid_conn_id, ...);
```

**改动后**:
```c
// 只累加到全局累加器
mouse_accumulator_add(x, y, wheel, buttons_final);
```

**累加函数**: `mouse_accumulator_add()` in `main/mouse_accumulator.c`
- 线程安全的累加操作
- 进入临界区,累加 dx/dy/wheel
- 检测按钮变化
- 设置 dirty 标志
- 退出临界区

### 3. BLE侧改造：节拍发送

**核心函数**: `mouse_accumulator_try_send()` in `main/mouse_accumulator.c`

**核心流程**:
1. **条件检查**: 未连接直接返回
2. **取出数据**: 临界区内取出累加器数据并清零
3. **饱和处理**: 
   - X/Y限制到 int16 范围 (-32767..32767)
   - Wheel限制到 int8 范围 (-127..127)
   - 超出部分写回累加器
4. **打包报告**: 构建6字节BLE鼠标报告
5. **发送BLE**: 调用 `hid_dev_send_report()`
6. **失败回滚**: 发送失败时将数据加回累加器

**关键点**:
```c
// 饱和处理示例 (X轴)
int16_t dx_send = clamp_s32(dx_total, -32767, 32767);
int32_t dx_remain = dx_total - dx_send;

// 剩余部分写回累加器
if (dx_remain != 0) {
    g_mouse_accumulator.acc_dx += dx_remain;
    g_mouse_accumulator.motion_dirty = true;
}
```

### 4. 定时器：稳定节拍

**初始化函数**: `mouse_accumulator_init()` in `main/mouse_accumulator.c`

```c
// 主程序中调用（简化接口）
ESP_ERROR_CHECK(mouse_accumulator_init());

// 内部实现：创建并启动定时器
esp_timer_create(&timer_args, &s_send_timer);
esp_timer_start_periodic(s_send_timer, BLE_SEND_INTERVAL_US);
```

**定时器参数**:
- 周期: 7500微秒 (7.5ms)
- 频率: 约133Hz
- 与BLE连接间隔匹配 (min_int=max_int=7.5ms)

### 5. 发送失败处理

**修改的函数**: `hid_dev_send_report()` 
- **修改前**: `void hid_dev_send_report(...)`
- **修改后**: `esp_err_t hid_dev_send_report(...)`

**回滚逻辑** (在 `ble_try_send_mouse()` 中):
```c
esp_err_t ret = hid_dev_send_report(...);
if (ret != ESP_OK) {
    // 发送失败,回滚数据到累加器
    portENTER_CRITICAL(&g_mouse_accumulator.spinlock);
    g_mouse_accumulator.acc_dx += dx_send;
    g_mouse_accumulator.acc_dy += dy_send;
    g_mouse_accumulator.acc_wheel += wheel_send;
    g_mouse_accumulator.buttons = buttons_current;
    g_mouse_accumulator.motion_dirty = true;
    g_mouse_accumulator.buttons_dirty = true;
    portEXIT_CRITICAL(&g_mouse_accumulator.spinlock);
}
```

### 6. 连接管理

**BLE断开时清理累加器** (使用模块化API):
```c
case ESP_HIDD_EVENT_BLE_DISCONNECT:
    // 清零累加器,避免断线重连后发送旧数据
    mouse_accumulator_clear();
    break;
```

**清理函数**: `mouse_accumulator_clear()` in `main/mouse_accumulator.c`
- 线程安全地清零所有累加值
- 重置所有标志位

## 修改的文件清单

### 新增文件（模块化设计）

1. **main/mouse_accumulator.h** (头文件)
   - 鼠标累加器模块的公共接口
   - 数据结构定义
   - API 函数声明

2. **main/mouse_accumulator.c** (实现文件)
   - 运动累加器数据结构实现
   - `mouse_accumulator_init()` - 初始化模块
   - `mouse_accumulator_clear()` - 清零累加器
   - `mouse_accumulator_add()` - USB数据累加
   - `mouse_accumulator_try_send()` - BLE节拍发送
   - `mouse_accumulator_timer_callback()` - 定时器回调
   - 饱和处理和失败回滚逻辑

3. **main/hid_host_example.h** (新增头文件)
   - 提供给 mouse_accumulator 模块的辅助函数接口
   - `mouse_accumulator_is_ble_connected()` - BLE连接状态查询
   - `mouse_accumulator_send_ble_report()` - BLE报告发送

### 修改的文件

1. **main/hid_host_example.c** (主文件简化)
   - 引入 `mouse_accumulator.h` 和 `hid_host_example.h`
   - 实现辅助函数供累加器模块调用
   - 修改 `hid_host_mouse_report_callback()` 调用 `mouse_accumulator_add()`
   - 修改 `ble_hid_event_callback()` 调用 `mouse_accumulator_clear()`
   - 修改 `app_main()` 调用 `mouse_accumulator_init()`
   - **代码行数大幅减少，职责更清晰**

2. **main/hid_dev.c**
   - 修改 `hid_dev_send_report()` 返回 `esp_err_t`

3. **main/hid_dev.h**
   - 更新 `hid_dev_send_report()` 函数声明

## 模块化设计优势

✅ **代码组织更清晰**
- 鼠标累加器相关功能独立成一个模块
- 职责单一，易于理解和维护

✅ **降低耦合度**
- 主文件只需调用简单的API接口
- 内部实现细节完全封装

✅ **便于测试和调试**
- 可以独立测试累加器模块
- 日志标签独立（MOUSE_ACC）

✅ **便于复用**
- 累加器模块可以在其他项目中复用
- 接口设计清晰，易于移植

## 技术要点总结

### ✅ 实现的核心要求

1. **USB和BLE彻底解耦** ✅
   - USB回调只累加,不发送
   - BLE按自己的节拍发送

2. **运动位移不丢失** ✅
   - 使用 int32 累加避免溢出
   - 饱和处理后剩余部分写回累加器
   - 发送失败时回滚数据

3. **发送节拍稳定** ✅
   - 定时器周期固定在7.5ms
   - 与BLE连接间隔匹配
   - 每个节拍最多发1包

4. **线程安全** ✅
   - 使用 spinlock 保护累加器
   - 所有读写操作都在临界区内

5. **按钮优先** ✅
   - 即使 dx=dy=0,只要 `buttons_dirty` 也会发送
   - 避免点击延迟

6. **失败处理** ✅
   - 发送失败时回滚数据
   - 下个节拍重试

### 🎯 预期效果

- **体感接近USB鼠标**: 稳定的133Hz发送频率
- **不丢位移**: 快速甩鼠标时积分一致
- **响应稳定**: 不会出现忽快忽慢的抖动
- **按钮灵敏**: 点击延迟最小化

## 调试建议

### 日志级别控制

代码中已经添加了调试日志,可通过menuconfig调整:
```
Component config → Log output → Default log verbosity
```

- **VERBOSE**: 显示所有USB累加日志 (高频,影响性能)
- **DEBUG**: 显示BLE发送日志
- **INFO**: 只显示关键事件
- **WARN**: 只显示告警和错误

### 性能监控

可以添加以下监控点:
1. 累加器的峰值 (检查是否会溢出)
2. 发送失败率 (检查回滚频率)
3. 定时器精度 (检查节拍是否稳定)

## 可能的优化方向

1. **动态调整节拍周期**
   - 根据BLE连接质量动态调整
   - 低延迟优先 vs 省电优先

2. **自适应饱和阈值**
   - 根据实际使用情况调整 clamp 范围
   - 避免过度分包

3. **Connection Event回调**
   - 如果ESP-IDF支持,可用connection event替代定时器
   - 更精确地与BLE连接间隔同步

## 测试验证

建议进行以下测试:
1. **慢速移动**: 验证精度不丢失
2. **快速甩动**: 验证大幅移动不丢步
3. **连续点击**: 验证按钮响应及时
4. **断线重连**: 验证累加器正确清零
5. **长时间使用**: 验证无内存泄漏或累加器异常

---

**创建时间**: 2025-12-13
**最后更新**: 2025-12-13
