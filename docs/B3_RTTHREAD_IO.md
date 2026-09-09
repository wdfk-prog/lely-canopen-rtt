# B3 RT-Thread I/O runtime

B3 adds the RT-Thread hardware/time bridge while preserving the B2 single-owner policy. RT-Thread itself remains multithreaded, but Lely EV/IO2/CANopen objects are executed by one dedicated owner thread.

## Runtime model

`lely_rtt_runtime_start()` creates the owner thread. The RT CAN RX callback, CAN status callback and RT one-shot timer callback never call Lely directly. They only acquire a short runtime lifetime pin, wake the shared event and release the pin. The owner thread drains RX, advances the passive `io_user_timer` clock, injects CAN status changes and drains `ev_loop`.

The public entry point is `port/rtthread/include/lely/rtthread/runtime.h`. Manual callers provide the CAN device name, bitrate, RX batch, owner-thread resources and lifecycle timeouts explicitly. The optional auto-init layer fills the same configuration structure from Kconfig defaults.

## Optional automatic startup

`PKG_LELY_APP_AUTO_INIT` follows the default-instance pattern used by `canopennode-rtt`: when enabled, Kconfig selects RT-Thread component initialization and `port/rtthread/src/auto_init.c` registers one `INIT_APP_EXPORT()` hook. The hook builds a `lely_rtt_runtime_config` from Kconfig, creates one default runtime and starts it during RT-Thread application initialization. Disable the option when application code owns runtime creation explicitly.

The auto-start hook does not create a concrete CANopen node. B3 has no product DCF/OD or Node-ID contract yet, so inventing a Node-ID here would blur the boundary between the transport runtime and the later CANopen application lifecycle.

## ULOG diagnostics

With `PKG_LELY_USING_ULOG`, the port installs handlers for Lely `diag()` and `diag_at()` before runtime startup. Lely-originated messages use ULOG tag `lely`; RT-Thread adaptation-layer lifecycle/CAN/timer messages use `lely.rtt`. Fatal Lely diagnostics are flushed and retain the upstream termination contract.

The bridge deliberately reuses RT-Thread's existing ULOG backend and async path. It creates no extra logging queue or thread. CAN RX/status callbacks and the deadline callback remain logging-free because they execute outside the Lely owner context; errors are reported later from owner/caller-thread paths. The port also avoids per-frame RX/TX logging by default so sustained CAN traffic cannot flood the async logging buffer.

## Event and lifecycle split

One `rt_event` object is reused for both owner work and two one-shot lifecycle acknowledgements:

```text
RX_READY / TIMER_DUE / CAN_STATUS / STOP -> owner thread
READY                                  -> start() caller
EXIT                                   -> start()/stop() caller
```

The owner work loop intentionally uses `RT_WAITING_FOREVER`: every condition that should wake it is represented by an explicit event bit, so periodic timeout polling would only consume CPU.

`start()` and `stop()` do **not** wait forever. `start_timeout_ms` bounds the READY wait and `stop_timeout_ms` bounds each EXIT wait. A timeout never frees owner/runtime storage. The caller must call `stop()` again until EXIT has been observed before destroying the runtime. Lifecycle APIs for the same runtime are intentionally single-caller: application code must serialize `start()` / `stop()` / `destroy()` because READY/EXIT are one-caller handshake bits, not broadcast lifecycle synchronization.

External callback lifetime (CAN RX/status and the RT deadline callback) uses an atomic refcount. Cleanup first closes callback admission, then unregisters/stops CAN/timer producers, and finally waits for already-pinned callbacks to release. The owner uses `rt_thread_mdelay(1)` between refcount samples, avoiding both a tight busy loop and an RT-Thread completion dependency. The external `stop()` caller still has a bounded EXIT wait; if that wait expires, runtime storage remains allocated and must not be destroyed until a later `stop()` observes EXIT.

## Time

The port extends `rt_tick_get()` into a monotonic uptime and converts it to `struct timespec`. The value is not UTC and is not a replacement for the C11 time ABI. Unsigned tick subtraction tolerates one RT tick counter wrap between owner samples.

`io_user_timer` publishes its next absolute deadline through the `setnext` callback. The port arms an RT-Thread one-shot timer, rounding deadlines up to avoid early expiry. Deadlines farther than the RT-Thread half-range timer limit are reached through intermediate wakeups. If an RT one-shot timer cannot be armed, the runtime fails closed and requests owner shutdown instead of repeatedly self-waking.

## CAN RX/TX

