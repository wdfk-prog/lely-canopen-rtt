# Master + remote Node1 example

This directory is the corrected B4 role model from Issue #3. The MCU owns a
local CANopen Master object dictionary (`master_sdev`); `../node1/node1.dcf`
describes the remote slave Node1 and is only a Host-side generation input.

Generation chain:

```text
../node1/node1.dcf
        -> master.yml
        -> dcfgen -r -> staging full Master DCF
        -> compact_master_dcf.py -> master.dcf
        -> dcf2c --no-strings -> master_sdev.c
                                  + master_sdev.h (project-maintained declaration)
```

The target keeps `LELY_NO_STDIO=1`, `LELY_NO_CO_DCF=1` and
`LELY_NO_CO_OBJ_FILE=1`; it never parses `master.dcf` or `node1.dcf` at runtime.

## Checked-in first-stage policy

The checked-in OD is intentionally conservative while Issue #3 product policy
items remain unconfirmed:

- local Master Node-ID is `127` as a runnable example value; the product must confirm or replace it. `0xFF` is accepted by `co_dev_t` as the unconfigured sentinel, but Lely does not advance an NMT service with that ID into Pre-operational;
- Node1 is in the network list and NMT boot is enabled;
- Node1 is not marked mandatory;
- automatic Reset Communication is enabled in the checked-in example (`reset_communication: true`); the product must confirm or change this before CAN HIL;
- automatic NMT Start of Node1 is disabled;
- Node1 heartbeat producer is currently 1000 ms in `node1.dcf`; the checked-in
  consumer timeout is 3000 ms (explicit 3.0 multiplier in `master.yml`);
- expected product is taken from Node1's DCF, while revision/serial are pinned
  to `0x00000001` in `master.yml`; all three identity checks are therefore
  present in the generated Master OD. Device-type and vendor checks remain
  disabled because their current DCF values are zero.

These are example-generation values, not product ABI. Confirm them before CAN
HIL and regenerate the artifacts instead of adding policy to `runtime.c`.

`dcfgen -r` mirrors both Node1 PDO directions into the Master description. The
checked-in OD contains Master RPDO1 (`0x1400/0x1600`) mapped to local
`0x2000:01` for Node1 TPDO1 (`0x181`), plus Master TPDO1 (`0x1800/0x1A00`)
mapped from local `0x2200:01` to Node1 RPDO1 (`0x201`). Remote mapping metadata
is retained at `0x5800/0x5A00` and `0x5C00/0x5E00`. B5.2 applications update
`0x2200:01` through the owner-safe local OD API and then call
`lely_rtt_runtime_tpdo_event(runtime, 1)` (or `co tpdo event 1`). B9 can switch
these same local PDOs to synchronous transmission at runtime without changing
the shared DCF/SDEV defaults.

Node1 also exposes EMCY at `0x081`; the Master `0x1028:01` consumer entry is
configured for that COB-ID. With the B6 bridge enabled, `co emcy` or
`co emcy 1` reads the bounded remote EMCY history without exposing Lely objects
to non-owner threads.

## SYNC and synchronous PDO example

Enable `PKG_LELY_USING_MASTER_SYNC_PDO` together with the existing Master+Node1
example. The shared checked-in DCF/SDEV deliberately keeps its pre-B9 defaults:
object `0x1006` is zero and RPDO1/TPDO1 remain event-driven type 255. This
preserves the existing B5.2 example when B9 is disabled. The Master OD already
contains SYNC producer object `0x1005`, so B9 can opt into a real synchronous
cycle at runtime without replacing or remapping the generated OD.

Use the owner-safe controls to enter synchronous mode explicitly:

```text
co sync period 1000000
co pdo trans rx 1 1
co pdo trans tx 1 1
co sync status
```

After these commands the local Master produces SYNC every 1,000,000 us, TPDO1
uses cyclic synchronous transmission type 1, and RPDO1 applies received Node1
TPDO data at the synchronous boundary. Node1 remains event-driven in this shared
fixture so the pre-B9 B5.2 smoke path is unchanged; a product that requires the
remote node itself to participate synchronously should configure that node's
SYNC consumer and PDO communication parameters through its normal device
configuration flow.

Lely handles each local SYNC in owner-thread order: synchronous TPDOs are
sampled/transmitted first, synchronous RPDO data is then committed to the local
OD, and only afterwards does B9 publish its application SYNC
indication/snapshot. `co sync status` therefore observes a post-PDO boundary
rather than a pre-PDO notification.

The same controls are available to product code through
`lely_rtt_runtime_sync_set_period()`,
`lely_rtt_runtime_pdo_get_transmission()` and
`lely_rtt_runtime_pdo_set_transmission()`. These calls update the active Lely
service through the owner queue; they do not perform dynamic PDO remapping.

Transmission type 0 is supported for TPDO event-on-next-SYNC behavior. After
setting TPDO1 to type 0, update its mapped OD value and call
`lely_rtt_runtime_tpdo_event(runtime, 1)`; the event is armed immediately but
the PDO is sampled and sent only when the next SYNC is processed. Types 1..240
are cyclic synchronous and are driven only by SYNC. Types 254/255 preserve the
existing event-driven behavior. RTR-only/reserved types remain outside B9.

