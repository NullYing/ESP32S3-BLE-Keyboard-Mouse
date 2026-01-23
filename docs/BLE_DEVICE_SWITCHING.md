# BLE 双设备切换功能文档

## 功能概述

ESP32-S3 BLE HID 设备支持两个设备槽位 (Slot A / Slot B)，可通过快捷键在两个已配对设备之间切换。

> **注意**：ESP32 BLE HID 作为外设，同一时间只能连接一个主机。"双设备"指维护两个设备记录，按需切换。

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| **Alt + `** | 在 Slot A ↔ Slot B 之间切换 |
| **Alt + N** | 发现新设备（优先填入空槽位） |

---

## 核心文件

| 文件 | 职责 |
|------|------|
| `ble_device_manager.h` | 设备管理器接口定义 |
| `ble_device_manager.c` | 双槽位逻辑实现 |
| `hid_host_example.c` | 快捷键检测、GAP 事件处理 |

---

## 状态变量

```c
static device_slot_t s_slots[2];     // 两个设备槽位
static int s_active_slot = 0;        // 当前活动槽位 (0=A, 1=B)
static bool s_is_switching = false;  // 正在切换中
static bool s_is_discovering = false;// 正在发现新设备
static int s_target_slot = -1;       // 切换目标槽位

extern bool sec_conn;                // BLE 连接状态（来自 hid_host_example.c）
```

---

## 切换流程 (Alt + `)

> **提示**：如果当前正在切换等待目标设备，再次按 Alt+` 可以切换到另一个槽位。

```
1. 检查另一个槽位是否有设备
   └── 没有 → 提示用户先用 Alt+N 配对
   
2. 设置状态
   ├── s_is_switching = true
   ├── s_target_slot = other_slot
   └── sec_conn = false ⚠️ 立即阻止报告发送

3. 断开当前连接
   └── esp_ble_gap_disconnect()

4. 开始广播
   └── 等待目标设备重连

5. 设备连接时 (on_connected)
   ├── 检查是否为目标设备
   │   ├── 是 → 接受连接，sec_conn = true ✅
   │   └── 否 → 拒绝连接，继续等待

6. 切换等待期间可以：
   ├── Alt+` → 切换到另一个槽位
   └── Alt+N → 取消切换，接受任何新设备
```

---

## 发现新设备流程 (Alt + N)

> **提示**：如果当前正在切换等待目标设备，按 Alt+N 可以取消切换并进入发现模式，新设备将保存到原本的切换目标槽位。

```
1. 设置状态
   ├── s_is_discovering = true
   ├── s_is_switching = false (取消切换状态)
   └── sec_conn = false ⚠️ 立即阻止报告发送

2. 断开当前连接（如有）

3. 开始广播
   └── 接受任何新设备

4. 设备连接时 (on_connected)
   ├── 如果 s_target_slot 已设置 → 使用该槽位
   │   └── 用于从切换模式转发现模式的场景
   ├── 否则优先填入空槽位
   │   └── 如果两个槽位都满，替换当前槽位
   ├── 保存到 NVS
   └── sec_conn = true ✅
```

---

## 关键注意点

### 1. sec_conn 状态管理

```c
// 切换/发现开始时：立即设为 false
sec_conn = false;  // 阻止报告发送

// 切换/发现成功时：设回 true
sec_conn = true;   // 恢复报告发送
```

**为什么重要**：防止在断开和重连之间的时间窗口内，鼠标定时器尝试发送报告导致错误。

### 2. 报告发送检查

鼠标累加器在发送前检查：
```c
bool mouse_accumulator_is_ble_connected(void) {
  return sec_conn && !ble_device_manager_is_switching();
}
```

### 3. 非目标设备拒绝

切换过程中如果连接的不是目标设备，会被拒绝：
```c
if (!bda_equal(s_slots[s_target_slot].bda, bda)) {
    esp_ble_gap_disconnect(...);
    start_advertising();  // 继续等待
}
```

### 4. NVS 持久化

设备信息使用 NVS 持久化：
- `slot_a`: Slot A 设备地址
- `slot_b`: Slot B 设备地址
- `active`: 当前活动槽位

---

## LED 反馈

切换/发现时 LED 闪烁蓝色 3 次，表示操作进行中。

---

## 调试建议

1. **查看槽位状态**：启动时日志显示 `Slot A: XX:XX:XX:XX:XX:XX` 或 `(empty)`
2. **切换失败**：检查目标设备蓝牙是否开启
3. **报告发送失败**：检查 `sec_conn` 和 `s_is_switching` 状态
