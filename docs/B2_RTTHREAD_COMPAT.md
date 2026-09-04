# B2：Lely 单 owner 线程运行策略

## 1. 结论

B2 不再实现 C11 `<threads.h>`、RT-Thread condvar 或 compiler emulated TLS。
目标固定为：

```text
RT-Thread 可以有多个线程/ISR
            |
            | RT-Thread queue/event/mailbox
            v
      Lely owner thread
            |
            v
       EV + IO2 + CO
```

`LELY_NO_THREADS=1` 表示 **Lely 自身按单线程访问模型编译**，并不表示整个 RT-Thread 系统没有线程。
所有 Lely EV、IO2 和 CANopen 对象只能由一个 owner thread 访问；其它线程和 ISR 只能向 owner thread
投递事件或数据，不能直接调用 Lely API。

这一策略直接消除了原 B2 中的 GCC/Clang `__emutls_*` ABI 依赖，也不再需要 pthread、C11 mutex、condvar
或 thread-local storage backend。

## 2. 为什么删除 `__emutls_*`

原实现为 `_Thread_local` 提供：

```text
compiler-generated TLS control object
        |
        v
__emutls_get_address()
        |
        +-- rt_thread_self()
        +-- 查找/分配 per-thread heap object
        v
thread-local object address
```

`__emutls_get_address()` 和 `__emutls_register_common()` 不是 Lely API，也不是 RT-Thread API；它们属于
编译器 runtime ABI。编译器只有在特定 TLS lowering 策略下才会生成这些调用，因此 port 对具体
compiler/runtime 行为产生了不必要的绑定。

对 MCU vendor port 来说，这类依赖还有两个问题：

1. 工具链升级或更换后，TLS lowering 方式可能变化；
2. port 必须正确复现 compiler TLS control object 的布局、对齐、初始化和生命周期规则。

因此本 target 不继续维护 compiler ABI shim。

## 3. 为什么可以使用 `LELY_NO_THREADS`

当前 allowlist 中 Lely 已经为该模式提供完整条件编译路径，不需要修改 frozen `upstream/`：

- `ev/loop.c`：`ev_loop_thrd` 从 `_Thread_local` 变为普通静态状态；
- `ev/std_exec.c`：`ev_exec_list` 从 `_Thread_local` 变为普通静态状态；
- `ev/future.c`、`ev/loop.c`、`util/stop.c`：mutex/condvar/atomic 路径被裁掉；
- `io2/user/can.c`、`io2/user/timer.c`、`io2/can_net.c`、`io2/ctx.c`、`io2/tqueue.c`：内部 mutex/condvar
  路径被裁掉。

因此不需要通过宏伪造 `_Thread_local`，也不需要修改 upstream 文件。

## 4. Target feature policy

`port/rtthread/lely_rtt_config.h` 固定：

```c
LELY_NO_THREADS = 1
LELY_NO_ATOMICS = 1
LELY_NO_TIMEOUT = 1
```

三者的含义不同：

- `LELY_NO_THREADS=1`：Lely 对象不提供跨线程同步保护；
- `LELY_NO_ATOMICS=1`：在单 owner 前提下不引入 C11 atomic runtime；
- `LELY_NO_TIMEOUT=1`：不使用 `ev_loop`/`io_user_can` 的阻塞 deadline wait。

`LELY_NO_TIMEOUT` 不关闭 CANopen 协议定时器。当前 selected source 中该宏只影响 EV wait 和
`io_user_can` 的同步等待路径；CANopen 的 heartbeat、SDO/NMT 等时间推进仍由 B3 的
`io_user_timer` 路径承担。

## 5. 单 owner 不变量

下面是 B3 及应用层必须保持的运行约束：

```text
Lely object ownership = exactly one RT-Thread owner thread
```

禁止：

```text
CAN RX ISR/thread --------直接调用-------> io_user_can_chan_on_msg()
application thread -------直接调用-------> co_* / ev_exec_*
RT timer callback --------直接调用-------> Lely timer/EV object
second worker thread -----调用------------> ev_loop_wait/run family
```

必须改为：

```text
CAN RX ISR/thread
        |
        v
RT-Thread ingress queue
        |
        v
Lely owner thread
        |
        +--> io_user_can_chan_on_msg(..., timeout = 0)
        +--> ev/IO2/CANopen processing
```

应用命令和 timer wakeup 同样先进入 owner thread 的 ingress 通道。

特别注意：`io_user_can_chan_on_msg()` 在 `LELY_NO_THREADS=1` 下没有 condition-variable consumer 可以在
调用者阻塞时并发腾出 RX ring，因此 B3 必须使用 `timeout=0` 的非阻塞注入策略；ring 满时由 port 明确
记录/上报 overflow，而不能在 Lely 内部等待。

## 6. B2 删除的兼容层

以下旧 B2 文件已删除：

```text
port/rtthread/compat/include/threads.h
port/rtthread/include/lely/rtthread/time.h
port/rtthread/include/lely/rtthread/tls.h
port/rtthread/src/threads.c
port/rtthread/src/time.c
port/rtthread/src/tls.c
```

同时 `upstream/src/libc/stdatomic.c` 不再进入 RT-Thread source allowlist。

因此 target 不再提供：

```text
mtx_*
cnd_*
thrd_*
__emutls_get_address
__emutls_register_common
```

## 7. 文件头和注释规则

本 port 自有 C/H 文件遵循以下规则：

- 文件头说明文件用途、作者和 SPDX license；
- public API/宏使用 Doxygen 描述调用契约；
- 结构体、全局/静态变量仅在存在 ownership、并发、状态或生命周期含义时解释；
- 不为显而易见的局部变量逐行添加无意义注释；
- frozen `upstream/` 保持 byte-identical，不修改其原有作者和注释。

B2 调整后不再保留自有 `.c` compatibility backend；当前需要维护文件头的 target C/H 主要是
`lely_rtt_config.h` 和 `include/lely/features.h`。后续 B3 新增 CAN/timer/runtime `.c/.h` 时继续执行
同一规则。

## 8. B3 必须实现的线程边界

B3 不只是接 CAN driver，还需要把 single-owner contract 落到 runtime：

```text
CAN RX callback/ISR -> bounded ingress queue --+
application request -> command queue ----------+--> Lely owner thread
RT timer wakeup ----> event/wakeup ------------+
                                               |
                                               +--> io_user_can
                                               +--> io_user_timer
                                               +--> io_can_net
                                               +--> CANopen node
```

B3 需要明确 queue 深度、overflow 语义、ISR 可调用 API、owner thread 优先级、启动/停止顺序，以及
shutdown 时如何阻止新的 ingress 后再排空已有 work。
