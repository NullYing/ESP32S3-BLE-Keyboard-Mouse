# 项目 review 与修复验证（2026-10-05）

审查基线：`feature/switch` 合并提交 `3f07b1ce6e2225ec89cf73b9d1dff166dd745dde`，包括当时未提交的模块重构及新增源码。此前已将 `origin/main` 的 `722a274` 无冲突合并；本次在该工作区继续修复，保留用户原有修改。以下“当前修复”对应本次交付源码，文末原始发现的行号对应修复前版本。

**当前结论：原报告 11 项问题均已完成代码修复，五组主机回归及 ESP-IDF 完整构建通过；真实 USB/BLE 硬件验收待执行。** 不能把替身接口测试的通过视为真实主机配对、端点恢复或实时并发性能已经验证。

## 当前修复

三个 agent 分别负责输入协议、鼠标事件、BLE 槽位与认证；主 agent 整合初始化、USB 错误恢复、设备生命周期与编译验证。

| 编号 | 原优先级与问题 | 当前实现 | 验证 |
| --- | --- | --- | --- |
| 1 | P1 四字节鼠标误猜 Report ID | 所有 Report 长度使用描述符，拒绝未知 ID/短包；`[1,10,20,1]` 解码为按钮 1、X 10、Y 20、滚轮 1 | 输入及应用整合回归 PASS |
| 2 | P1 Report 键盘直接当 Boot 转发 | 描述符转换 Report ID、数组键码、NKRO、多 Report ID 状态为 BLE 八字节；仅明确 SET_PROTOCOL BOOT 成功后采用 Boot 输入 | 输入及应用整合回归 PASS |
| 3 | P2 同窗口完整点击丢失 | 同按钮状态合并位移，按钮变化切分批次，发送失败保留原批次 | 鼠标边沿与失败重试回归 PASS |
| 4 | P2 无新输入时残差停止发送 | 非零 X/Y/滚轮残差主动触发后续发送，排空前保持原按钮状态 | 60000 位移及滚轮残差回归 PASS |
| 5 | P2 断线旧位移重连后发送 | 认证连接生命周期控制队列；离线只保留当前按钮；发送锁内检查 generation，拒绝跨连接旧快照 | 断线、快速重连、并发 clear 回归 PASS |
| 6 | P2 Variable Usage Range 丢 Y | 展开 Usage Minimum/Maximum，按 Report Count 分配字段偏移 | X/Y 范围、编号报告、负值轴回归 PASS |
| 7 | P2 USB 传输错误永久停止 | 保留句柄与解析状态；应用任务执行 stop、设备 CLEAR_FEATURE(ENDPOINT_HALT)、start；失败退避重试 | USB 恢复及应用整合回归 PASS；物理故障注入待执行 |
| 8 | P1 BLE 回调未初始化无广播 | 注册前初始化 manager、sender、LED、鼠标定时器及 callbacks 的 USB/广告依赖；设置 LED 更新回调 | 初始化顺序、注册广播链、断开广播、USB LED 转发回归 PASS |
| 9 | P1 普通发现覆盖 A，空 B 不可填入 | 正常发现优先空槽；切换转发现保留明确目标，拒绝保留设备自动重连占据空槽 | 首次 A、第二设备 B、指定目标回归 PASS |
| 10 | P2 AUTH 前保存槽位 | CONNECT 只筛选候选；当前连接地址与 conn_id 匹配且 AUTH 成功后提交槽位；失败不覆盖 | 失败/陈旧/非目标 AUTH、ID 0、NVS 写入时机回归 PASS |
| 11 | P2 持续按住热键重复切换 | 解码后检查组合键按下边沿，持续报告被消费，释放后允许再次触发，USB 断开重置 latch | Boot/NKRO 热键、重复报告、释放重按回归 PASS |

## 架构决定

