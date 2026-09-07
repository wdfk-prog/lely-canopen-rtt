# Lely CANopen RT-Thread 移植

本目录用于把 Lely CANopen 的纯 C 协议栈移植到 RT-Thread，并保留 Lely 原有的 `ev` 事件执行器与 `io2` 异步 I/O 机制。

当前交付状态为 **B4：本地 CANopen Master + 远端 Node1**。B3 的 `LELY_NO_THREADS=1` single-owner runtime 保持不变；B4 由 Host 将远端 `node1.dcf` 纳入 Master OD 生成链，目标端只实例化 `master_sdev`，并在本地 reset 后强制校验 `co_nmt_is_master()`。**当前仍未执行目标 BSP 编译、目标板运行或 CAN HIL，因此 Boot-up、Heartbeat、Client-SDO、NMT boot 和远端状态恢复仍属于待目标验证项。**

## 1. 目标架构

```text
RT-Thread Application
        |
        | ev_task / ev_exec
        v
     Lely EV
  ev_loop + future
        |
        v
 Lely CANopen co
        |
        v
     can_net_t
        |
        v
    io_can_net
      /     \
     v       v
io_user_can  io_user_timer
     |             |
     v             v
RT CAN Driver   RT time source
```

RT-Thread 负责系统线程、跨线程 ingress、时间源和 CAN 设备；Lely EV/IO2/CANopen 由一个专用 owner thread 串行访问。其它线程和 ISR 不直接进入 Lely。

详细设计见 [架构说明](docs/ARCHITECTURE.md)。

## 2. 当前阶段

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| B0 | 冻结 Lely upstream 源码边界、来源和校验信息 | 已完成 |
| B1 | Kconfig、SConscript、统一 feature 配置、RT-Thread 源码 allowlist | 已完成 |
| B2 | `LELY_NO_THREADS` single-owner policy、去 C11 threads/TLS/atomic backend | 已实现，待目标构建/运行验证 |
| B3 | owner runtime、`io_user_timer`、RT CAN bridge、callback refcount drain、可选 hardware filter hook | 已实现，待目标构建/运行验证 |
| B4.0 | owner thread 内 `co_dev_t` / `co_nmt_t` 生命周期与逆序 teardown | 已实现，待目标验证 |
| B4.1 | remote `node1.dcf` → `master.yml` → compact Master DCF → static `master_sdev.c` | 已实现，Windows Host 需复跑 |
| B4.2 | Node1 Boot-up + Heartbeat consumer + NMT boot + Client-SDO identity | 源码路径已接通，待 CAN HIL |
| B4.3 | Master/CSDO/NMT boot feature/source graph | 已修正，待 BSP build |
| B4.4 | local/remote NMT state + remote boot result read-only snapshots | 已实现，待目标验证 |
| M0 | default Master MSH：`co status/node/boot` 只读 snapshot | 已实现，待目标验证 |
| M1 | Master command ingress + `co nmt ...` | 已实现，待目标验证 |
| M2 | request-id CSDO transaction + `co sdo read/write` | 已实现，待目标验证 |

## 3. 目录说明

