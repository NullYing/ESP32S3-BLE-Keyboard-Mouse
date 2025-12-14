# 🔌 USB-to-BLE HID Bridge (ESP32-S3)

**English** | [中文](#中文)

---

## English

### Overview

This project transforms an **ESP32-S3** into a powerful bridge between **USB HID devices** (keyboards and mice) and **Bluetooth Low Energy (BLE) HID**, enabling any wired USB keyboard or mouse to function as a wireless BLE HID device. Simply plug your USB device into the ESP32-S3 and connect to your Mac, PC, tablet, or phone via Bluetooth.

**Key Highlights:**
- ✅ **Full macOS Compatibility** – Optimized for macOS with Report Protocol support
- ✅ **High DPI Mouse Support** – 16-bit precision (-32768 to 32767) for smooth high-resolution mouse movement
- ✅ **Composite HID Device** – Simultaneously supports Keyboard, Mouse, Consumer Control, and Gamepad
- ✅ **Advanced Motion Processing** – Ring Buffer + Time Window resampling for stable, lossless mouse movement
- ✅ **Multi-Button Mouse** – Supports up to 5 buttons (left, right, middle, side buttons 4 & 5)

---

### 🚀 Key Features

#### **USB Host Mode**
- Detects and reads input from standard USB HID keyboards and mice via TinyUSB
- Supports both Boot Protocol and Report Protocol modes
- Automatic protocol detection and switching

#### **BLE HID Emulation**
- Sends keystrokes and mouse movements over Bluetooth as a standard BLE HID device
- Compatible with macOS, Windows, Linux, iOS, and Android
- Low latency and stable connection

#### **macOS Optimization**
- **Report Protocol Support**: Automatically switches USB devices to Report Protocol mode for macOS compatibility
- **16-bit Mouse Precision**: Full support for high DPI mice with 16-bit X/Y coordinates
- **Stable Polling**: Fixed 7.5ms (133Hz) BLE transmission rate for consistent performance

#### **High DPI Mouse Compatibility**
- **16-bit Coordinate Range**: Supports mouse movement from -32768 to +32767 pixels per report
- **Motion Accumulator**: Advanced Ring Buffer + Time Window algorithm ensures no movement loss
- **Smooth Movement**: Handles high-frequency USB input (up to 1000Hz) and resamples to stable BLE rate

#### **Status LED Indicators**

| LED Color    | Meaning                     |
| ------------ | --------------------------- |
| 🔴 **Red**   | USB & BLE both disconnected |
| 🟢 **Green** | USB device connected        |
| 🔵 **Blue**  | BLE device connected        |
| ⚪ **White** | Both USB and BLE connected  |

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
  - No movement loss even during high-speed mouse movements
  - Stable 133Hz transmission rate regardless of USB input frequency
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

---

### ⚙️ How It Works

1. **USB Initialization**  
   The ESP32-S3 acts as a USB host using TinyUSB to enumerate and communicate with connected HID devices.

2. **Device Detection & Protocol Switching**  
   Automatically detects device type (keyboard/mouse) and switches to Report Protocol mode for macOS compatibility.

3. **HID Report Parsing**  
   Incoming USB HID reports are parsed to extract keycodes, mouse movements, and button states.

4. **Motion Accumulation (Mouse Only)**  
   Mouse movements are accumulated in a Ring Buffer with timestamps, then integrated over time windows for stable BLE transmission.

5. **BLE HID Emulation**  
   The ESP32-S3 translates parsed data into BLE HID reports and sends them via GATT notifications to the connected Bluetooth device.

---

### 🧠 Usage Guide

#### Requirements

- ESP32-S3 board with native USB OTG support
- ESP-IDF v5.x or later
- USB keyboard/mouse + USB OTG adapter/cable
- Serial monitor (for debugging)

#### Build & Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

#### Pairing

1. Power the **ESP32-S3** and connect a **USB keyboard or mouse**.
2. On your **Mac, PC, or phone**, scan for Bluetooth devices.
3. Connect to the advertised **"BLE HID"** device.
4. The LED will turn white when both USB and BLE are connected.

---

### ⚠️ Limitations

- Only supports standard USB HID devices (Boot Protocol and Report Protocol)
- BLE battery service is not implemented
- Some vendor-specific keys or features may not work
- Maximum 6 simultaneous keypresses (HID standard limitation)

---

### 🔮 Future Improvements

- Add BLE battery service
- Support for more multimedia keys
- Configurable polling rates
- Support for additional HID device types

---

### 🏗️ Built With

- **ESP-IDF** – Espressif IoT Development Framework
- **TinyUSB** – USB Host stack
- **ESP BLE HID Profile** – For Bluetooth HID emulation

---

### 🙏 Acknowledgments

This project is built upon the excellent work of:

- **[Vengeance110703/USB-to-BLE-Keyboard](https://github.com/Vengeance110703/USB-to-BLE-Keyboard)** – Original USB-to-BLE keyboard implementation that served as the foundation for this project
- **Espressif Systems** – For ESP-IDF framework and ESP32-S3 hardware
- **TinyUSB** – For the robust USB Host stack
- **ESP-IDF BLE HID Examples** – For the BLE HID profile implementation foundation

Special thanks to the open-source community for their contributions and feedback.

---

## 中文

### 项目简介

本项目将 **ESP32-S3** 打造成一个强大的 **USB HID 设备**（键盘和鼠标）与 **蓝牙低功耗 (BLE) HID** 之间的桥接器，让任何有线 USB 键盘或鼠标都能作为无线 BLE HID 设备使用。只需将 USB 设备插入 ESP32-S3，然后通过蓝牙连接到您的 Mac、PC、平板或手机。

**核心亮点：**
- ✅ **完整 macOS 兼容性** – 针对 macOS 优化，支持 Report Protocol
- ✅ **高 DPI 鼠标支持** – 16 位精度（-32768 至 32767），支持高分辨率鼠标平滑移动
- ✅ **复合 HID 设备** – 同时支持键盘、鼠标、消费控制（Consumer Control）和游戏手柄
- ✅ **高级运动处理** – Ring Buffer + 时间窗重采样，确保稳定、无丢失的鼠标移动
- ✅ **多按键鼠标** – 支持最多 5 个按键（左、右、中键及侧键 4、5）

---

### 🚀 核心功能

#### **USB Host 模式**
- 通过 TinyUSB 检测并读取标准 USB HID 键盘和鼠标输入
- 支持 Boot Protocol 和 Report Protocol 两种模式
- 自动协议检测和切换

#### **BLE HID 模拟**
- 通过蓝牙以标准 BLE HID 设备形式发送按键和鼠标移动
- 兼容 macOS、Windows、Linux、iOS 和 Android
- 低延迟、稳定连接

#### **macOS 优化**
- **Report Protocol 支持**：自动将 USB 设备切换到 Report Protocol 模式以确保 macOS 兼容性
- **16 位鼠标精度**：完整支持高 DPI 鼠标，使用 16 位 X/Y 坐标
- **稳定轮询**：固定 7.5ms（133Hz）BLE 传输速率，确保一致性能

#### **高 DPI 鼠标兼容性**
- **16 位坐标范围**：支持每次报告 -32768 至 +32767 像素的鼠标移动
- **运动累加器**：先进的 Ring Buffer + 时间窗算法确保不丢失任何移动
- **平滑移动**：处理高频 USB 输入（最高 1000Hz）并重采样为稳定的 BLE 速率

#### **状态 LED 指示灯**

| LED 颜色    | 含义                     |
| ------------ | --------------------------- |
| 🔴 **红色**   | USB 和 BLE 均未连接 |
| 🟢 **绿色** | USB 设备已连接        |
| 🔵 **蓝色**  | BLE 设备已连接        |
| ⚪ **白色** | USB 和 BLE 均已连接  |

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
  - 即使高速移动鼠标也不会丢失移动数据
  - 无论 USB 输入频率如何，都保持稳定的 133Hz 传输速率
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

---

### ⚙️ 工作原理

1. **USB 初始化**  
   ESP32-S3 作为 USB 主机，使用 TinyUSB 枚举并与连接的 HID 设备通信。

2. **设备检测和协议切换**  
   自动检测设备类型（键盘/鼠标）并切换到 Report Protocol 模式以确保 macOS 兼容性。

3. **HID 报告解析**  
   解析传入的 USB HID 报告，提取键码、鼠标移动和按键状态。

4. **运动累加（仅鼠标）**  
   鼠标移动以时间戳形式累加到 Ring Buffer 中，然后在时间窗内积分以实现稳定的 BLE 传输。

5. **BLE HID 模拟**  
   ESP32-S3 将解析后的数据转换为 BLE HID 报告，并通过 GATT 通知发送到连接的蓝牙设备。

---

### 🧠 使用指南

#### 要求

- 支持原生 USB OTG 的 ESP32-S3 开发板
- ESP-IDF v5.x 或更高版本
- USB 键盘/鼠标 + USB OTG 适配器/线缆
- 串口监视器（用于调试）

#### 编译和烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

#### 配对

1. 给 **ESP32-S3** 上电并连接 **USB 键盘或鼠标**。
2. 在您的 **Mac、PC 或手机**上扫描蓝牙设备。
3. 连接到名为 **"BLE HID"** 的设备。
4. 当 USB 和 BLE 都连接时，LED 将变为白色。

---

### ⚠️ 限制

- 仅支持标准 USB HID 设备（Boot Protocol 和 Report Protocol）
- 未实现 BLE 电池服务
- 某些厂商特定的按键或功能可能无法工作
- 最多 6 个同时按键（HID 标准限制）

---

### 🔮 未来改进

- 添加 BLE 电池服务
- 支持更多多媒体按键
- 可配置的轮询速率
- 支持更多 HID 设备类型

---

### 🏗️ 技术栈

- **ESP-IDF** – Espressif 物联网开发框架
- **TinyUSB** – USB Host 协议栈
- **ESP BLE HID Profile** – 用于蓝牙 HID 模拟

---

### 🙏 致谢

本项目基于以下优秀工作构建：

- **[Vengeance110703/USB-to-BLE-Keyboard](https://github.com/Vengeance110703/USB-to-BLE-Keyboard)** – 原始 USB-to-BLE 键盘实现，为本项目提供了基础
- **Espressif Systems** – 提供 ESP-IDF 框架和 ESP32-S3 硬件
- **TinyUSB** – 提供强大的 USB Host 协议栈
- **ESP-IDF BLE HID 示例** – 提供 BLE HID 配置文件实现基础

特别感谢开源社区的贡献和反馈。

---

## 📄 License

This project is open source. Please refer to the license file for details.

本项目为开源项目。详情请参阅许可证文件。