- 所有合法鼠标报告长度优先使用描述符。Report ID 来自描述符，不再猜首字节。新增 `hid_decode_mouse_report` 纯 C API；unknown ID/truncated 报文丢弃。只有明确选择 USB Boot 才按 Boot 格式解释。
- 键盘在开始接口前通过 `keyboard_handler_configure(handle, descriptor, descriptor_len, bool boot_protocol)` 配置实际协议。Report 模式经描述符解码到 BLE 八字节，包括 Report ID、NKRO、多 Report ID 状态聚合；不猜测。配置失败时，Boot 兼容键盘可明确 SET_PROTOCOL BOOT 后以 boot=true 重新配置。断开调用 `keyboard_handler_clear(handle)` 清状态与热键 latch。
- USB 输入协议与 BLE 输出协议独立；保留鼠标 USB Report 用于高精度和滚轮。输入派发按已注册句柄执行，支持由描述符识别的 protocol NONE 键盘，拒绝未配置接口。
- 鼠标新增 `mouse_accumulator_set_connected(bool connected)`，由真正认证成功、断开和切换事件设置，不能根据临时 send readiness 推断连接。断线与重连清相对队列和残差但保留最新 USB buttons；认证后先发送当前按钮同步。同按钮状态内合并 motion，按钮变化切分 batch，残差保留原按钮状态，非零残差始终可发送。
- 鼠标使用一次锁内 snapshot、锁外 notify、generation 与 tail 校验提交，保护 clear、producer 和 reentry。发送 helper 将 generation 传入 BLE sender，在其 send_mutex 内再次验证，防止旧快照发到新 host。full queue 丢新 motion 而不移动 tail，最新按钮独立保存用于恢复最终释放。USB detach 先记录 buttons=0 再 clear，清除旧运动与旧按下状态并重试释放。
- BLE CONNECT 只筛选并记 candidate；成功 AUTH 且地址、conn_id 匹配当前连接后才提交 slots 并开启 send。conn_id=0 合法。AUTH 失败 disconnect，保留槽位和切换目标。正常发现优先 empty slot，切换转发现继承明确目标。发送 mutex 等待在途发送结束后改变 session，readiness 使用 spinlock 避免锁竞争误报断线。广播使用统一去重流程；切换蓝灯由独立任务执行，回调与状态机不再同步等待灯效。
- 所有 BLE 回调依赖、LED callback、sender 和鼠标 timer 在注册异步回调前初始化。应用队列在 HID driver 安装前创建，USB Host 安装完成通知后再初始化恢复 client。
- USB 错误回调只记录待恢复表并发送唤醒事件，在应用任务恢复，保留设备句柄、布局和键盘状态。队列满也不丢恢复请求；失败以 250ms 退避重试，恢复过程中产生的新请求通过 generation 保留。恢复顺序为 stop（驱动 halt/flush/clear host pipe）、标准设备 CLEAR_FEATURE(ENDPOINT_HALT)、start；断开已注销的句柄不再恢复。USB 辅助 client 只处理 clear-feature control，不 claim interface；应用任务 pump 其事件。
- USB control 超时尝试 halt/flush/clear EP0。1100ms 后仍无完成 callback 则返回超时，把 transfer 与 device 引用留在静态 pending 状态，禁止恢复重入；以后 poll 收到 callback 才 free/close，避免无限等待及释放在途 buffer。遵守 usb_host_hid 1.0.4 的两阶段 close：DISCONNECTED 清应用状态并记录延后 close，由应用任务在恢复调用结束后第二次 close，避免恢复过程并发释放接口记录。未更改锁定依赖。

## 修改文件

- `main/hid_report_parser.c`、`main/hid_report_parser_c.h`、`main/usb_keyboard_report.c/.h`、`main/keyboard_handler.c/.h`：鼠标及键盘 Report 解析、描述符校验、热键边沿。
- `main/mouse_accumulator.c/.h`：按钮边沿、残差排空、连接状态、并发提交和 generation。
- `main/ble_device_manager.c/.h`、`main/ble_hid_callbacks.c/.h`、`main/ble_hid_send.c/.h`：空槽发现、认证提交、广播、发送会话校验和异步灯效。
- `main/hid_host_example.c/.h`、`main/usb_hid_types.h`、`main/usb_hid_recovery.c/.h`、`main/CMakeLists.txt`：主程序接入、初始化顺序、USB 恢复与延后关闭，编译新模块；CMake/header 的换行统一为 LF。
- `tests/input_protocol/`、`tests/mouse_accumulator/`、`tests/usb_recovery/`、`tests/usb_integration/`、`tests/test_usb_integration.py`、`tests/test_ble_sessions.py`：实际生产 C 实现及主程序函数的主机回归，SDK/NVS/通知接口使用替身。
- `docs/PROJECT_REVIEW_2026-10-05.md`：更新 11 项状态、架构决定、验证证据、限制与回滚记录。原有其他重构文件继续保留，不能将工作区全部 diff 视为本轮新增改动。