RX indication only wakes the owner. The owner reads at most `rx_batch` frames per pass from the RT-Thread CAN software FIFO and injects valid frames with `io_user_can_chan_on_msg(..., RT_NULL, 0)`. A full Lely RX queue is never waited on by another thread. After `io_can_net` starts, B3 also performs one explicit RX FIFO probe because `rt_device_set_rx_indicate()` installs the callback but does not replay notifications for frames buffered earlier during bitrate/CAN-FD/filter setup.

TX uses `rt_can_msg.nonblocking = 1`. Startup fails if the selected RT CAN device does not provide `sendmsg_nonblocking`. A write accepted by RT-Thread is treated as completion by `io_user_can`; this is not a hardware TX-complete acknowledgement. A full RT non-blocking TX ring is mapped to Lely `ERRNUM_NOBUFS`, not AGAIN/WOULDBLOCK, so the single-owner executor cannot busy-spin by reposting the same write.

For CAN FD, Lely `can_msg.len` is always a byte count, while RT-Thread BSPs differ on whether `rt_can_msg.len` is a byte count or raw DLC. `can_fd_len_mode` makes that BSP contract explicit. DLC mode converts 0..15 to 0/1/.../8/12/16/20/24/32/48/64 bytes and zero-pads rounded TX payloads; byte mode passes the byte count. CAN FD frames are rejected when either the package or runtime capability is disabled. RT-Thread does not expose ESI in the generic message used by this port, so Lely TX frames carrying ESI are rejected instead of silently dropping the flag.

## Hardware acceptance filters

Hardware filtering is optional and is never the Lely correctness layer. Lely still performs software CAN-ID dispatch through `can_net`/registered receivers.

When `filter_setup == RT_NULL`, B3 installs no restrictive filter and requires the BSP's normal CAN-open state to accept all frames needed by the CANopen network. This is the default correctness policy.

A BSP/application can provide `filter_setup(dev, arg)` to configure RT-Thread hardware acceptance filters before RX/status callbacks are registered. This is a performance optimization intended to reduce ISR/FIFO/owner-thread work. A restrictive filter must include every valid CANopen COB-ID. If COB-IDs can change at runtime, either keep the hardware filter broad enough for all allowed IDs or manage dynamic hardware-filter updates in product code; B3 does not derive or synchronize hardware filters from Lely's changing receiver tree. If `filter_setup()` returns an error after changing hardware state, the hook itself must roll those changes back or leave the device safe for close/retry, because the generic runtime cannot infer BSP-specific filter state.

## CAN status

When the BSP supports `RT_CAN_CMD_SET_STATUS_IND`, the callback only wakes the owner. The owner obtains `RT_CAN_CMD_GET_STATUS`. The conservative default derives active/passive/bus-off only from REC/TEC thresholds and derives specific Lely error flags from changes in RT-Thread's bit/stuff/CRC/form/ACK counters. BSPs whose status fields encode controller-specific state should provide `status_mapper` rather than relying on guessed `errcode` semantics.

If the Lely receive/error queue is full, the old status snapshot is retained so the transition is retried instead of being lost. If status indication is disabled in runtime configuration, status is sampled after RX and timer wakeups; detection latency therefore depends on those wakeups.

## Shutdown order

Shutdown uses the following safety order:

```text
close callback admission
        -> detach RX/status producers and stop controller
        -> remove runtime from CAN callback registry
        -> stop RT one-shot timer
        -> mdelay(1) refcount drain until callback_refs == 0
        -> io_ctx_shutdown()
        -> drain EV cancellation/completion tasks
        -> destroy io_can_net
        -> destroy io_user_can / io_user_timer
        -> destroy ev_loop / io_ctx
        -> send EXIT acknowledgement
```

The callback refcount boundary is required because unregistering a driver callback or stopping a one-shot timer does not prove that a callback which started immediately before that operation has already returned. `io_ctx_shutdown()` is kept before Lely object destruction because it can enqueue cancellation/completion tasks.

## Build dependency

The B3 runtime uses:

```text
RT_USING_HEAP
RT_USING_DEVICE
RT_USING_CAN
RT_USING_EVENT
```

No `RT_USING_DEVICE_IPC` dependency is required by the callback lifetime drain. The port uses RT-Thread event flags plus atomics and `rt_thread_mdelay(1)`. The Lely target policy remains `LELY_NO_THREADS=1`, `LELY_NO_ATOMICS=1` and `LELY_NO_TIMEOUT=1`.

## Validation boundary

The repository-level checks can prove source selection, vendor integrity, feature-policy consistency, comments and static lifecycle relationships. They do not prove the concrete BSP's CAN driver behavior. Target build and board validation still need the actual BSP/toolchain, including CAN bitrate, CAN FD length convention, filter behavior, `RT_CAN_CMD_START`, status semantics, RX pressure and bus-off recovery.
