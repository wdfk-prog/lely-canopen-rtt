# Remote Node1 Fixture

> 摘要：Node1 test DCF provenance, CAN identifiers, PDO mappings, and regeneration path for the remote slave fixture.

[中文](README.zh-CN.md)

`node1.dcf` is the repository's minimal remote CANopen slave fixture for Node-ID 1. It is not a vendor file and is not the MCU's local Object Dictionary in the Master example.

The fixture was derived from Lely test-DCF structure and then adapted for this repository's NMT/Heartbeat/SDO/PDO/EMCY smoke paths. CANopen communication-object semantics follow the project use of CiA 301; the EDS/DCF file format is a Host-side configuration format.

## Key defaults

```text
Node-ID   = 1
EMCY      = 0x081
TPDO1     = 0x181
RPDO1     = 0x201
SSDO TX   = 0x581
SSDO RX   = 0x601
Heartbeat = 0x701
```

The Heartbeat producer period is `1000 ms`.

## PDO test values

| Object | Type | Purpose |
|---|---|---|
| `0x2000:00` | `UNSIGNED32` | RPDO/SDO smoke value. |
| `0x2001:00` | `UNSIGNED32` | TPDO smoke value. |

RPDO1 maps `0x2000:00` with mapping entry `0x20000020`. TPDO1 maps `0x2001:00` with `0x20010020`. Both are event-driven in the checked-in fixture.

In the Master example, Node1 TPDO1 feeds Master RPDO1/local `0x2000:01`, while Master local `0x2200:01` is mapped to Master TPDO1 and then Node1 RPDO1.

## Regeneration

Run from the repository root:

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\examples\node1 `
    -NoHeader `
    -MetaFile node1_sdev.meta
```

Direct `dcf2c` is also possible:

```powershell
.\tools\dcf2c.exe `
    -o .\examples\node1\node1_sdev.c `
    .\examples\node1\node1.dcf `
    node1_sdev
```

`node1_sdev.c` is a generated static-device reference. `node1_sdev.h` is a project-maintained thin declaration header. In the Master example, do not link `node1_sdev.c` as the MCU's local device; the local device is `../master_node1/master_sdev.c`.

See [Object Dictionary](../../docs/en/object-dictionary.md) and [Host generation](../../docs/en/host-generation.md).