```text
lely-rtt-vendor/
├── README.md                       # 项目入口文档
├── Kconfig                         # RT-Thread menuconfig 配置入口
├── SConscript                      # RT-Thread SCons 源文件选择入口
├── LICENSE                         # Lely upstream Apache-2.0 许可证
├── NOTICE                          # Lely upstream NOTICE
│
├── upstream/                       # 冻结的 Lely upstream 源码，不直接手工修改
├── examples/node1/                 # 远端 Node1 DCF 与旧从站生成物 provenance（不进入目标构建）
├── examples/master_node1/          # Master YAML/DCF/static OD 与主站示例说明
├── port/rtthread/                  # RT-Thread 目标适配层
│   ├── lely_rtt_config.h           # 唯一的 RT-Thread Lely feature policy
│   ├── include/lely/features.h      # features.h include overlay
│   ├── include/lely/rtthread/runtime.h # B3 lifecycle + B4 Master/snapshot API
│   └── src/                         # owner/time/timer/CAN bridge + Master control
│       ├── master_command.c          # M1 跨线程 Master command ingress
│       ├── master_sdo.c              # M2 owner-thread CSDO transaction
│       └── msh.c                     # M0/M1/M2 default Master MSH 前端
│
├── metadata/                       # 机器可读的 vendor/build 元数据
│   ├── UPSTREAM.lock
│   ├── VENDOR_ALLOWLIST.txt
│   ├── VENDOR_MANIFEST.sha256
│   └── RTTHREAD_SOURCE_ALLOWLIST.txt
│
├── tools/                          # Host/vendor 维护工具，不进入目标固件
│   ├── check_vendor.sh
│   ├── dcf2c.exe                     # Windows x86-64 Lely DCF-to-C 工具
│   ├── setup_dcfgen_windows.ps1     # 可选：按已验证版本准备 Windows dcfgen 环境
│   ├── requirements-dcfgen-windows.txt # dcfgen Windows 固定依赖版本
│   ├── gen_sdev.ps1                 # Windows 通用 YAML/DCF -> static sdev C/H 生成器
│   ├── gen_cfg_dcf.py               # Lely dcfgen concise DCF -> application C/H 生成器
│   ├── compact_master_dcf.py        # 裁剪 dcfgen 的大 CompactSubObj Master DCF
│   └── update_lely.sh
│
└── docs/
    ├── ARCHITECTURE.md             # IO2 + EV 架构与模块边界
    ├── B2_RTTHREAD_COMPAT.md        # B2 single-owner/no-thread 策略
    ├── B3_RTTHREAD_IO.md            # B3 owner/CAN/timer/lifecycle 设计
    ├── BUILD_AND_CONFIG.md          # Kconfig/SCons/source allowlist 使用说明
    ├── DCF_DCF2C_CANOPENEDITOR.md   # DCF 创建、dcf2c 安装/生成与 Node1 provenance
    └── UPSTREAM_MAINTENANCE.md      # upstream 元数据和更新流程
```

## 4. 最重要的几个文件

`Kconfig` 决定用户在 RT-Thread `menuconfig` 中开启哪些 CANopen 功能；`port/rtthread/lely_rtt_config.h` 把这些 `PKG_LELY_*` 配置统一映射为 Lely 的 `LELY_NO_*` 宏。

`SConscript` 不会扫描整个 `upstream/`。它只读取 `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` 中经过审核的候选 C 源码，然后根据 Kconfig 再删除被关闭功能对应的 `.c`。这样可以避免 Linux/POSIX/Win32 backend、`fiber_exec`、`thrd_loop`、`can_rt`、`vcan` 等文件意外进入 MCU 固件。

这套构建关系详见 [构建与配置说明](docs/BUILD_AND_CONFIG.md)，B2 single-owner contract 见 [B2 适配说明](docs/B2_RTTHREAD_COMPAT.md)。

`metadata/` 下的四个文件不是固件代码，而是为了保证 upstream 来源、导入范围和 RT-Thread 编译范围可追踪。具体作用见 [upstream 维护说明](docs/UPSTREAM_MAINTENANCE.md)。

## 5. Vendor 校验

从本目录执行：

```sh
./tools/check_vendor.sh
```

它检查 frozen upstream 的模块范围、符号链接、Lely public include 闭包以及 SHA-256 manifest。

该命令只验证 **源码快照边界**，不等于编译、链接或目标板 CANopen 验证。

## 6. Upstream 更新

从明确 tag/commit 更新：

```sh
./tools/update_lely.sh --ref <tag-or-commit>
```

或指定 GitHub mirror：

```sh
./tools/update_lely.sh \
    --ref <tag-or-commit> \
    --remote https://github.com/lely-industries/lely-core.git
```

更新工具只按照 `metadata/VENDOR_ALLOWLIST.txt` 导入允许的 upstream 模块，并更新 `metadata/UPSTREAM.lock` 与 `metadata/VENDOR_MANIFEST.sha256`。

详细规则见 [upstream 维护说明](docs/UPSTREAM_MAINTENANCE.md)。

## 7. RT-Thread 工程接入

本目录内部已经提供 `Kconfig` 和 `SConscript`，但上层 RT-Thread 工程如何 `source`/包含该包，取决于最终项目目录和 package 集成方式，本阶段没有假设一个不存在的上层工程结构。

上层完成集成后，配置入口为：

```text
PKG_USING_LELY
```

启用后，Kconfig 选择 heap、device、CAN 和 event。Lely 仍以 single-owner 模式编译，不要求 C11 `<threads.h>`、pthread、mutex、condvar、device-IPC completion 或 compiler TLS backend。RT-Thread event 负责 owner wakeup/lifecycle handshake；shutdown 关闭 callback admission 后，用原子 refcount + `rt_thread_mdelay(1)` 等待已经进入的 CAN RX/status 与 one-shot timer callback 退出。

