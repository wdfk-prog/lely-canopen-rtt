# Kconfig、SCons 与 RT-Thread Source Allowlist 使用说明

## 1. 为什么需要 `RTTHREAD_SOURCE_ALLOWLIST.txt`

`metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` 是 **RT-Thread 目标允许进入构建的 Lely C 源文件候选清单**。

它不是 upstream 下载清单，也不是简单的“项目里有哪些 `.c`”。它解决的是：

> frozen `upstream/` 为了可维护性保留整个 `libc/util/can/co/ev/io2` 模块，但 MCU 固件只允许经过审核的 C 源文件进入 SCons build graph。

如果直接使用：

```python
Glob('upstream/src/**/*.c')
```

会存在以下风险：

- Linux/POSIX/Win32 backend 被误编译；
- `fiber_exec`、`thrd_loop`、`vcan` 等当前架构不需要的模块进入固件；
- upstream 新增 `.c` 后无 Review 就自动进入 target；
- RAM/Flash 和平台依赖发生不易察觉的变化。

因此 B1 使用“显式 allowlist + Kconfig 二次裁剪”。

## 2. SConscript 如何使用它

`SConscript` 首先读取：

```text
metadata/RTTHREAD_SOURCE_ALLOWLIST.txt
```

然后逐项执行静态校验：

1. 必须是相对路径；
2. 不能包含 `..`；
3. 必须以 `.c` 结束；
4. 不能重复；
5. 对应文件必须真实存在；
6. 明确禁止的 source 不能进入 target boundary。

随后再根据 Kconfig 删除关闭功能对应的实现，例如：

```text
PKG_LELY_USING_CO_CSDO=n
    -> remove upstream/src/co/csdo.c

PKG_LELY_USING_CO_EMCY=n
    -> remove upstream/src/co/emcy.c

PKG_LELY_USING_CO_TPDO=n
    -> remove upstream/src/co/tpdo.c
```

所以它是“候选源码边界”，最终实际编译源码数量还取决于 Kconfig。

## 3. 当前明确禁止的 target source

SConscript 有硬性 boundary guard，当前以下文件即使误写进 allowlist 也会报错：

```text
upstream/src/ev/fiber_exec.c
upstream/src/ev/strand.c
upstream/src/ev/thrd_loop.c
upstream/src/io2/can_rt.c
upstream/src/io2/sys/clock.c
upstream/src/io2/vcan.c
upstream/src/io2/linux/*
upstream/src/io2/posix/*
upstream/src/io2/win32/*
```

如果未来架构决定启用其中某项，不能只修改 allowlist；需要同时 Review 架构、依赖、Kconfig 和资源影响。

## 4. Kconfig 的职责

`Kconfig` 是用户配置入口。

总开关：

```text
PKG_USING_LELY
```

启用后选择 B3 runtime 直接依赖的 RT-Thread 能力：

```text
RT_USING_HEAP
RT_USING_DEVICE
RT_USING_CAN
RT_USING_EVENT
```

`RT_USING_EVENT` 用于 owner work wakeup 与 READY/EXIT handshake。callback teardown 不再依赖 `RT_USING_DEVICE_IPC`/completion；shutdown 使用原子 refcount，并在等待已经获得 runtime lifetime pin 的 CAN RX/status 与 one-shot timer callback 退出时执行 `rt_thread_mdelay(1)`。`LELY_NO_THREADS=1` 保持不变。

启用默认的 `PKG_LELY_APP_AUTO_INIT` 时还会选择 `RT_USING_COMPONENTS_INIT`，并通过 `INIT_APP_EXPORT()` 在 application init 阶段启动一个默认 runtime。相关 Kconfig 包括 `PKG_LELY_CAN_DEV_NAME`、bitrate choice、RX batch、owner thread stack/priority/timeslice、start/stop timeout，以及可选 CAN FD/BRS/len-mode。若产品自行创建 runtime，应关闭 auto init。

`PKG_LELY_USING_ULOG` 默认开启并选择 `RT_USING_ULOG`。开启后，Lely 原生 `diag()/diag_at()` 由 RT-Thread bridge 接入 ULOG，tag 为 `lely`；B3 runtime/CAN/timer/auto-init 适配日志 tag 为 `lely.rtt`。是否使用异步日志、异步 buffer 大小、输出线程优先级和 backend 都继续由 RT-Thread 全局 ULOG 配置决定，Lely package 不修改 `ULOG_USING_ASYNC_OUTPUT`。

CAN RX/status indication 与 one-shot timer callback 仍只负责投递 owner 事件，不在 callback/ISR 路径调用 ULOG。逐帧 RX/TX 也不默认打印，避免高总线负载时挤占异步日志队列。

CANopen 功能通过 `PKG_LELY_USING_CO_*` 控制，例如：

