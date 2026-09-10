# Quick Start

> 摘要：Integrate the package into an RT-Thread BSP, start the owner runtime, and keep product policy outside the port.

[中文](../zh/quick-start.md)

The shortest usable path depends on whether the application wants only the transport/runtime layer or the checked-in Master + Node1 example. Both paths use the same `lely_rtt_runtime_config` and owner-thread implementation.

## 1. Integrate the package

The parent RT-Thread project must include this repository's `Kconfig` and `SConscript`. The exact parent-directory/package mechanism is intentionally outside this repository because it depends on the final BSP/package layout.

Enable the package entry:

```text
PKG_USING_LELY=y
```

This selects the RT-Thread heap, device, CAN, and event components required by the base runtime.

## 2. Configure the CAN device and owner thread

With `PKG_LELY_APP_AUTO_INIT=y`, configure at least:

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

The concrete values are project policy, not universal defaults. In particular, the thread priority must be valid for the selected BSP and `RT_THREAD_PRIORITY_MAX`.

Keep `PKG_LELY_AUTO_INIT_START_CONTROLLER=y` only when the BSP expects `RT_CAN_CMD_START`. Enable status indication only when the BSP implements `RT_CAN_CMD_SET_STATUS_IND` with usable semantics.

## 3. Choose runtime-only or Master example

For a runtime-only integration, leave `PKG_LELY_EXAMPLE_MASTER_NODE1=n` and create/configure CANopen objects from the product layer later.

For the checked-in Master example, enable:

```text
PKG_LELY_EXAMPLE_MASTER_NODE1=y
```

The auto-init layer binds `examples/master_node1/master_sdev.c` before start. The owner thread creates `co_dev_t` and `co_nmt_t` only after the CAN/IO2 runtime is ready and rejects startup if the static device is not a CANopen Master.

`examples/node1/node1.dcf` remains a Host-side description of a remote node. It is not loaded or parsed on the MCU.

## 4. Enable only the control-plane features you need

The Master control plane is intentionally additive. Typical combinations are:

| Need | Option |
|---|---|
| post NMT commands from application threads | `PKG_LELY_USING_MASTER_COMMAND` |
| remote SDO upload/download | `PKG_LELY_USING_MASTER_SDO` |
| manual remote reconfiguration | `PKG_LELY_USING_MASTER_NMT_CFG` |
| local manufacturer OD access | `PKG_LELY_USING_LOCAL_OD` |
| trigger static Master TPDO | `PKG_LELY_USING_MASTER_PDO_TX` |
| SYNC and synchronous PDO controls | `PKG_LELY_USING_MASTER_SYNC_PDO` |
| EMCY bridge | `PKG_LELY_USING_MASTER_EMCY` |
| TIME bridge | `PKG_LELY_USING_MASTER_TIME` |
| default example MSH command | `PKG_LELY_USING_MSH` |

Kconfig expresses the required dependencies and automatically selects some subordinate options. See [Configuration](configuration.md) before enabling a large feature set.

## 5. Manual lifecycle, when auto-init is disabled

Application-owned initialization uses the same public API:

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

/* Optional, before start():
 * lely_rtt_runtime_configure_master(runtime, &master_sdev);
 */

if (lely_rtt_runtime_start(runtime) != RT_EOK) {
    while (lely_rtt_runtime_stop(runtime) == -RT_ETIMEOUT) {
    }
    lely_rtt_runtime_destroy(runtime);
    return -RT_ERROR;
}
```

Lifecycle calls for one runtime must be externally serialized. A start/stop timeout does not authorize freeing runtime storage; call `stop()` again until owner exit has been observed before `destroy()`.

## 6. Build and target validation

The package-level source checks do not replace a real target build. With the actual BSP/toolchain:

- confirm the selected RT-Thread CAN device exists;
- confirm bitrate setup and optional controller start succeed;
- confirm non-blocking CAN TX is supported by the driver path used by this port;
- confirm RX, error/status handling, and shutdown behavior on the actual controller;
- if CAN FD is enabled, confirm the BSP's `rt_can_msg.len` convention;
- for the Master example, validate Boot-up, Heartbeat, NMT boot, SDO, PDO/SYNC, EMCY, and TIME as required by the product.

Continue with [RT-Thread integration](rt-thread-integration.md) before adding application-side control.
