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

`dcfgen -r` also mirrors Node1 TPDO1 into the Master description. The checked-in
first-stage OD therefore contains Master RPDO1 (`0x1400/0x1600`), a local mapped
value (`0x2000:01`), and remote PDO metadata (`0x5800/0x5A00`) for Node1's
`0x2001:00` mapping. This only establishes the generated/static OD structure;
B4 does not add a generic application PDO API.

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
