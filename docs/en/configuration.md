# Configuration and Build Selection

> 摘要：Maps Kconfig to Lely feature policy, source selection, runtime resources, and Master-service dependencies.

[中文](../zh/configuration.md)

Configuration is split across three layers: Kconfig expresses product intent, `lely_rtt_config.h` converts that intent into one ABI-consistent Lely feature policy, and `SConscript` selects only the target-eligible sources required by the final option set.

## 1. Base package

```text
PKG_USING_LELY
```

The package selects:

```text
RT_USING_HEAP
RT_USING_DEVICE
RT_USING_CAN
RT_USING_EVENT
```

The target policy keeps C++, stdio/daemon runtime support, runtime DCF parsing, gateways, object files, host backends, Lely thread backends, and other non-target mechanisms out unless explicitly required by the selected architecture.

## 2. Runtime options

`PKG_LELY_APP_AUTO_INIT` controls whether `port/rtthread/src/auto_init.c` creates one default runtime during RT-Thread application initialization.

Important automatic-runtime options are:

| Option | Meaning |
|---|---|
| `PKG_LELY_CAN_DEV_NAME` | RT-Thread CAN device name. |
| `PKG_LELY_AUTO_INIT_BITRATE` | Arbitration bitrate in bit/s. |
| `PKG_LELY_AUTO_INIT_RX_BATCH` | Maximum RX frames drained per owner pass. |
| `PKG_LELY_AUTO_INIT_THREAD_STACK_SIZE` | Owner stack size. |
| `PKG_LELY_AUTO_INIT_THREAD_PRIORITY` | Owner priority. |
| `PKG_LELY_AUTO_INIT_THREAD_TIMESLICE` | Owner time slice. |
| `PKG_LELY_AUTO_INIT_START_TIMEOUT_MS` | READY wait bound. |
| `PKG_LELY_AUTO_INIT_STOP_TIMEOUT_MS` | EXIT wait bound per stop attempt. |
| `PKG_LELY_AUTO_INIT_START_CONTROLLER` | Issue `RT_CAN_CMD_START`. |
| `PKG_LELY_AUTO_INIT_STATUS_INDICATION` | Register CAN status indication callback. |
| `PKG_LELY_AUTO_INIT_CANFD` | Enable CAN FD mapping in the default runtime. |
| `PKG_LELY_AUTO_INIT_BRS` | Permit CAN FD bit-rate switching. |
| `PKG_LELY_AUTO_INIT_CANFD_LEN_BYTES` / `PKG_LELY_AUTO_INIT_CANFD_LEN_DLC` | Select byte-count vs raw-DLC BSP convention. |

Manual runtime creation fills the same `struct lely_rtt_runtime_config`; there is no second configuration model.

## 3. Logging and CAN FD

`PKG_LELY_USING_ULOG` routes Lely diagnostics through RT-Thread ULOG. Lely messages use tag `lely`; RT-Thread adaptation messages use `lely.rtt`. The package does not create a second logging thread or queue.

`PKG_LELY_USING_CANFD` only enables Lely CAN FD structures/branches. The actual BSP CAN driver must also support compatible CAN FD operation. Do not infer target capability from this symbol alone.

## 4. Master example and control plane

`PKG_LELY_EXAMPLE_MASTER_NODE1` builds the checked-in local Master OD and selects the Lely Master/CSDO/NMT-boot prerequisites required by that example.

The control-plane group is dependency-driven:

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

`PKG_LELY_USING_MASTER_COMMAND` selects an RT-Thread message queue. The queue is only cross-thread transport; Lely services are still executed by the owner.

`PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH` defaults to 8 and bounds copied commands waiting for owner dispatch.

`PKG_LELY_USING_MASTER_EMCY` uses a fixed-size per-runtime history. `PKG_LELY_MASTER_EMCY_HISTORY_DEPTH` defaults to 8 and is limited to 1..32.

## 5. MSH

`PKG_LELY_USING_MSH` requires:

- automatic runtime creation;
- the checked-in Master + Node1 example;
- CANopen Master support;
- `RT_USING_FINSH && FINSH_USING_MSH`.

It selects `PKG_LELY_USING_MASTER_COMMAND` and exposes only the subcommands whose underlying features are enabled.

## 6. CANopen feature selection

The final Kconfig section maps individual CANopen services to `LELY_NO_*` macros, including Client-SDO, EMCY, LSS, Master, NMT boot/configuration, Node Guarding, RPDO, TPDO, SYNC, TIME, MPDO, SSDO block transfer, and optional OD metadata such as names, limits, defaults, and uploads.

Do not define conflicting `LELY_NO_*` macros in application or generated translation units. `port/rtthread/include/lely/features.h` overlays the single project policy so every target translation unit sees the same ABI-affecting definitions.

## 7. Source allowlist

`metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` is the only target-eligible upstream source list consumed by `SConscript`. It includes the selected pure-C utility, CAN, CANopen, EV, and IO2 user-backend sources while excluding host/platform backends such as SocketCAN/Win32, `vcan`, `io_can_rt`, fiber/strand/thread-loop implementations, and other non-target code.

The allowlist is broader than one concrete Kconfig profile. `SConscript` first loads and validates it, then removes feature-specific `.c` files for disabled CANopen services.

This is distinct from `metadata/VENDOR_ALLOWLIST.txt`, which defines which upstream files are imported into the frozen vendor snapshot at all.

## 8. Build evidence

A successful source-selection check only proves that the expected source graph was chosen. It does not prove that the target compiler, linker script, BSP CAN driver, or final firmware behavior is correct. Treat a real BSP build and target run as separate evidence.
