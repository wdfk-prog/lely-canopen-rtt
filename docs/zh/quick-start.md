# 快速接入

> 摘要：把本包接入 RT-Thread BSP，启动 single-owner runtime，并把产品级 Master 策略保持在通用适配层之外。

[English](../en/quick-start.md)

最短接入路径取决于应用只需要 transport/runtime，还是需要仓库内 Master + Node1 示例。两条路径最终都使用同一个 `lely_rtt_runtime_config` 和同一套 owner-thread 实现。

## 1. 接入 package

上层 RT-Thread 工程需要包含本仓库的 `Kconfig` 和 `SConscript`。具体如何从父工程 `source`/include 取决于最终 BSP/package 目录，本仓库不假设一个固定的上层路径。

首先启用：

```text
PKG_USING_LELY=y
```

它会选择基础 runtime 所需的 RT-Thread heap、device、CAN 和 event。

## 2. 配置 CAN 设备和 owner thread

`PKG_LELY_APP_AUTO_INIT=y` 时，至少需要关注：

```text
PKG_LELY_CAN_DEV_NAME="can1"
PKG_LELY_AUTO_INIT_BITRATE=1000000
PKG_LELY_AUTO_INIT_RX_BATCH=8
PKG_LELY_AUTO_INIT_THREAD_STACK_SIZE=4096
PKG_LELY_AUTO_INIT_THREAD_PRIORITY=5
PKG_LELY_AUTO_INIT_THREAD_TIMESLICE=10
PKG_LELY_AUTO_INIT_START_TIMEOUT_MS=3000
PKG_LELY_AUTO_INIT_STOP_TIMEOUT_MS=3000
```

这些值属于工程策略，不是对所有 BSP 都成立的推荐值。尤其 thread priority 必须满足目标 BSP 的 `RT_THREAD_PRIORITY_MAX`。

只有 BSP 需要显式 `RT_CAN_CMD_START` 时才保留 `PKG_LELY_AUTO_INIT_START_CONTROLLER=y`。只有 BSP 确实实现 `RT_CAN_CMD_SET_STATUS_IND` 且状态字段语义可用时才启用 status indication。

## 3. 选择 runtime-only 或仓库内 Master 示例

只需要 runtime 时，保持 `PKG_LELY_EXAMPLE_MASTER_NODE1=n`，由产品层后续绑定自己的 CANopen 对象。

需要仓库内 Master 示例时启用：

```text
PKG_LELY_EXAMPLE_MASTER_NODE1=y
```

auto-init 会在 `start()` 前绑定 `examples/master_node1/master_sdev.c`。真正的 `co_dev_t` 和 `co_nmt_t` 只在 CAN/IO2 runtime 已准备完成后由 owner thread 创建；如果静态设备不是 CANopen Master，startup 会失败。

`examples/node1/node1.dcf` 始终是 remote node 的 Host 描述，不会在 MCU 运行时被加载或解析。

## 4. 只启用实际需要的控制面

Master control plane 是逐项叠加的：

| 需求 | 选项 |
|---|---|
| 应用线程发送 NMT command | `PKG_LELY_USING_MASTER_COMMAND` |
| remote SDO upload/download | `PKG_LELY_USING_MASTER_SDO` |
| 手动 remote configuration | `PKG_LELY_USING_MASTER_NMT_CFG` |
| 本地 manufacturer OD 访问 | `PKG_LELY_USING_LOCAL_OD` |
| 动态 local OD upload + 写入通知 | `PKG_LELY_USING_MASTER_OD_HOOKS` |
| 触发静态 Master TPDO | `PKG_LELY_USING_MASTER_PDO_TX` |
| SYNC 与同步 PDO 控制 | `PKG_LELY_USING_MASTER_SYNC_PDO` |
| EMCY bridge | `PKG_LELY_USING_MASTER_EMCY` |
| TIME bridge | `PKG_LELY_USING_MASTER_TIME` |
| 默认示例 MSH 命令 | `PKG_LELY_USING_MSH` |

Kconfig 会表达依赖并自动 select 部分下层能力。大范围启用前先看[配置指南](configuration.md)。

## 5. 关闭 auto-init 后的显式生命周期

应用显式创建 runtime 时使用同一套公开 API：

```c
#include <lely/rtthread/runtime.h>

static struct lely_rtt_runtime_config cfg = {
    .can_name = "can1",
    .can_bitrate = 1000000,
    .rx_batch = 8,
    .thread_stack_size = 4096,
    .thread_priority = 5,
    .thread_timeslice = 10,
    .start_timeout_ms = 3000,
    .stop_timeout_ms = 3000,
    .start_controller = RT_TRUE,
    .use_status_indication = RT_FALSE,
    .can_fd = RT_FALSE,
    .can_brs = RT_FALSE,
    .can_fd_len_mode = LELY_RTT_CANFD_LEN_BYTES,
    .filter_setup = RT_NULL,
    .filter_setup_arg = RT_NULL,
    .status_mapper = RT_NULL,
    .status_mapper_arg = RT_NULL,
};

lely_rtt_runtime_t *runtime = lely_rtt_runtime_create(&cfg);
if (!runtime)
    return -RT_ENOMEM;

/* 如需静态 Master，应在 start() 前调用：
 * lely_rtt_runtime_configure_master(runtime, &master_sdev);
 */

if (lely_rtt_runtime_start(runtime) != RT_EOK) {
    while (lely_rtt_runtime_stop(runtime) == -RT_ETIMEOUT) {
    }
    lely_rtt_runtime_destroy(runtime);
    return -RT_ERROR;
}
```

同一个 runtime 的 lifecycle 调用必须由应用外部串行化。`start()`/`stop()` timeout 不代表可以释放 runtime；必须继续 `stop()`，直到确认 owner 已退出后才能 `destroy()`。

## 6. Target 构建与验证

package 级源码检查不能替代真实 target build。使用实际 BSP/toolchain 时至少确认：

- 配置的 RT-Thread CAN device 存在；
- bitrate 设置与可选 controller start 成功；
- 当前 driver path 支持本 port 所需的 non-blocking CAN TX；
- RX、error/status 和 shutdown 在真实 CAN controller 上行为符合预期；
- CAN FD 开启时确认 BSP 的 `rt_can_msg.len` 语义；
- Master 示例按产品需要验证 Boot-up、Heartbeat、NMT boot、SDO、PDO/SYNC、EMCY 和 TIME。

继续阅读[RT-Thread 集成](rt-thread-integration.md)，再接入应用侧控制逻辑。