## 验证

以下入口全部 PASS，C 回归均使用 `-Werror`、AddressSanitizer、UndefinedBehaviorSanitizer；BLE 测试忽略未使用替身变量/函数警告。测试覆盖的是生产 C 代码与提取的实际主程序函数，没有只重写业务逻辑来验证替身。

```sh
sh tests/input_protocol/run.sh
sh tests/mouse_accumulator/run.sh
sh tests/usb_recovery/run.sh
python3 tests/test_ble_sessions.py
python3 tests/test_usb_integration.py
git diff --check
```

- USB 恢复 8 组场景覆盖精确接口/interrupt IN 端点选择、停止/提交/完成失败、失效句柄、已停止重试、取消失败、flush 失败及延迟 callback 的生命周期。
- 应用 USB 整合 9 组场景覆盖输入派发、Boot fallback、protocol NONE、队列满保留恢复请求、断开释放与延后 close、启动失败清理、恢复过程中断开及新错误 generation。
- BLE 回归覆盖注册广告链、断开重新广播、注入 USB 句柄的 LED 输出转发、依赖初始化顺序、双槽发现、认证失败、陈旧认证、目标筛选、conn_id=0、锁竞争和鼠标 session guard。
- ESP-IDF 6.0.2 / ESP32-S3 完整编译与最终整合后的增量编译 PASS。编译副本 `/tmp/esp-review-fixed-20261005`；最终 `main/` 源码与副本逐文件字节一致。固件 `808768` 字节，1 MiB 应用分区剩余 `239808` 字节（23%）。最终日志 `/tmp/esp-review-fixed-build.log`。
- 完整 configure 有三个原有默认配置的 unknown Kconfig symbol 警告：`BT_BLE_LOG_UHCI_OUT_ENABLED`、`MBEDTLS_TLS_DISABLED`、`MBEDTLS_BIGNUM_C`。源码编译没有错误；原广告参数未使用警告已消除。最终增量编译没有新增警告。
- 本次没有烧录，没有重复仅默认配置构建或运行 PlatformIO 构建入口。

## 剩余验收与实现边界

- 真实硬件待验收：启动广播、A/B 两台主机配对及切换、失败配对后的重试、Windows/macOS 上的 Report ID/NKRO/USB LED、鼠标短点击与断线重连、物理 USB stall/拔插过程中恢复。
- 有界鼠标队列满时可能丢中间点击及移动；最终按钮状态会补发，不能保证无限堆积期间每一个点击都保留。BLE 输出仍为五按钮、16 位轴与 8 位滚轮；超范围单个 USB 输入会夹紧，水平 pan 不转发。
- 键盘输出最多六个普通键，超过时发送 ErrorRollOver；描述符解析有 64 字段、16 Report ID 上限，超限可对 Boot 兼容键盘显式回退。USB 单次输入缓冲仍为 64 字节。键盘 USB detach 的释放为一次通知，暂时发送失败时没有像鼠标累加器那样的重试队列。
- USB control 超时可能取消同一设备 EP0 上的其他控制传输，需要物理复合设备与 LED 请求并发测试；若 SDK 永远不交付已取消 transfer 的 callback，pending 资源保持保留并暂停后续恢复，以保证在途内存安全。
- 替身同步测试覆盖确定性交错，不能证明双核调度的所有时序或持续高回报率性能。NVS 写入错误会记录日志，实际掉电持久化也需硬件验收。

## 回滚记录

修复源码、回归测试和本报告纳入 `feature/switch` 的交付提交，推送状态与提交 SHA 以 Git 记录为准。修复前备份在 `/tmp/esp-review-fix-backup`：`working-tree.patch` 保存原有 tracked 修改，`untracked.paths` 和 `untracked.tar` 保存原有未跟踪文件。需要回滚时先保存本轮修复，再按备份恢复，不能 reset --hard 丢弃用户重构。

原合并前 HEAD 为 `3e075ec4d9b465bc29de0f313617ec09de579aca`，合并提交为 `3f07b1c`。原合并备份 `/tmp/esp-switch-merge-backup` 保留。若未来撤销已共享合并，应基于第一父提交 revert，并另行保留需要的本地差异。