默认 `PKG_LELY_APP_AUTO_INIT=y` 时，package 参考 CANopenNode-RTT 的 default-instance 模式，在 RT-Thread application init 阶段通过 `INIT_APP_EXPORT()` 自动创建并启动 runtime。CAN 设备名、bitrate、owner 线程资源、启动/停止超时和可选 CAN FD 参数都可在 Kconfig 中配置。若应用自行调用 `lely_rtt_runtime_create()/start()`，应关闭该选项，避免同时创建默认 runtime。

启用 `PKG_LELY_EXAMPLE_MASTER_NODE1=y` 后，auto-init 会在 `start()` 前通过 `lely_rtt_runtime_configure_master()` 绑定 `examples/master_node1/master_sdev.c`。真正的 `co_dev_create_from_sdev()`、`co_nmt_create()` 和本地 NMT reset 只在 owner thread 内发生；reset 后若 `co_nmt_is_master()==0`，启动直接失败。目标端继续保持 `LELY_NO_CO_DCF=1`，不会把远端 `node1.dcf` 当成本地设备或在 MCU 上解析 DCF。

默认 `PKG_LELY_USING_ULOG=y` 时，package 选择 RT-Thread ULOG，并把 Lely 自身的 `diag()/diag_at()` 统一桥接到 tag `lely`；RT-Thread 适配层使用 tag `lely.rtt`。异步输出仍完全由工程自己的 `ULOG_USING_ASYNC_OUTPUT` 配置负责，package 不创建第二套日志线程或队列。CAN RX/status 与 deadline callback 不直接打印日志，避免在 ISR/driver/timer callback 上引入日志开销。

Host 生成链在 **Windows PowerShell** 下执行。项目已经自带 Windows x86-64 的 `tools\dcf2c.exe`；`dcfgen` 由 Python 包 `dcf-tools` 提供。推荐按下面这套已经在 Windows 实机跑通的命令准备环境：

```powershell
.\tools\dcf2c.exe --help
py -3 --version
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install --force-reinstall `
    "setuptools==81.0.0" `
    "empy==3.3.4" `
    "dcf-tools==2.4.2"
.\.venv\Scripts\dcfgen.exe --help
```

通用 static OD 入口是 `tools\gen_sdev.ps1`。它支持两种模式：`-Yml` 先调用 `dcfgen` 生成 Master DCF，再调用 `dcf2c`；`-Dcf` 直接把任意 DCF 转成 static sdev C。`-Name` 指定 C 符号和默认 `.c/.h` 文件名，`-OutDir` 指定输出目录。

Lely 官方 `dcfgen` 还会为需要配置 SDO 的 slave 生成 `<slave>.bin` concise DCF。B8 的 manual-only application DCF 复用这个官方编码器，但不把生成的 `master.dcf` 发布到目标 Master OD：`tools\gen_cfg_dcf.py` 在临时目录调用 `dcfgen`，只取指定 slave 的 `.bin`，校验 concise-DCF framing 后生成可直接编译的 `.c/.h`。因此 `dcfgen` 负责 CANopen datatype/SDO 编码，本项目脚本只负责 staging、校验和 C 数组封装。

RT-Thread MCU 上的 Master YAML 必须增加 `-CompactMaster -NoStrings`。Lely `dcfgen` 的标准 Master 模板会为多组 Manager 对象生成 `CompactSubObj=127/254`；目标端 `co_dev_create_from_sdev()` 会把这些 compact entry 展开成动态 `co_sub_t`，在小 MCU 上会造成不必要的 heap 压力。`-CompactMaster` 在 Host 端先调用 `tools\compact_master_dcf.py` 收缩这些范围，`-NoStrings` 再让 `dcf2c` 省略可选对象名称字符串。示例：

```powershell
# YAML -> DCF + C + H
.\tools\gen_sdev.ps1 `
    -Yml .\examples\master_node1\master.yml `
    -Name master_sdev `
    -OutDir .\generated\master_node1 `
    -DcfFileName master.dcf `
    -RemotePdo `
    -CompactMaster `
    -NoStrings

# 已有 DCF -> C + H
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\generated\node1
```