```text
PKG_LELY_USING_CO_CSDO
PKG_LELY_USING_CO_EMCY
PKG_LELY_USING_CO_LSS
PKG_LELY_USING_CO_MASTER
PKG_LELY_USING_CO_RPDO
PKG_LELY_USING_CO_TPDO
PKG_LELY_USING_CO_SYNC
PKG_LELY_USING_CO_TIME
```

部分配置带依赖关系。例如：

```text
NMT master -> requires CSDO
NMT boot   -> requires NMT master
NMT cfg    -> requires NMT master
CAN FD     -> requires RT_CAN_USING_CANFD
```

## 5. `lely_rtt_config.h` 的职责

`port/rtthread/lely_rtt_config.h` 是 RT-Thread target 唯一的 Lely feature policy。

它把：

```text
PKG_LELY_*
```

映射为：

```text
LELY_NO_*
```

例如：

```text
PKG_LELY_USING_CO_TPDO=y
    -> LELY_NO_CO_TPDO=0

PKG_LELY_USING_CO_TPDO=n
    -> LELY_NO_CO_TPDO=1
```

不要在：

```text
device_sdev.c
应用 .c
其他 RT-Thread port .c
```

单独重新定义 `LELY_NO_*`，否则可能产生结构体 layout/ABI 不一致。

## 6. `features.h` overlay

SConscript 的 include path 顺序首先放置：

```text
port/rtthread/include
```

因此：

```c
#include <lely/features.h>
```

首先命中：

```text
port/rtthread/include/lely/features.h
```

该 wrapper 再依次包含：

```text
upstream/include/lely/features.h
lely_rtt_config.h
```

好处是 upstream 文件保持 byte-identical，同时 RT-Thread target 能在 upstream feature detection 完成后施加统一 policy。

## 7. 新增 Lely target source 的正确步骤

如果未来确认需要一个新的 upstream `.c`：

1. 确认它属于当前 RT-Thread 架构，而不是 host backend/测试工具；
2. 检查它的 header、source 和 feature macro 依赖；
3. 将路径加入 `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt`；
4. 如果该功能可配置，在 `Kconfig` 增加/复用对应 symbol；
5. 在 `SConscript` 中增加正确的 source selection 关系；
6. 检查 `lely_rtt_config.h` 是否需要新的 `LELY_NO_*` 映射；
7. 做配置矩阵和构建验证。

不要通过扩大 Glob 或删除 boundary guard 绕过这套流程。

## 8. Source allowlist 与 vendor allowlist 的区别

这两个文件名称相似，但层级完全不同：

| 文件 | 控制什么 | 消费方 |
| --- | --- | --- |
| `metadata/VENDOR_ALLOWLIST.txt` | 从 Lely upstream **导入哪些目录/文件到 frozen snapshot** | `tools/update_lely.sh`、`tools/check_vendor.sh` |
| `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` | frozen snapshot 中 **哪些 C 源码允许进入 RT-Thread target build** | `SConscript` |

可以理解为：

```text
Lely upstream repository
        |
        | VENDOR_ALLOWLIST
        v
   frozen upstream/
        |
        | RTTHREAD_SOURCE_ALLOWLIST
        v
RT-Thread candidate sources
        |
        | Kconfig
        v
 actual target sources
```

## 9. B2 single-owner source policy

B2 不再固定加入 `threads.c`、`time.c`、`tls.c` 等 compatibility source。`PKG_USING_LELY=y` 时，SConscript 只加载 `RTTHREAD_SOURCE_ALLOWLIST.txt` 中的 frozen upstream source，并通过统一 `lely_rtt_config.h` 令所有 translation unit 使用：

```text
LELY_NO_THREADS=1
LELY_NO_ATOMICS=1
LELY_NO_TIMEOUT=1
```

因此 `upstream/src/libc/stdatomic.c`、pthread/Win32 threads backend、system clock backend、`thrd_loop`、`strand`、`fiber_exec` 等都被 target boundary 排除。

这里的 no-thread 是 Lely library access contract，不是 RT-Thread 系统配置。B3 通过 RT-Thread event 把 CAN RX、timer/status wakeup 串行化到唯一 Lely owner thread，并用原子 refcount + `rt_thread_mdelay(1)` 封闭 CAN/timer 外部 callback 生命周期。应用 command ingress 仍属于后续 product/runtime integration 范围。

B2 的约束见 [B2 single-owner compatibility](B2_RTTHREAD_COMPAT.md)。

## 10. 当前验证边界

B1/B2/B3 已具备静态 source/configuration boundary、single-owner policy 和 RT CAN/time runtime 源码，但当前目录不包含实际目标 BSP 和工具链配置，因此这里不能声称目标 SCons build、链接或目标板运行已通过。
