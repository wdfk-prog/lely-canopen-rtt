# CANopen Master Control Plane

> 摘要：Owner-safe NMT, SDO, local OD, PDO/SYNC, EMCY, TIME, and optional MSH controls for the CANopen Master.

[中文](../zh/master-control-plane.md)

All control-plane functions preserve the single-owner rule. Application and MSH threads never receive raw `co_dev_t`, `co_nmt_t`, `co_csdo_t`, PDO, EMCY, SYNC, or TIME service pointers.

## 1. Observation through snapshots

The runtime publishes stable snapshots for information that non-owner threads need to read:

- local Master NMT state;
- last known remote NMT state;
- most recent completed remote NMT boot result;
- local OD write metadata;
- processed SYNC sequence/counter/role/period;
- retained remote EMCY events;
- last received CANopen TIME value.

Snapshot APIs do not enter Lely. They may return a not-ready/busy result when no usable owner-published value exists.

## 2. NMT command ingress

`PKG_LELY_USING_MASTER_COMMAND` creates one per-runtime RT-Thread message queue. `lely_rtt_runtime_post_nmt()` posts an NMT command and returns after queue admission, not after the remote node completes its state transition.

That distinction matters: a successful post means "queued for owner dispatch". Use the remote NMT snapshot later to observe actual node state.

During shutdown, command admission closes before teardown so queued work cannot cross the lifetime boundary.

## 3. Client-SDO transactions

`PKG_LELY_USING_MASTER_SDO` adds request-object based upload/download APIs, including block transfer and explicit cancellation.

The request lifecycle is:

```text
create request
    -> post upload/download
    -> owner starts/owns Lely CSDO operation
    -> wait or cancel
    -> read terminal result
    -> destroy request
```

Important contracts:

- one application SDO transaction is active per remote Node-ID, with up to `PKG_LELY_MASTER_SDO_QUEUE_DEPTH` additional requests waiting in a per-node FIFO;
- different remote Node-IDs may have application SDO transactions active in parallel;
- the default path lazily creates an application-owned Client-SDO using the CiA 301 predefined connection and does not borrow the NMT boot Client-SDO;
- `lely_rtt_runtime_configure_sdo_channel()` may select a startup-only custom Client-SDO number `1..128`, corresponding to local Master objects `0x1280..0x12FF`; `0` keeps the predefined path;
- a selected custom Client-SDO is borrowed from the Lely NMT service manager and is never stopped or destroyed by the application bridge;
- custom communication parameters are validated after local NMT reset and again immediately before a queued request becomes active, so a runtime parameter drift fails locally instead of starting on a stale/overlapping channel;
- download payloads are copied before a successful post returns;
- upload result data remains owned by the request until request destruction succeeds;
- protocol timeout, SDO abort code, local error, cancellation, and shutdown cancellation remain distinct terminal outcomes.

Reset/stop transitions, remote Boot-up, and manual NMT configuration cancel or block the application SDO boundary for that node. This applies to both predefined and selected custom channels so application requests cannot interleave with NMT boot/configuration ownership.

## 4. Manual NMT configuration

`PKG_LELY_USING_MASTER_NMT_CFG` exposes `co_nmt_cfg_req()` through the owner boundary. A useful manual request needs configuration data from one of the supported sources, including a startup-registered application concise DCF.

`lely_rtt_runtime_configure_nmt_dcf()` is startup-only and copies/validates the concise DCF. The registered data is deliberately manual-only: automatic NMT boot does not consume it.

The current bridge rejects the `0x1F8A` restore/reset path because it conflicts with the runtime's Master Boot-up/NMT-boot ownership model.

## 5. Local OD, application hooks, and TPDO

`PKG_LELY_USING_LOCAL_OD` provides synchronous owner-dispatched read/write for local `0x2000..0x5FFF` objects.

`PKG_LELY_USING_MASTER_OD_HOOKS` adds two startup-only application integration points on top of that bridge:

- `lely_rtt_runtime_configure_local_od_upload_ind()` supplies a dynamic upload value for one manufacturer OD entry;
- `lely_rtt_runtime_configure_local_od_change_ind()` notifies the application immediately after a successful local/protocol write commits.

