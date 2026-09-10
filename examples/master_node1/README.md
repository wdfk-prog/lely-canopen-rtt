# Master + Remote Node1 Example

> 摘要：Checked-in Master/Node1 policy, PDO/EMCY/SYNC paths, and Host regeneration contract for the RT-Thread example.

[中文](README.zh-CN.md)

This example demonstrates the repository's corrected CANopen roles: the MCU owns the local `master_sdev`, while `../node1/node1.dcf` describes a remote slave and remains a Host-side generation input.

## Generation chain

```text
../node1/node1.dcf
        -> master.yml
        -> dcfgen -r
        -> compact_master_dcf.py
        -> master.dcf
        -> dcf2c --no-strings
        -> master_sdev.c
```

`master_sdev.h` is a project-maintained declaration header. The target keeps `LELY_NO_STDIO=1`, `LELY_NO_CO_DCF=1`, and `LELY_NO_CO_OBJ_FILE=1`; it does not parse either DCF at runtime.

## Checked-in example policy

The generated configuration is intentionally a runnable fixture rather than product policy:

| Item | Checked-in value/behavior |
|---|---|
| Local Master Node-ID | `127` example value. |
| Remote Node1 | Present in the network list; NMT boot enabled. |
| Mandatory node | No. |
| Reset Communication | Enabled by current YAML example. |
| Automatic NMT Start | Disabled. |
| Node1 Heartbeat producer | `1000 ms`. |
| Master Heartbeat consumer timeout | `3000 ms` through the YAML multiplier. |
| Identity checks | Product from Node1 DCF; revision/serial pinned to `1`; zero vendor/device-type values are not used as checks. |

Confirm or replace these values before CAN HIL or product use. Regenerate the OD instead of moving product policy into `runtime.c`.

## PDO paths

The checked-in OD contains both PDO directions:

```text
Node1 TPDO1 0x181
    -> Master RPDO1 0x1400/0x1600
    -> Master local 0x2000:01

Master local 0x2200:01
    -> Master TPDO1 0x1800/0x1A00
    -> Node1 RPDO1 0x201
    -> Node1 0x2000:00
```

The shared fixture keeps RPDO1/TPDO1 event-driven by default. This preserves the simple TPDO-event smoke path when the SYNC/PDO feature is disabled.

With `PKG_LELY_USING_MASTER_PDO_TX`, application code can update the mapped local value through the owner-safe OD API and trigger TPDO1 with `lely_rtt_runtime_tpdo_event(runtime, 1)`.

## SYNC and synchronous PDO

With `PKG_LELY_USING_MASTER_SYNC_PDO`, the same static mapping can be switched to synchronous transmission at runtime without dynamic remapping:

```text
co sync period 1000000
co pdo trans rx 1 1
co pdo trans tx 1 1
co sync status
```

The application-visible SYNC snapshot/callback is published after the synchronous TPDO and RPDO work for that SYNC has completed in owner-thread order.

Type 0 is supported for TPDO event-on-next-SYNC. Types 1..240 are synchronous; 254/255 keep event-driven behavior. RTR-only/reserved modes are outside the current bridge.

## EMCY

Node1 uses EMCY COB-ID `0x081`, and the Master OD contains the matching consumer entry. With `PKG_LELY_USING_MASTER_EMCY`, `co emcy`/`co emcy 1` reads the bounded owner-published history without exposing Lely objects to MSH/application threads.

## Manual NMT configuration

When `PKG_LELY_USING_MASTER_NMT_CFG` is enabled, the checked-in `master_cfg_dcf.c` contains one application concise-DCF write:

```text
0x1017:00 = 1000 ms
```

The auto-init path registers this copied DCF before start. Explicit applications use the same order:

```c
lely_rtt_runtime_configure_master(runtime, &master_sdev);
lely_rtt_runtime_configure_nmt_dcf(runtime, 1,
        master_node1_cfg_dcf, master_node1_cfg_dcf_size);
lely_rtt_runtime_start(runtime);
```

The application DCF is manual-only. Automatic NMT boot does not consume it.

## Windows regeneration

Run from the repository root:

```powershell
.\tools\gen_sdev.ps1 `
    -Yml .\examples\master_node1\master.yml `
    -Name master_sdev `
    -OutDir .\examples\master_node1 `
    -DcfFileName master.dcf `
    -RemotePdo `
    -CompactMaster `
    -ErrorHistoryDepth 8 `
    -MaxMasterSubObjects 256 `
    -NoStrings `
    -NoHeader `
    -MetaFile master_sdev.meta
```

For the manual application DCF:

```powershell
.\.venv\Scripts\python.exe .\tools\gen_cfg_dcf.py `
    --yml .\examples\master_node1\master_cfg.yml `
    --node node1 `
    --symbol master_node1_cfg_dcf `
    --basename master_cfg_dcf `
    --out-dir .\examples\master_node1 `
    --expect-entries 1
```

See [Host generation](../../docs/en/host-generation.md) and [Master control plane](../../docs/en/master-control-plane.md).
