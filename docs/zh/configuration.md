# 配置与构建选择

> 摘要：说明 RT-Thread Kconfig 如何映射到 Lely feature policy、target source selection、runtime 资源和可选 Master 控制面依赖。

[English](../en/configuration.md)

配置分成三层：Kconfig 表达产品意图，`lely_rtt_config.h` 把配置收敛成一份 ABI 一致的 Lely feature policy，`SConscript` 再从 target-eligible source 中选出最终需要编译的源文件。

## 1. 基础 package

```text
PKG_USING_LELY
```

启用后选择：

```text
RT_USING_HEAP
RT_USING_DEVICE
RT_USING_CAN
RT_USING_EVENT
```

target policy 默认排除 C++、stdio/daemon runtime、运行时 DCF 解析、gateway、object-file runtime、Host backend、Lely thread backend 等不属于当前 MCU 架构的机制。

## 2. Runtime 选项

`PKG_LELY_APP_AUTO_INIT` 决定 `port/rtthread/src/auto_init.c` 是否在 RT-Thread application init 阶段创建一个默认 runtime。

主要 auto-init 选项：

| 选项 | 含义 |
|---|---|
| `PKG_LELY_CAN_DEV_NAME` | RT-Thread CAN device name。 |
| `PKG_LELY_AUTO_INIT_BITRATE` | arbitration bitrate，单位 bit/s。 |
| `PKG_LELY_AUTO_INIT_RX_BATCH` | owner 每轮最多 drain 的 RX frame 数。 |
| `PKG_LELY_AUTO_INIT_THREAD_STACK_SIZE` | owner stack size。 |
| `PKG_LELY_AUTO_INIT_THREAD_PRIORITY` | owner priority。 |
| `PKG_LELY_AUTO_INIT_THREAD_TIMESLICE` | owner time slice。 |
| `PKG_LELY_AUTO_INIT_START_TIMEOUT_MS` | READY wait 上限。 |
| `PKG_LELY_AUTO_INIT_STOP_TIMEOUT_MS` | 每次 stop 等待 EXIT 的上限。 |
| `PKG_LELY_AUTO_INIT_START_CONTROLLER` | 是否发送 `RT_CAN_CMD_START`。 |
| `PKG_LELY_AUTO_INIT_STATUS_INDICATION` | 是否注册 CAN status indication callback。 |
| `PKG_LELY_AUTO_INIT_CANFD` | 默认 runtime 是否启用 CAN FD 映射。 |
| `PKG_LELY_AUTO_INIT_BRS` | 是否允许 CAN FD bit-rate switching。 |
| `PKG_LELY_AUTO_INIT_CANFD_LEN_BYTES` / `PKG_LELY_AUTO_INIT_CANFD_LEN_DLC` | 选择 BSP byte-count/raw-DLC 语义。 |

手动创建 runtime 仍然填写同一个 `struct lely_rtt_runtime_config`，不存在第二套配置模型。

## 3. 日志与 CAN FD

`PKG_LELY_USING_ULOG` 把 Lely diagnostic 接入 RT-Thread ULOG。Lely 自身消息使用 `lely` tag，RT-Thread 适配层使用 `lely.rtt`。package 不创建第二套日志线程或队列。

`PKG_LELY_USING_CANFD` 只打开 Lely CAN FD 结构与代码路径，不代表目标 BSP driver 自动具备 CAN FD 能力。是否可用必须由真实 driver 与 target 验证决定。

## 4. Master 示例与控制面

`PKG_LELY_EXAMPLE_MASTER_NODE1` 编译仓库内 local Master OD，并选择该示例需要的 Lely Master/CSDO/NMT-boot 前置能力。

控制面依赖关系：

```text
PKG_LELY_USING_MASTER_COMMAND
    +-- PKG_LELY_USING_MASTER_SDO
    +-- PKG_LELY_USING_MASTER_NMT_CFG
    +-- PKG_LELY_USING_LOCAL_OD
          +-- PKG_LELY_USING_MASTER_PDO_TX
          +-- PKG_LELY_USING_MASTER_SYNC_PDO
    +-- PKG_LELY_USING_MASTER_EMCY
    +-- PKG_LELY_USING_MASTER_TIME
```

`PKG_LELY_USING_MASTER_COMMAND` 会选择 RT-Thread message queue。这个 queue 只是跨线程传输，真正的 Lely service 调用仍由 owner 完成。

`PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH` 默认 8，用来限制等待 owner dispatch 的复制 command 数。

`PKG_LELY_USING_MASTER_EMCY` 使用固定大小的 per-runtime history；`PKG_LELY_MASTER_EMCY_HISTORY_DEPTH` 默认 8，范围 1..32。

## 5. MSH

`PKG_LELY_USING_MSH` 依赖：

- auto-init runtime；
- 仓库内 Master + Node1 示例；
- CANopen Master；
- `RT_USING_FINSH && FINSH_USING_MSH`。

它会 select `PKG_LELY_USING_MASTER_COMMAND`，并且只暴露底层 feature 已开启的子命令。

## 6. CANopen feature selection

Kconfig 最后一组把独立 CANopen service 映射到 `LELY_NO_*`：Client-SDO、EMCY、LSS、Master、NMT boot/configuration、Node Guarding、RPDO、TPDO、SYNC、TIME、MPDO、SSDO block，以及对象名、limits、defaults、upload 等可选 OD 元数据。

不要在应用或生成的 translation unit 中重新定义冲突的 `LELY_NO_*`。`port/rtthread/include/lely/features.h` 负责 overlay 唯一 policy，使 target 上所有 translation unit 看到一致的 ABI-affecting feature 宏。

## 7. Source allowlist

`metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` 是 `SConscript` 消费的 target-eligible upstream 源码列表。它包含当前架构允许的 pure-C util、CAN、CANopen、EV、IO2 user backend，同时排除 SocketCAN/Win32、`vcan`、`io_can_rt`、fiber/strand/thread-loop 等 Host 或非目标机制。

allowlist 的范围可以比某个具体 Kconfig profile 更大。`SConscript` 先加载并验证 allowlist，再根据被关闭的 CANopen feature 删除对应 `.c`。

它与 `metadata/VENDOR_ALLOWLIST.txt` 不是一个层次：后者决定哪些 upstream 文件允许进入 frozen vendor snapshot，前者决定哪些文件有资格进入 RT-Thread target build。

## 8. 构建证据边界

source-selection 检查成功只说明选中的源码图符合策略，不代表 target compiler、linker script、BSP CAN driver 或最终 firmware 行为已经正确。真实 BSP build 与 target run 必须单独作为验证证据。
