# Lely RT-Thread IO2 + EV 架构说明

## 1. 设计目标

本移植继续使用 Lely 的 `ev` 和 `io2` runtime，但不让 Lely 自己承担 RT-Thread 多线程同步。
RT-Thread 可以正常运行多个业务线程和 ISR；Lely EV/IO2/CANopen 对象由一个专用 owner thread 串行访问。

目标数据流：

```text
RT-Thread CAN Driver / ISR / RX thread
                |
                | bounded ingress queue
                v
        Lely owner thread
                |
                v
       io_user_can_chan
                |
                v
          io_can_net
                |
                v
           can_net_t
                |
                v
          liblely-co
```

时间路径：

```text
RT-Thread time source / timer wakeup
                |
                | event/queue wakeup
                v
        Lely owner thread
                |
                v
          io_user_timer
                |
                v
          io_can_net
                |
                v
      CANopen timers/state
```

应用与异步任务路径：

```text
application thread / callback
                |
                | command queue
                v
        Lely owner thread
                |
                v
             ev_exec
                |
                v
             ev_loop
                |
                v
        IO2 + CANopen tasks
```

这个架构的关键约束是：跨线程只发生在 RT-Thread ingress 层，不发生在 Lely 对象内部。

## 2. B1 选择的 EV 子集

目标固件候选源只包含：

```text
exec.c
future.c
loop.c
poll.c
std_exec.c
task.c
```

B1 明确不让以下机制进入 RT-Thread target：

```text
fiber_exec.c
strand.c
thrd_loop.c
```

当前 CANopen runtime 由一个 `ev_loop` executor domain 串行化协议任务，不需要 fiber stack、独立
thread loop 或 `io_can_rt` 所依赖的 strand。

这些文件继续物理保存在 frozen `upstream/` 中，供 upstream 更新和依赖审计；“存在于源码包”不等于
“进入目标固件”。

## 3. B2 single-owner policy

B2 固定：

```c
#define LELY_NO_THREADS 1
#define LELY_NO_ATOMICS 1
#define LELY_NO_TIMEOUT 1
```

这里的 `LELY_NO_THREADS` 只描述 Lely 的访问模型：

```text
one Lely owner thread
        |
        +--> ev_loop
        +--> io_user_can
        +--> io_user_timer
        +--> io_can_net
        +--> co_*
```

它不禁止 RT-Thread 的其它线程，也不禁止 CAN 驱动线程或 ISR。其它执行上下文必须先通过 RT-Thread
queue/event/mailbox 把 work 交给 owner thread。

采用 upstream 已有 no-thread 分支后：

- `ev_loop_thrd`/`ev_exec_list` 不再需要 `_Thread_local`；
- EV、future、stop、IO2 内部 mutex/condvar 被编译裁掉；
- C11 atomics backend 不再需要；
- target 不再依赖 GCC/Clang `__emutls_*` ABI；
- target 不再提供 package C11 `<threads.h>` compatibility。

详细约束见 [B2 single-owner compatibility](B2_RTTHREAD_COMPAT.md)。

## 4. B3 runtime 与 ingress 边界

B3 已把 single-owner invariant 落成真实 runtime。CAN driver callback 和 RT timer callback 都不会直接进入 Lely。

CAN 接收：

```text
CAN ISR / driver RX indication
        |
        | LELY_RTT_EVENT_RX_READY
        v
Lely owner thread
        |
        | rt_device_read() bounded batch
        v
RT-Thread CAN software FIFO
        |
        v
io_user_can_chan_on_msg(..., timeout=0)
```

这里直接复用 RT-Thread CAN device 自带的软件 RX FIFO，不再增加一层专用 CAN RX thread/queue。`rx_batch` 限制每轮读取数量，持续高负载下通过重新发送 RX_READY 保证 timer/status 仍有调度机会。

Timer：

```text
io_user_timer setnext
        |
        v
RT one-shot timer
        | callback only sends TIMER_DUE
        v
Lely owner thread
        |
        v
io_clock_settime() / tqueue / CANopen timer work
```

Lifecycle：

```text
shared rt_event:
  RX_READY / TIMER_DUE / CAN_STATUS / STOP -> owner work
  READY / EXIT                             -> caller handshake

external callback lifetime:
  callback acquire -> callback_refs++
  cleanup begin    -> prohibit new CAN/timer callback pins
                   -> detach/stop producers
                   -> callback refcount drain_wait() until callback_refs == 0
```