以下为初次审查的缺陷复现记录；这些旧行号、验证失败与建议用于追溯，当前状态以文首修复及验证为准。

## 原始发现（修复前记录）

### 8. [P1] BLE 回调模块未初始化，启动后没有广播

修复前位置：`main/hid_host_example.c:1191–1193`；`main/ble_hid_callbacks.c:25–30`、`75–80`、`177–180`。

app_main 注册了抽离后的回调，但没有调用 ble_hid_callbacks_init，也没有设置 LED 更新回调。模块内 s_adv_data、s_adv_params、s_usb_devices 始终为 NULL。注册完成时因此跳过广告数据配置，正常启动流程不会收到对应的广告数据配置完成事件并启动广播。已通过热键主动广播的连接在断开后也不会由断开回调恢复广播；LED 输出无法转发到 USB 键盘。

验证：全项目搜索只有初始化函数定义/声明，没有调用；构建同时报告主文件的 ble_hid_adv_data、ble_hid_adv_params 未使用。

建议：注册 BLE 回调前注入有效广告数据、广告参数及 USB 设备状态指针，设置 LED 更新回调，安排好 LED 句柄的初始化顺序。

### 9. [P1] 正常发现新设备覆盖当前槽位，空 B 槽始终无法填入

修复前位置：`main/ble_device_manager.c:199–218`、`316–336`。

discover_new 总是将 s_target_slot 设为 s_active_slot，on_connected 因而总走指定槽位分支，优先填入另一个空槽位的分支不可达。初次配对 A 后按 Alt+N 配对 B，实际覆盖 A，B 槽仍为空；Alt+反引号无法切换。因此正常使用路径不能建立双设备槽位。

验证：实际 discover_new/on_connected 函数复现输出 slotA=新地址、validB=0、active=0。

建议：普通发现时选择空槽位；仅从切换模式转入发现模式时继承明确的切换目标。

### 10. [P2] 发现模式在认证成功前就保存设备并结束

修复前位置：`main/ble_hid_callbacks.c:94–98`；`main/ble_device_manager.c:347–363`。

BLE CONNECT 时只要正在切换/发现，就提前调用 on_connected。发现分支立即写入 NVS、清除发现状态并设置 sec_conn=true。如果随后配对失败，旧槽位已被覆盖，发现模式也不再等待新设备；LED 状态还可能显示已连接。这个问题独立于原第 8 项“配对失败无条件启用发送”。

验证：实际函数模拟发现后的 CONNECT、尚未 AUTH，输出 discovery=0、saved=新地址。

建议：CONNECT 仅执行目标地址筛选；成功 AUTH 后再提交槽位和发现/切换完成状态，失败保持原槽位。

### 11. [P2] 持续按住切换热键会重复触发切换

修复前位置：`main/keyboard_handler.c:75–91`、`94–110`。

每份包含 Alt+反引号的报告都会执行 switch_slot，没有热键按下边沿或释放后重新使能的状态。设备重复上报按住状态、或按住组合键时操作其他键，会重复切换；switch_slot 在切换等待中会翻转目标，所以目标可被改回原设备。发现热键也重复调用，虽然发现函数会拒绝已在进行的操作。

验证：向实际键盘回调连续输入两份相同热键报告，switch_slot 被调用两次。

建议：组合键从未按下变为按下时只触发一次，完整释放后再允许触发；断开时重置热键状态。

### 1. [P1] 四字节鼠标按下按钮后会被误认为带 Report ID

修复前位置：`main/hid_host_example.c:500–518`，同时参见 `297–298`。

描述符布局只在长度至少 5 字节时使用。常见无 Report ID 的四字节报告 `[Buttons, X, Y, Wheel]` 因此进入回退路径，该路径根据首字节是否为 1–15 猜测存在 Report ID。按下左键后的 `[1,10,20,1]` 被解析为 buttons=10、X=20、Y=1、wheel=0；实际应为 buttons=1、X=10、Y=20、wheel=1。结果是错误点击、拖动跳变和滚轮失效。

验证：直接提取实际鼠标回调函数，在主机 C 程序中复现。

建议：所有合法报告长度都先使用描述符布局；是否存在 Report ID 应由描述符决定。

### 2. [P1] Report Protocol 键盘仍按 Boot 格式直接转发