Master+Node1 不再使用专用包装器，统一直接调用 `tools\gen_sdev.ps1`。刷新仓库内 MCU 示例时必须显式传入 `-CompactMaster -NoStrings -NoHeader -MetaFile master_sdev.meta`，并继续使用 8-entry 的 `0x1003` error history 上限和 256 个估算 sub-object 的安全门槛。这样 `master.dcf`、`master_sdev.c` 和 `master_sdev.meta` 都由同一个通用入口维护，而 `master_sdev.h` 仍保留为项目维护的声明头。不要把未经裁剪的 `dcfgen master.dcf` 直接交给 `dcf2c`。`dcfgen --help` 可能打印 `pkg_resources is deprecated` 警告，只要后续命令继续正常执行就不是失败。完整命令和参数见 [DCF、CANopenEditor 与 Lely dcf2c 使用指南](docs/DCF_DCF2C_CANOPENEDITOR.md)。

默认 auto-init Master 可通过 `lely_rtt_runtime_get_default()` 获取只读 ownership 的 runtime handle。应用线程只读取 owner 发布的 `lely_rtt_runtime_get_local_nmt_state()`、`lely_rtt_runtime_get_remote_nmt_state()` 和 `lely_rtt_runtime_get_remote_boot_status()` snapshot，不直接进入 Lely。

### Master MSH 辅助控制

`PKG_LELY_USING_MSH=y` 为 default auto-init runtime 导出单一根命令 `co`。M0 的只读命令完全复用 snapshot API：

```text
co status
co node <node-id>
co boot <node-id>
```

MSH 不直接访问 `master_nmt/master_dev`。`PKG_LELY_USING_MASTER_COMMAND=y` 才引入每 runtime 的 RT-Thread message queue，并通过 owner event 执行 M1 NMT 控制：

```text
co nmt start <node-id|all>
co nmt stop <node-id|all>
co nmt preop <node-id|all>
co nmt reset-node <node-id|all>
co nmt reset-comm <node-id|all>
```

`queued:` 只表示命令已经进入 owner queue，不表示远端节点已经完成状态切换；真实状态继续使用 `co node <id>` 查询。shutdown 开始后 command admission 关闭，队列中尚未执行的控制请求不会越过 teardown 边界。

可选 `PKG_LELY_USING_MASTER_SDO=y` 增加 M2 的异步 request-id CSDO transaction。每个 remote Node-ID 最多只有一个 application SDO 活跃，请求支持协议 timeout、SDO abort code、completion、显式 application cancel、Client-SDO block upload/download 和 shutdown cancellation。普通与 block download 都在 post 返回前复制输入数据；block upload 的完成数据仍由 request 持有到 destroy。当前实现为 application 独立创建基于 Node-ID 的 CiA 301 预定义默认 CSDO，不借用 NMT boot CSDO；自定义 CSDO COB-ID 留给后续 Controller 配置模型。MSH 当前仍只暴露 CiA 301 标量普通传输诊断类型，block/cancel 作为 application API 提供：

```text
co sdo read  <node> <index> <subindex> <bool|u8|u16|u32|i8|i16|i32> <timeout-ms>
co sdo write <node> <index> <subindex> <bool|u8|u16|u32|i8|i16|i32> <value> <timeout-ms>
```

M2 不把 `co_csdo_t *` 暴露给 MSH/application。受控 `stop/reset-node/reset-comm` 会先取消该节点的 application SDO；成功发送 reset 后继续挂起新的 application SDO，直到远端 boot process 完成或状态证据恢复到可进行 SDO 的状态。对于远端自发 Boot-up，owner 会先终止并销毁 application default CSDO，再调用 `co_nmt_on_st()` 让 Lely NMT boot 独占默认 SDO 通道；snapshot 仍在默认 NMT 处理之后发布，且不修改 frozen upstream。B9 进一步增加 owner-safe SYNC producer/consumer application bridge、post-PDO SYNC snapshot/callback，以及本地 RPDO/TPDO transmission type `0..240/254/255` 控制；共享 Master+Node1 示例继续保持原有 event-driven 默认值，启用 B9 后可通过 MSH/API 显式切换为同步 PDO。

注意：B4 主站角色与 M0/M1/M2 控制面已经接到源码/配置层，但当前 ZIP 不包含实际 BSP/工具链工程，本阶段没有执行目标 SCons build、目标板运行或 CANopen HIL。

## B3 RT-Thread I/O runtime

The single-owner RT-Thread CAN/time bridge is documented in [docs/B3_RTTHREAD_IO.md](docs/B3_RTTHREAD_IO.md).
