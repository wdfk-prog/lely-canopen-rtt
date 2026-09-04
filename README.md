# Lely CANopen RT-Thread 移植

本目录用于把 Lely CANopen 的纯 C 协议栈移植到 RT-Thread，并保留 Lely 原有的 `ev` 事件执行器与 `io2` 异步 I/O 机制。

当前交付状态是 **B3：RT-Thread I/O runtime**。B2 的 `LELY_NO_THREADS=1` single-owner policy 保持不变，B3 已加入 owner thread、RT tick/passive timer、RT CAN RX/TX、CAN status、可选硬件 filter hook，以及基于 callback refcount drain 的 callback teardown 同步。**尚未执行目标 BSP 编译、目标板运行或 CAN 总线验证，因此不能把源码级实现等同于目标运行通过。**

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
| B4+ | 应用 command ingress、具体 CANopen node 生命周期与目标板验证 | 待实现 |

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
├── port/rtthread/                  # RT-Thread 目标适配层
│   ├── lely_rtt_config.h           # 唯一的 RT-Thread Lely feature policy
│   ├── include/lely/features.h      # features.h include overlay
│   ├── include/lely/rtthread/runtime.h # B3 public runtime API
│   └── src/                         # owner/time/timer/CAN bridge
│
├── metadata/                       # 机器可读的 vendor/build 元数据
│   ├── UPSTREAM.lock
│   ├── VENDOR_ALLOWLIST.txt
│   ├── VENDOR_MANIFEST.sha256
│   └── RTTHREAD_SOURCE_ALLOWLIST.txt
│
├── tools/                          # vendor 维护工具，不进入目标固件
│   ├── check_vendor.sh
│   └── update_lely.sh
│
└── docs/
    ├── ARCHITECTURE.md             # IO2 + EV 架构与模块边界
    ├── B2_RTTHREAD_COMPAT.md        # B2 single-owner/no-thread 策略
    ├── B3_RTTHREAD_IO.md            # B3 owner/CAN/timer/lifecycle 设计
    ├── BUILD_AND_CONFIG.md          # Kconfig/SCons/source allowlist 使用说明
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

启用后，Kconfig 选择 heap、device、CAN、event。Lely 仍以 single-owner 模式编译，不要求 C11 `<threads.h>`、pthread、mutex、condvar、device-IPC completion 或 compiler TLS backend。RT-Thread event 负责 owner wakeup/lifecycle handshake；shutdown 关闭 callback admission 后，用原子 refcount + `rt_thread_mdelay(1)` 等待已经进入的 CAN RX/status 与 one-shot timer callback 退出。

默认 `PKG_LELY_APP_AUTO_INIT=y` 时，package 参考 CANopenNode-RTT 的 default-instance 模式，在 RT-Thread application init 阶段通过 `INIT_APP_EXPORT()` 自动创建并启动一个 B3 runtime。CAN 设备名、bitrate、owner 线程资源、启动/停止超时和可选 CAN FD 参数都可在 Kconfig 中配置。若应用自行调用 `lely_rtt_runtime_create()/start()`，应关闭该选项，避免同时创建默认 runtime。

注意：B3 的 auto init 自动启动的是 single-owner CAN/time runtime，不会凭空创建具体 CANopen Node。Node-ID、DCF/OD 与 `co_*` 节点生命周期仍属于后续应用集成。

默认 `PKG_LELY_USING_ULOG=y` 时，package 选择 RT-Thread ULOG，并把 Lely 自身的 `diag()/diag_at()` 统一桥接到 tag `lely`；RT-Thread 适配层使用 tag `lely.rtt`。异步输出仍完全由工程自己的 `ULOG_USING_ASYNC_OUTPUT` 配置负责，package 不创建第二套日志线程或队列。CAN RX/status 与 deadline callback 不直接打印日志，避免在 ISR/driver/timer callback 上引入日志开销。

注意：B3 已实现到源码层，但当前 ZIP 不包含你的实际 BSP/工具链工程，本阶段没有执行目标 SCons build、目标板运行或 CANopen HIL。

## B3 RT-Thread I/O runtime

The single-owner RT-Thread CAN/time bridge is documented in [docs/B3_RTTHREAD_IO.md](docs/B3_RTTHREAD_IO.md).
