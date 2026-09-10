# Object Dictionary Model

> 摘要：Separates the MCU-local Master OD from remote DCF inputs and shows how Node1 participates in Host generation.

[中文](../zh/object-dictionary.md)

The most important Object Dictionary rule in this repository is ownership: the MCU instantiates one local static CANopen device description, while remote-node DCF files remain Host-side configuration inputs.

## 1. Local and remote roles

For the checked-in example:

```text
examples/node1/node1.dcf
        | remote slave description only
        v
examples/master_node1/master.yml
        | dcfgen -r
        v
examples/master_node1/master.dcf
        | compact + dcf2c
        v
examples/master_node1/master_sdev.c
        | target build
        v
co_dev_create_from_sdev(&master_sdev)
        v
co_nmt_create(...)
```

`master_sdev` is the local MCU Object Dictionary. `node1.dcf` is not instantiated on the MCU and is never parsed at runtime.

The target policy intentionally keeps:

```text
LELY_NO_CO_DCF=1
LELY_NO_CO_OBJ_FILE=1
LELY_NO_STDIO=1
```

That keeps text/file-backed DCF processing in the Host toolchain.

## 2. Why CANopenEditor output and Lely static OD are different

CANopenEditor can export `OD.c`/`OD.h` for CANopenNode. Those files belong to the CANopenNode stack and are not the static-device format consumed by Lely.

For this repository, CANopenEditor is useful as an EDS/DCF editor. The Lely path is:

```text
CANopenEditor / vendor EDS
        -> DCF
        -> Lely dcf2c
        -> const struct co_sdev
```

The checked-in `.h` files are thin project-maintained declarations. `dcf2c` itself generates the C static description; it does not define this repository's header policy.

## 3. The Node1 fixture

`examples/node1/node1.dcf` is a test fixture, not a vendor-supplied product file. It uses Node-ID 1 and provides the communication objects needed by the repository's Master smoke paths.

Key default CAN identifiers are:

```text
EMCY      0x081
TPDO1     0x181
RPDO1     0x201
SSDO TX   0x581
SSDO RX   0x601
Heartbeat 0x701
```

Its two manufacturer objects are used as simple PDO/SDO test values:

| Object | Role |
|---|---|
| `0x2000:00` | `UNSIGNED32` value mapped to Node1 RPDO1. |
| `0x2001:00` | `UNSIGNED32` value mapped to Node1 TPDO1. |

The Node1 default PDOs are event-driven. RPDO1 maps `0x2000:00` with mapping value `0x20000020`; TPDO1 maps `0x2001:00` with `0x20010020`.

## 4. What the generated Master OD contains

`dcfgen -r` mirrors the remote PDO relationships into the Master description. In the checked-in example, Master-side application objects provide local endpoints for those remote PDOs. The static Master OD also carries NMT manager information, Node1 identity expectations, Heartbeat/NMT boot information, and the configured remote EMCY consumer entry.

The checked-in Master policy is intentionally example policy, not a product ABI. Values such as local Master Node-ID, Node1 mandatory/optional status, reset behavior, identity checks, and heartbeat multiplier must be reviewed before a product uses the generated OD.

## 5. Local application OD access

When `PKG_LELY_USING_LOCAL_OD` is enabled, application/MSH threads may access only the local manufacturer-specific range:

```text
0x2000..0x5FFF
```

The request crosses the owner queue and the actual `co_dev_t` access happens in the Lely owner thread. Existing Lely download indications are chained so Server-SDO and RPDO writes continue to work. The bridge publishes metadata for the most recent observed local write without exposing `co_dev_t` to non-owner code.

This API is not a generic remote OD API. Remote objects are read or written through Client-SDO.

## 6. Static mapping versus dynamic reconfiguration

The control plane can trigger a configured TPDO and can change local RPDO/TPDO transmission types when the SYNC/PDO feature is enabled. It does not implement dynamic PDO remapping.

If the product needs a different PDO layout, change the Host-side OD/network configuration and regenerate the static artifacts instead of inventing a runtime mapping policy in the RT-Thread port.

## 7. Product replacement

A product should normally replace the example Master/Node DCF/YAML values with its own device identities, communication parameters, PDO layout, Heartbeat policy, startup policy, and manufacturer objects. Keep the same ownership model:

- product configuration on the Host;
- generated local static `co_sdev` for the MCU;
- remote DCF/EDS files as Host inputs;
- no target-side text DCF parsing unless the architecture is deliberately changed.

Generation details are in [Host generation](host-generation.md).