修复前位置：`main/keyboard_handler.c:62–73`、`127–130`，同时参见 `main/hid_host_example.c:882–887`。

设备连接时强制选择 USB Report Protocol，但键盘回调只检查长度至少 8 字节，然后直接发送前 8 字节。带 Report ID 的九字节键盘报告 `[5,0,0,4,0,0,0,0,0]` 应表示无修饰键、A 键按下，实际却把 Report ID=5 当作 BLE 修饰键，并丢失最后一个键码。NKRO 报告同样不能按该方式转换。

验证：本轮直接提取重构后的实际键盘回调函数，确认输出 modifier=5、key1=0。未做协议转换的报告还参与热键判断，可能误触发本地快捷键。

建议：按 USB 描述符转换到 BLE 八字节键盘格式；对兼容设备也可使用 USB Boot Protocol。USB 输入协议与 BLE 输出协议可以独立选择。

### 3. [P2] 同一个发送窗口中的完整点击会丢失

修复前位置：`main/mouse_accumulator.c:325–326`、`397–400`。

窗口内所有事件只保留最后一个按钮状态，然后批量删除。按下、释放在同一 BLE 周期内发生时，只发送 buttons=0，主机完全看不到按下。较长连接间隔或 BLE 发送失败重试会扩大触发窗口，多个点击也会被合并。

验证：调用实际累加器的 add(按下)、add(释放)、try_send，输出只有一份 buttons=0 报告。

建议：按按钮状态转换切分消费批次，保持按下和释放顺序；同一按钮状态内再合并位移。

### 4. [P2] 残余位移在没有新输入时不会继续发送

修复前位置：`main/mouse_accumulator.c:297–303`、`350–354`。

计算总量时加入残差，但是否发送只看新事件产生的 dirty 标志。两个 X=30000 的事件先发送 32767 并留下 27233；后续没有运动事件时，每次 tick 都提前返回。最后一段位移停留在残差中，直到下次新输入才可能发出。滚轮饱和同样受影响。

验证：实际累加器连续调用两次 try_send，只发送一包，residual_dx=27233。

建议：非零 residual_dx、residual_dy、residual_wheel 也应触发发送。

### 5. [P2] 断线期间的旧鼠标数据会在重连后发送

修复前位置：`main/hid_host_example.c:608–609`；`main/mouse_accumulator.c:237–263`、`286–289`。

BLE 断开事件只清空当时的队列。之后 USB 回调继续入队，发送定时器因未连接而返回。重新连接后，积累的旧移动会被发送到主机，导致指针跳动；旧按钮转换也可能丢失或延迟出现。

验证：在连接查询返回 false 时 add(X=123)，恢复 true 后 try_send 输出 X=123。

建议：断线期间不积累相对位移，或在新连接可发送之前重置队列；按钮当前状态应单独同步。

### 6. [P2] 合法的 X/Y Usage Range 没有展开

修复前位置：`main/hid_report_parser.c:377–386`、`408–420`。

Variable 字段中，解析器将每个 usage_range 只当成一项，并仅检查 usage_min。使用 Usage Minimum=X、Usage Maximum=Y、Report Count=2 的合法描述符因此只得到 X，Y 大小为 0。带按钮的复现描述符输出 x_size=8、y_size=0，而应为 8、8。主回调会将 Y 解析为 0。

验证：直接编译项目的 hid_report_parser.c，用合法按钮+X/Y范围描述符复现。

建议：按范围展开 usage，每一个 usage 消费一个 Report Size 对应的字段位置；同时遵守 Report Count。

### 7. [P2] USB 传输错误后没有恢复，必须重新插拔

修复前位置：`main/hid_host_example.c:793–816`。

错误分支清空设备句柄、描述符缓存后直接返回，假设驱动自动恢复。锁定依赖 usb_host_hid 1.0.4 的 in_xfer_done 在错误时只通知 HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR；重新提交传输仅发生于成功分支。接口仍插着时不会产生新的 CONNECTED 事件，因此暂时传输错误后输入会永久停止，直到重新插拔。

验证：核对项目错误处理及实际安装的锁定版本驱动实现，未进行 USB 故障注入。

建议：通过任务队列执行接口停止/恢复，处理 endpoint stall 和错误状态，成功后恢复句柄及布局。