The upload hook replaces the registered entry's upload indication for the current runtime run and the previous Lely indication is restored on stop; the registration itself persists across stop/start. Callback bytes use CANopen SDO transfer encoding. Successful-write notification runs after snapshot publication, is notification-only, and cannot roll back a committed write.

Both callbacks execute in the Lely owner thread and must remain bounded/non-blocking. They must not call runtime APIs that wait for owner completion. Unregistered entries keep their existing behavior and raw `co_dev_t/co_sub_t` ownership is never exposed to the application.

`PKG_LELY_USING_MASTER_PDO_TX` lets application code trigger an already configured static Master TPDO. The normal pattern is:

```text
write mapped local OD value
    -> trigger TPDO event
    -> owner invokes configured TPDO service
```

This does not remap PDOs dynamically.

## 6. SYNC and synchronous PDO

`PKG_LELY_USING_MASTER_SYNC_PDO` adds:

- owner-safe object `0x1006` period control;
- local RPDO/TPDO transmission-type get/set;
- a processed-SYNC snapshot;
- an optional startup-registered application callback.

Supported transmission-type control includes synchronous `0..240` and existing event-driven `254/255`. Type 0 provides event-on-next-SYNC behavior for TPDO. RTR-only/reserved modes and dynamic PDO remapping remain outside the bridge.

The application SYNC indication is published after Lely has processed synchronous TPDOs and then committed synchronous RPDO data. It therefore marks a post-PDO boundary for that SYNC cycle.

A registered callback executes in the owner thread. It must stay bounded/non-blocking and must not call a runtime API that waits for owner completion.

## 7. EMCY

`PKG_LELY_USING_MASTER_EMCY` provides two directions:

- received remote EMCY indications are copied into a bounded per-runtime history ring;
- local EMCY producer operations are dispatched through the owner.

The history path uses fixed storage with atomic publication and does not allocate per event. The depth is configured by `PKG_LELY_MASTER_EMCY_HISTORY_DEPTH`.

The runtime rebinds its indication when Lely recreates the local EMCY service after relevant NMT lifecycle changes.

## 8. TIME

`PKG_LELY_USING_MASTER_TIME` keeps CANopen TIME separate from the transport clock. Received TIME values are published as snapshots. Producer sends are explicit caller-supplied absolute time values.

The runtime does not automatically use monotonic `rt_tick_get()` uptime as wall clock and does not start periodic TIME production on its own.

## 9. MSH command surface

With `PKG_LELY_USING_MSH`, the default auto-init Master exports one root command:

```text
co status
co node <node-id>
co boot <node-id>
co nmt start|stop|preop|reset-node|reset-comm <node-id|all>
co cfg <node-id> <timeout-ms>
co od status
co od read <index> <subindex> <type>
co od write <index> <subindex> <type> <value>
co tpdo event <pdo-number>
co sync status
co sync period <microseconds>
co pdo trans rx|tx <pdo-number> [0..240|254|255]
co emcy [node-id]
co emcy push <eec> <error-register> [msef-10hex]
co emcy pop|clear
co time status
co time mode off|consumer|producer|both
co time send <unix-sec> <nanoseconds>
co sdo read <node> <index> <subindex> <type> <timeout-ms>
co sdo write <node> <index> <subindex> <type> <value> <timeout-ms>
```

Only commands backed by enabled Kconfig features are printed/accepted. Scalar MSH OD/SDO types are `bool|u8|u16|u32|i8|i16|i32`. Block SDO and explicit cancellation are application APIs rather than MSH scalar commands.

## 10. What the control plane does not provide

The current API deliberately does not expose raw Lely Client-SDO/OD ownership, runtime mutation of custom `0x1280..0x12FF` communication parameters, general dynamic PDO remapping, or an automatic wall-clock synchronization policy. The application OD write callback is currently a post-commit notification rather than a second pre-commit download validator. Custom CSDO selection is limited to the startup-only numbered-channel selector described above; direct use of the same raw Lely CSDO while application requests are active remains outside the contract.