The optional callback registered by `lely_rtt_runtime_configure_sync_ind()` runs
in the Lely owner thread after the synchronous PDO work. The registration
persists across stop/start cycles until reconfigured or the runtime is destroyed.
Keep the callback and its data alive for that lifetime, keep the callback bounded
and non-blocking, and do not call a runtime API that waits for owner completion.
For application threads that only need observation, prefer
`lely_rtt_runtime_get_sync()` and the local-OD snapshot/read APIs.

## Manual NMT configuration example

When both `PKG_LELY_EXAMPLE_MASTER_NODE1` and
`PKG_LELY_USING_MASTER_NMT_CFG` are enabled, the target also builds
`master_cfg_dcf.c`. Its concise DCF contains one real remote write:
`0x1017:00 = 1000` (`UNSIGNED16`), matching Node1's heartbeat-producer
configuration. The auto-init path copies this data into the runtime before the
owner thread starts. Product code that creates a runtime explicitly can use the
same order:

```c
lely_rtt_runtime_configure_master(runtime, &master_sdev);
lely_rtt_runtime_configure_nmt_dcf(runtime, 1,
        master_node1_cfg_dcf, master_node1_cfg_dcf_size);
lely_rtt_runtime_start(runtime);
```

After the runtime is started, `co cfg 1 1000` enters the existing owner queue
and `co_nmt_cfg_req()` path. Lely first executes any 0x1F22 data present in the
Master OD; its application `cfg_ind` stage then executes the copied concise DCF
through the same configuration-owned Client-SDO and completes it with
`co_nmt_cfg_res()`. The checked-in Master OD still has no 0x1F22 object, so this
example exercises the application `cfg_ind` branch directly.

The copied application DCF is deliberately manual-only. Automatic NMT boot or
other Lely configuration activity reaches the installed callback but does not
consume this application source unless a manual RT-Thread CFG request currently
owns that Node-ID. This keeps the existing startup policy unchanged.

The checked-in `master_cfg_dcf.c/.h` are generated Host artifacts. Lely's
official `dcfgen` performs the CANopen-aware SDO encoding from the dedicated
`master_cfg.yml` and produces a staging `node1.bin`; `tools/gen_cfg_dcf.py`
validates that concise DCF and embeds it as the C array used by the RT-Thread
example. The staging `master.dcf` is discarded, so this generation path does not
populate object 0x1F22 or change automatic boot behavior. Regenerate from the
repository root on the Windows Host with:

```powershell
.\.venv\Scripts\python.exe .\tools\gen_cfg_dcf.py `
    --yml .\examples\master_node1\master_cfg.yml `
    --node node1 `
    --symbol master_node1_cfg_dcf `
    --basename master_cfg_dcf `
    --out-dir .\examples\master_node1 `
    --expect-entries 1
```

The generator also accepts `--bin <file>` when a concise DCF has already been
produced by `dcfgen`. Do not hand-edit the generated C/H bytes.

MSH reports the terminal classification together with `stage`, `source`, entry
count and the last application DCF object when available. A successful Node1
request therefore reports `source=app-dcf`, `entries=1` and the final object
`1017:00`. Protocol failures retain the SDO abort code and the last stage reached.

## Windows Host regeneration

Windows is the primary Host workflow for this project package. The package
already contains `../../tools/dcf2c.exe`. From the repository root, use the
Windows dependency set that has been verified on a real host:

```powershell
.\tools\dcf2c.exe --help
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install --force-reinstall `
    "setuptools==81.0.0" `
    "empy==3.3.4" `
    "dcf-tools==2.4.2"
.\.venv\Scripts\dcfgen.exe --help
```

A `pkg_resources is deprecated` warning from `dcfgen` is non-fatal when the
command continues and prints its usage/output. The generator intentionally does
not re-validate Python package versions: successful execution of `dcfgen` is the
runtime check. `tools\setup_dcfgen_windows.ps1` is only an optional convenience
wrapper for the same installation sequence.

There is no Master+Node1-specific generator anymore. Use the generic
`tools\gen_sdev.ps1` directly so every YAML/DCF conversion follows one Host
entry point. For this MCU example, keep the safety options shown below:
`-CompactMaster` shrinks dcfgen's large `CompactSubObj=127/254` Manager arrays,
`-NoStrings` omits optional OD names, `-NoHeader` preserves the project-maintained
`master_sdev.h`, and `-MetaFile` refreshes the checked-in generation metadata.

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

The compactor uses the highest configured remote node-ID from `0x1F81` for
node-indexed Manager arrays, caps `0x1003` error history at 8 entries for this
example, and rejects a compacted DCF that still estimates more than 256
sub-objects. These are Host-generation safety limits; they do not move any
product policy into `runtime.c`.

If a previous generation produced a very large `master_sdev.c`, rerun the
generic command above after updating these tools. Its output must contain a line
beginning with `Master DCF footprint estimate:` before the C file is published.

The current Linux review environment cannot execute the bundled Windows
`dcf2c.exe`, so Windows Host regeneration is still a manual verification item.
See `../../docs/DCF_DCF2C_CANOPENEDITOR.md` for tool download, installation,
ExecutionPolicy and troubleshooting details.