owner work wait 使用 `RT_WAITING_FOREVER`，因为所有工作都有显式 event 唤醒；`start()`/`stop()` 对 READY/EXIT 使用配置的有限超时，避免调用线程无限阻塞。CAN/timer callback teardown 使用原子 refcount，并用 `rt_thread_mdelay(1)` 协作式等待，不使用 completion 或 `yield` 忙轮询。

Hardware CAN acceptance filter 是可选优化，不是 correctness layer。默认不安装 restrictive filter，要求 BSP 正常 open 状态能够接收 CANopen 网络所需全部帧；应用可通过 `filter_setup` 在启动时配置硬件 filter。动态 COB-ID 场景必须保持硬件接受范围足够宽，或由产品层自行维护 filter 同步；Lely 的 `can_net` 软件 receiver tree 始终负责最终分发。

应用线程直接调用 `co_*`/Lely API 仍不允许。面向业务的 command ingress API 属于后续 product integration。

## 5. B1/B2 选择的 IO2 子集

目标路径保留：

```text
user/can
user/timer
can_net
以及它们依赖的 can/clock/ctx/dev/timer/tqueue/sys helper
```

明确不使用：

```text
io2/can_rt.c
io2/vcan.c
io2/linux/*
io2/posix/*
io2/win32/*
```

`io_can_net` 独占其 CAN channel 和 timer，因此当前 CANopen 主通道不同时挂 `io_can_rt`。

## 6. CAN 队列策略

MCU 目标不采用 Lely host-oriented 的超大默认队列。RT-Thread target policy 当前固定：

```c
#define LELY_IO_USER_CAN_RXLEN 32
#define LELY_IO_CAN_NET_TXLEN  32
```

这两个值在 `port/rtthread/lely_rtt_config.h` 中覆盖，不直接 patch upstream `.c`。

32 是当前工程基线，不代表已经通过最终总线压力测试。B3+ 需要根据 ingress queue high-water mark、
Lely RX/TX overflow counter、RAM map 和实际 CAN 负载决定是否调整。

## 7. Feature/ABI 一致性

Lely 的多个 public/internal structure 会受 `LELY_NO_*` 控制，因此不同 translation unit 不能看到不同
feature set。

统一配置链为：

```text
Kconfig
  |
  v
rtconfig.h
  |
  v
port/rtthread/include/lely/features.h
  |                         \
  v                          -> lely_rtt_config.h
upstream/include/lely/features.h
```

`port/rtthread/include/lely/features.h` 是 include overlay：先包含 byte-identical upstream
`features.h`，再应用 RT-Thread target policy。

因此未来的 Lely upstream `.c`、B3 port `.c`、`dcf2c` 生成的 `device_sdev.c` 和应用代码都必须通过
相同 CPPPATH 解析到这个 wrapper。

## 8. 当前阶段边界

B1 已实现：

- Kconfig 功能选择；
- SConscript candidate source boundary；
- `LELY_NO_*` 统一映射；
- EV/IO2 target source 裁剪策略；
- RX/TX queue = 32。

B2 已实现：

- Lely single-owner 访问模型；
- `LELY_NO_THREADS=1`；
- `LELY_NO_ATOMICS=1`；
- `LELY_NO_TIMEOUT=1`；
- 删除 package C11 threads/condvar/TLS/emutls compatibility；
- 排除 `stdatomic.c`、thread backend、system clock backend。

B3 已实现到源码层：

- owner thread 与 shared event dispatcher；
- `rt_tick_get()` monotonic extension + `io_user_timer` one-shot bridge；
- RT CAN RX/TX + `io_user_can`；
- CAN status mapping 与可选 BSP mapper；
- CAN FD len/DLC BSP contract；
- CAN/timer callback lifetime pin + mdelay refcount shutdown drain；
- bounded READY/EXIT waits；
- optional hardware acceptance-filter startup hook。

仍未完成/验证：

- 实际 BSP SCons compile/link；
- 目标板 CAN/CAN FD、bus-off、RX pressure、timer 精度与 HIL；
- 产品层 application command ingress 与具体 CANopen node 生命周期。

## 9. 源码包大小与固件大小不是一回事

`upstream/` 继续保留完整模块树，主要为了 upstream 更新和依赖审计。没有被 `SConscript` 选中的 `.c`
不会因为存在于 ZIP 中就自动占用目标板 Flash/RAM。

真正影响固件资源的是：

```text
Kconfig 生效配置
    -> SConscript 选中的源文件
    -> 编译/链接实际保留的 section
    -> 最终 map / stack / heap 实测
```

当前策略优先保证 vendor 可追踪性，再通过 `RTTHREAD_SOURCE_ALLOWLIST + Kconfig` 控制 firmware build
graph。B3+ 再根据 map、queue high-water mark 和 stack watermark 做资源优化。
