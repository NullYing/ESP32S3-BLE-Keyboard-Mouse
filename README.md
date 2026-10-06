# 🔌 USB-to-BLE HID Bridge (ESP32-S3 / ESP32-S31)

**English** | [中文](#中文)

---

## English

### Overview

This project transforms an **ESP32-S3 or ESP32-S31** into a powerful bridge between **USB HID devices** (keyboards and mice) and **Bluetooth Low Energy (BLE) HID**, enabling any wired USB keyboard or mouse to function as a wireless BLE HID device. Simply plug your USB device into the board and connect to your Mac, PC, tablet, or phone via Bluetooth.

**Key Highlights:**

- ✅ **Two Host Slots** – Save Slot A / Slot B and switch with Alt + backtick; use Alt + N to pair a new host
- ✅ **ESP32-S31 Support** – Separate ESP-IDF 6.1 preview configuration for Function-CoreBoard-1
- ✅ **Full macOS Compatibility** – Optimized for macOS with Report Protocol support
- ✅ **High DPI Mouse Support** – 16-bit precision (-32767 to 32767) for smooth high-resolution mouse movement
- ✅ **Composite HID Device** – Simultaneously supports Keyboard, Mouse, Consumer Control, and Gamepad
- ✅ **Advanced Motion Processing** – Ring Buffer + Time Window resampling with button transitions and send retries
- ✅ **Multi-Button Mouse** – Supports up to 5 buttons (left, right, middle, side buttons 4 & 5)

---

### 🚀 Key Features

#### **USB Host Mode**

- Detects and reads input from standard USB HID keyboards and mice via ESP-IDF USB Host and the `usb_host_hid` driver
- Supports both Boot Protocol and Report Protocol modes
- Automatic protocol detection and switching

#### **BLE HID Emulation**

- Sends keystrokes and mouse movements over Bluetooth as a standard BLE HID device
- Compatible with macOS, Windows, Linux, iOS, and Android
- Low latency and stable connection

#### **macOS Optimization**

- **Report Protocol Support**: Automatically switches USB devices to Report Protocol mode for macOS compatibility
- **16-bit Mouse Precision**: Full support for high DPI mice with 16-bit X/Y coordinates
- **Adaptive Sending**: Requests a 7.5 ms BLE connection interval (~133 Hz); the mouse timer follows the interval negotiated with the host

#### **High DPI Mouse Compatibility**

- **16-bit Coordinate Range**: Supports mouse movement from -32767 to +32767 pixels per report
- **Motion Accumulator**: Ring Buffer + Time Window resampling with residual movement retries
- **Smooth Movement**: Accumulates high-frequency USB input and resamples to the negotiated BLE interval

#### **Two Host Slots and Switching**

- Stores two host records (Slot A / Slot B) and the active slot in NVS across restarts
- Connects to **one BLE host at a time**; switching waits for the selected host to reconnect
- Commits slot records and enables input only after the current host authenticates successfully
- Handles shortcuts locally, including while BLE is disconnected; holding a shortcut triggers it once until released

#### **Input and Connection Recovery**

- Parses Report IDs, keyboard arrays and NKRO input into the standard eight-byte BLE keyboard report
- Preserves keyboard state across independently updated Report IDs; BLE output supports six non-modifier keys
- Preserves mouse button transitions and retries remaining movement after send failures
- Clears old relative movement during BLE disconnects and switching
- Retries USB transfer recovery while keeping the device handle and parsed layout
- Forwards host Num Lock, Caps Lock and Scroll Lock output reports to compatible USB keyboards

#### **Status LED Indicators**

| LED Color    | Meaning                     |
| ------------ | --------------------------- |
| 🔴 **Red**   | USB & BLE both disconnected |
| 🟢 **Green** | USB device connected        |
| 🔵 **Blue**  | BLE device connected        |
| ⚪ **White** | Both USB and BLE connected  |
| 🔵 **Three blue flashes** | Switching slots or discovering a new host |

#### **Additional Features**

- Supports modifier keys (Ctrl, Alt, Shift, Cmd) and up to 6 simultaneous keypresses
- Consumer Control support (volume, media keys, etc.)
- Optional Gamepad support (configurable)
- Proper key release events for correct key repetition behavior
- Thread-safe motion accumulator with spinlock protection

---

### 🎯 Project Improvements

This project includes significant improvements over basic USB-to-BLE implementations:

#### **1. Motion Accumulator (Ring Buffer + Time Window)**

- **Problem Solved**: USB polling rate (often 1000Hz) doesn't match BLE transmission rate (~133Hz), causing movement loss and jittery behavior
- **Solution**: Ring Buffer stores USB events with microsecond timestamps, then integrates movement over time windows for stable BLE transmission
- **Benefits**:
  - Accumulates motion and drains residual movement across subsequent reports
  - Mouse sending follows the negotiated BLE interval independently of USB input frequency
  - Handles USB input jitter and bursts gracefully
  - Two-phase commit ensures data integrity even if BLE transmission fails

#### **2. macOS Compatibility**

- **Problem Solved**: macOS requires Report Protocol mode for USB HID devices, while many devices default to Boot Protocol
- **Solution**: Automatically detects and switches USB devices to Report Protocol mode
- **Benefits**: Seamless compatibility with macOS without manual configuration

#### **3. High DPI Mouse Support**

- **Problem Solved**: Standard 8-bit mouse coordinates (-127 to +127) are insufficient for high DPI mice, causing pixelation and loss of precision
- **Solution**: 16-bit HID Report Map with full 16-bit coordinate support
- **Benefits**: Smooth, precise mouse movement even with high DPI mice (4000+ DPI)

#### **4. Multi-Button Mouse Support**

- Extended button support beyond standard 3-button mice
- Supports side buttons (buttons 4 & 5) commonly found on gaming mice

#### **5. Composite HID Device**

- Single BLE device can act as Keyboard, Mouse, Consumer Control, and Gamepad simultaneously
- Proper HID Report Map with multiple Report IDs

#### **6. USB HID Report Map Parsing**

- **Problem Solved**: USB HID devices use complex report descriptors that need to be parsed to extract input data correctly, especially for Report Protocol mode devices
- **Solution**: Advanced HID report parser that can handle various report descriptor formats and extract keycodes, mouse movements, and button states accurately
- **Benefits**:
  - Supports both Boot Protocol and Report Protocol modes
  - Handles complex report descriptors with multiple Report IDs
  - Accurate extraction of all input fields including buttons, axes, and consumer controls
  - Compatible with a wide range of USB HID devices

---

### ⚙️ How It Works

1. **USB Initialization**  
   The board acts as a USB host using ESP-IDF USB Host to enumerate and communicate with connected HID devices.

2. **Device Detection & Protocol Switching**  
   Automatically detects device type (keyboard/mouse) and switches to Report Protocol mode for macOS compatibility.

3. **HID Report Parsing**  
   Incoming USB HID reports are parsed to extract keycodes, mouse movements, and button states.

4. **Motion Accumulation (Mouse Only)**  
   Mouse movements are accumulated in a Ring Buffer with timestamps, then integrated over time windows for stable BLE transmission.

5. **BLE HID Emulation**  
   The board translates parsed data into BLE HID reports and sends them via GATT notifications to the connected Bluetooth device.

---

### 🧠 Usage Guide

#### Requirements

- ESP32-S3 with native USB OTG, or ESP32-S31-Function-CoreBoard-1
- ESP-IDF 6.1 for the verified S3/S31 builds; S31 is a preview target
- USB keyboard/mouse + USB OTG adapter/cable
- Serial monitor (for debugging)

#### Build & Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

For **ESP32-S31-Function-CoreBoard-1**, use ESP-IDF 6.1 with the preview target
and the separate configuration described in [ESP32-S31 support](docs/ESP32_S31.md).

#### Pairing

1. Power the **board** and connect a **USB keyboard or mouse**.
2. On your **Mac, PC, or phone**, scan for Bluetooth devices.
3. Connect to the advertised **"BLE HID"** device.
4. The LED will turn white when both USB and BLE are connected. With empty slots, the first authenticated host is saved in Slot A.

#### Switching and Pairing a Second Host

| Shortcut | Action |
| --- | --- |
| Alt + backtick (the grave key, `` ` ``) | Switch between Slot A and Slot B |
| Alt + N | Discover a host to pair |

Either left or right Alt works. These shortcuts are consumed by the bridge; release the combination before triggering it again.

1. Pair the first host as described above.
2. Press **Alt + N** on the attached USB keyboard. The current host disconnects and the LED flashes blue three times.
3. Scan for **BLE HID** on the second host and pair it. After authentication, the empty Slot B is filled without replacing Slot A.
4. Press **Alt + backtick** to switch. Keep Bluetooth enabled on the target host; reconnect to **BLE HID** there if it does not reconnect automatically.

If both slots are occupied, **Alt + N replaces the active slot** after the new host authenticates. While waiting for a switch, Alt + N instead pairs into the selected target slot; Alt + backtick can select the other saved slot. Switching to an empty slot has no effect: use Alt + N to pair first.

Host records persist across restarts. Startup logs show Slot A / Slot B addresses and the active slot. For troubleshooting, first check the target host's Bluetooth connection and the serial authentication logs.

---

### ⚠️ Limitations

- Only supports standard USB HID devices (Boot Protocol and Report Protocol)
- BLE battery service is not implemented
- Some vendor-specific keys or features may not work
- Two saved host slots, with one active BLE connection; the host controls reconnection
- NKRO USB input is converted to six-key BLE output (plus modifiers)
- Finite mouse buffering can drop new motion when overloaded
- On S31, Full/Low-Speed devices behind High-Speed hubs require unsupported split transactions; see [S31 limitations](docs/ESP32_S31.md)

---

### Verification

ESP-IDF 6.1 builds pass for both ESP32-S3 and ESP32-S31. Five host regression suites cover input decoding and shortcuts, mouse accumulation, USB recovery, BLE sessions, and application USB integration. Physical pairing, LED behavior and USB/BLE interaction still require hardware validation, including on S31.

See [review and regression details](docs/PROJECT_REVIEW_2026-10-05.md) and [S31 build and wiring](docs/ESP32_S31.md).

---

### 🔮 Future Improvements

- Add BLE battery service
- Support for more multimedia keys
- Configurable polling rates
- Support for additional HID device types

---

### 🏗️ Built With

- **ESP-IDF** – Espressif IoT Development Framework
- **ESP-IDF USB Host + usb_host_hid** – USB enumeration and HID class driver
- **ESP BLE HID Profile** – For Bluetooth HID emulation

---

### 🙏 Acknowledgments

This project is built upon the excellent work of:

- **[Vengeance110703/USB-to-BLE-Keyboard](https://github.com/Vengeance110703/USB-to-BLE-Keyboard)** – Original USB-to-BLE keyboard implementation that served as the foundation for this project
- **[pasztorpisti/hid-report-parser](https://github.com/pasztorpisti/hid-report-parser/)** – USB HID report parser library that provided inspiration and reference for HID descriptor parsing
- **Espressif Systems** – For ESP-IDF framework and ESP32-S3 hardware
- **Espressif USB Host and HID driver** – For the USB Host implementation
- **ESP-IDF BLE HID Examples** – For the BLE HID profile implementation foundation

Special thanks to the open-source community for their contributions and feedback.

---

## 中文

### 项目简介

本项目将 **ESP32-S3 或 ESP32-S31** 打造成一个强大的 **USB HID 设备**（键盘和鼠标）与 **蓝牙低功耗 (BLE) HID** 之间的桥接器，让任何有线 USB 键盘或鼠标都能作为无线 BLE HID 设备使用。只需将 USB 设备插入开发板，然后通过蓝牙连接到您的 Mac、PC、平板或手机。

**核心亮点：**

- ✅ **双主机槽位** – 保存 Slot A / Slot B，通过 Alt + 反引号切换，Alt + N 配对新主机
- ✅ **ESP32-S31 支持** – 为 Function-CoreBoard-1 提供独立 ESP-IDF 6.1 预览配置
- ✅ **完整 macOS 兼容性** – 针对 macOS 优化，支持 Report Protocol
- ✅ **高 DPI 鼠标支持** – 16 位精度（-32767 至 32767），支持高分辨率鼠标平滑移动
- ✅ **复合 HID 设备** – 同时支持键盘、鼠标、消费控制（Consumer Control）和游戏手柄
- ✅ **高级运动处理** – Ring Buffer + 时间窗重采样，保留按键变化并重试发送
- ✅ **多按键鼠标** – 支持最多 5 个按键（左、右、中键及侧键 4、5）

---

### 🚀 核心功能

#### **USB Host 模式**

- 通过 ESP-IDF USB Host 和 `usb_host_hid` 驱动检测并读取标准 USB HID 键盘和鼠标输入
- 支持 Boot Protocol 和 Report Protocol 两种模式
- 自动协议检测和切换

#### **BLE HID 模拟**

- 通过蓝牙以标准 BLE HID 设备形式发送按键和鼠标移动
- 兼容 macOS、Windows、Linux、iOS 和 Android
- 低延迟、稳定连接

#### **macOS 优化**

- **Report Protocol 支持**：自动将 USB 设备切换到 Report Protocol 模式以确保 macOS 兼容性
- **16 位鼠标精度**：完整支持高 DPI 鼠标，使用 16 位 X/Y 坐标
- **自适应发送**：请求 7.5 ms BLE 连接间隔（约 133 Hz），鼠标发送定时器跟随主机实际协商的间隔

#### **高 DPI 鼠标兼容性**

- **16 位坐标范围**：支持每次报告 -32767 至 +32767 像素的鼠标移动
- **运动累加器**：Ring Buffer + 时间窗重采样，支持剩余位移重试
- **平滑移动**：累加高频 USB 输入，并按协商后的 BLE 连接间隔重采样

#### **双主机槽位与切换**

- 在 NVS 中保存两个主机记录（Slot A / Slot B）及活动槽位，重启后恢复
- **同一时间连接一个 BLE 主机**，切换时等待选中的主机重连
- 当前主机认证成功后才保存槽位记录并启用输入发送
- 快捷键在本地处理，BLE 未连接时也可使用；持续按住只触发一次，释放后可再次触发

#### **输入与连接恢复**

- 将 Report ID、数组键码和 NKRO 输入解析为标准八字节 BLE 键盘报告
- 保留不同 Report ID 独立更新的键盘状态，BLE 输出最多支持六个非修饰键
- 保留鼠标按键变化，发送失败后重试剩余位移
- BLE 断连或切换时清除旧相对位移
- USB 传输异常时保留设备句柄和解析布局，重试恢复输入
- 将主机的 Num Lock、Caps Lock、Scroll Lock 输出报告转发给兼容的 USB 键盘

#### **状态 LED 指示灯**

| LED 颜色    | 含义                     |
| ------------ | --------------------------- |
| 🔴 **红色**   | USB 和 BLE 均未连接 |
| 🟢 **绿色** | USB 设备已连接        |
| 🔵 **蓝色**  | BLE 设备已连接        |
| ⚪ **白色** | USB 和 BLE 均已连接  |
| 🔵 **蓝色闪烁三次** | 正在切换槽位或发现新主机 |

#### **其他功能**

- 支持修饰键（Ctrl、Alt、Shift、Cmd）和最多 6 个同时按键
- 消费控制支持（音量、媒体键等）
- 可选游戏手柄支持（可配置）
- 正确的按键释放事件，确保按键重复行为正常
- 线程安全的运动累加器，使用自旋锁保护

---

### 🎯 项目改进

相比基础的 USB-to-BLE 实现，本项目包含以下重要改进：

#### **1. 运动累加器（Ring Buffer + 时间窗）**

- **解决的问题**：USB 轮询速率（通常 1000Hz）与 BLE 传输速率（约 133Hz）不匹配，导致移动丢失和抖动
- **解决方案**：Ring Buffer 存储带微秒时间戳的 USB 事件，然后在时间窗内积分运动数据以实现稳定的 BLE 传输
- **优势**：
  - 累加位移，并在后续报告中继续发送剩余位移
  - 鼠标发送周期跟随协商后的 BLE 间隔，与 USB 输入频率解耦
  - 优雅处理 USB 输入抖动和突发
  - 两阶段提交确保即使 BLE 传输失败也能保证数据完整性

#### **2. macOS 兼容性**

- **解决的问题**：macOS 要求 USB HID 设备使用 Report Protocol 模式，而许多设备默认使用 Boot Protocol
- **解决方案**：自动检测并将 USB 设备切换到 Report Protocol 模式
- **优势**：无需手动配置即可与 macOS 无缝兼容

#### **3. 高 DPI 鼠标支持**

- **解决的问题**：标准 8 位鼠标坐标（-127 至 +127）不足以支持高 DPI 鼠标，导致像素化和精度丢失
- **解决方案**：16 位 HID Report Map，完整支持 16 位坐标
- **优势**：即使使用高 DPI 鼠标（4000+ DPI）也能实现平滑、精确的鼠标移动

#### **4. 多按键鼠标支持**

- 扩展了标准 3 键鼠标的按键支持
- 支持游戏鼠标常见的侧键（按键 4 和 5）

#### **5. 复合 HID 设备**

- 单个 BLE 设备可同时作为键盘、鼠标、消费控制和游戏手柄使用
- 具有多个 Report ID 的完整 HID Report Map

#### **6. USB HID Report Map 解析**

- **解决的问题**：USB HID 设备使用复杂的报告描述符，需要正确解析才能提取输入数据，特别是 Report Protocol 模式设备
- **解决方案**：先进的 HID 报告解析器，能够处理各种报告描述符格式，准确提取键码、鼠标移动和按键状态
- **优势**：
  - 支持 Boot Protocol 和 Report Protocol 两种模式
  - 处理具有多个 Report ID 的复杂报告描述符
  - 准确提取所有输入字段，包括按键、轴和消费控制
  - 兼容广泛的 USB HID 设备

---

### ⚙️ 工作原理

1. **USB 初始化**  
   开发板作为 USB 主机，使用 ESP-IDF USB Host 枚举并与连接的 HID 设备通信。

2. **设备检测和协议切换**  
   自动检测设备类型（键盘/鼠标）并切换到 Report Protocol 模式以确保 macOS 兼容性。

3. **HID 报告解析**  
   解析传入的 USB HID 报告，提取键码、鼠标移动和按键状态。

4. **运动累加（仅鼠标）**  
   鼠标移动以时间戳形式累加到 Ring Buffer 中，然后在时间窗内积分以实现稳定的 BLE 传输。

5. **BLE HID 模拟**  
   开发板将解析后的数据转换为 BLE HID 报告，并通过 GATT 通知发送到连接的蓝牙设备。

---

### 🧠 使用指南

#### 要求

- 支持原生 USB OTG 的 ESP32-S3，或 ESP32-S31-Function-CoreBoard-1
- 已验证 S3/S31 使用 ESP-IDF 6.1 构建；S31 为预览目标
- USB 键盘/鼠标 + USB OTG 适配器/线缆
- 串口监视器（用于调试）

#### 编译和烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

**ESP32-S31-Function-CoreBoard-1** 已增加适配，使用 ESP-IDF 6.1 预览目标及独立配置，
详见 [ESP32-S31 构建与接线说明](docs/ESP32_S31.md)。

#### 配对

1. 给**开发板**上电并连接 **USB 键盘或鼠标**。
2. 在您的 **Mac、PC 或手机**上扫描蓝牙设备。
3. 连接到名为 **"BLE HID"** 的设备。
4. 当 USB 和 BLE 都连接时，LED 将变为白色；槽位为空时，第一个认证成功的主机保存到 Slot A。

#### 切换与配对第二个主机

| 快捷键 | 操作 |
| --- | --- |
| Alt + 反引号（grave 键，`` ` ``） | 在 Slot A / Slot B 之间切换 |
| Alt + N | 进入新主机配对模式 |

左右 Alt 均可使用。快捷键由桥接器处理，不转发到主机；再次触发前需释放组合键。

1. 按上述步骤配对第一个主机。
2. 在已连接的 USB 键盘上按 **Alt + N**，当前主机断开，LED 蓝色闪烁三次。
3. 在第二个主机上搜索并配对 **BLE HID**；认证成功后填入空的 Slot B，保留 Slot A。
4. 按 **Alt + 反引号**切换。目标主机需开启蓝牙；若未自动重连，请在目标主机上连接 **BLE HID**。

两个槽位都已占用时，**Alt + N 会在新主机认证成功后替换当前活动槽位**。
若正在等待切换，Alt + N 改为向选中的目标槽位配对；再次按 Alt + 反引号可选择另一个已保存槽位。
切换到空槽位不会生效，请先通过 Alt + N 配对。

主机记录会在重启后恢复，启动日志显示 Slot A / Slot B 地址及活动槽位。
无法切换时，先检查目标主机的蓝牙连接及串口认证日志。

---

### ⚠️ 限制

- 仅支持标准 USB HID 设备（Boot Protocol 和 Report Protocol）
- 未实现 BLE 电池服务
- 某些厂商特定的按键或功能可能无法工作
- 保存两个主机槽位，同一时间连接一个 BLE 主机，重连由主机发起
- USB NKRO 输入转换为最多六个非修饰键的 BLE 输出
- 鼠标缓冲区容量有限，过载时可能丢弃新增位移
- S31 不支持高速 Hub 下全速/低速设备所需的 split transactions，详见 [S31 限制](docs/ESP32_S31.md)

---

### 验证状态

ESP32-S3 和 ESP32-S31 均已通过 ESP-IDF 6.1 编译。五组主机回归覆盖输入解析与快捷键、鼠标累加、USB 恢复、BLE 会话和应用 USB 集成。实际配对、LED 表现及 USB/BLE 联动仍需真机验证，包括 S31。

详见 [审查与回归记录](docs/PROJECT_REVIEW_2026-10-05.md) 及 [S31 构建与接线](docs/ESP32_S31.md)。

---

### 🔮 未来改进

- 添加 BLE 电池服务
- 支持更多多媒体按键
- 可配置的轮询速率
- 支持更多 HID 设备类型

---

### 🏗️ 技术栈

- **ESP-IDF** – Espressif 物联网开发框架
- **ESP-IDF USB Host + usb_host_hid** – USB 枚举与 HID 类驱动
- **ESP BLE HID Profile** – 用于蓝牙 HID 模拟

---

### 🙏 致谢

本项目基于以下优秀工作构建：

- **[Vengeance110703/USB-to-BLE-Keyboard](https://github.com/Vengeance110703/USB-to-BLE-Keyboard)** – 原始 USB-to-BLE 键盘实现，为本项目提供了基础
- **[pasztorpisti/hid-report-parser](https://github.com/pasztorpisti/hid-report-parser/)** – USB HID 报告解析器库，为 HID 描述符解析提供了参考和灵感
- **Espressif Systems** – 提供 ESP-IDF 框架和 ESP32-S3 硬件
- **Espressif USB Host 与 HID 驱动** – 提供 USB 主机实现
- **ESP-IDF BLE HID 示例** – 提供 BLE HID 配置文件实现基础

特别感谢开源社区的贡献和反馈。

---

## 📄 License

This project is open source. Please refer to the license file for details.

本项目为开源项目。详情请参阅许可证文件。
