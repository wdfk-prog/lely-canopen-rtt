# Host Generation: DCF, dcfgen, dcf2c, and CANopenEditor

> 摘要：Windows-first Host flow from YAML/DCF inputs to compact static Lely SDEV artifacts for the RT-Thread target.

[中文](../zh/host-generation.md)

The target does not parse DCF files. All text-file parsing, network configuration expansion, compacting, and C generation happen on the Host before the firmware build.

## 1. Tool roles

| Tool | Role in this repository |
|---|---|
| CANopenEditor | Edit/import EDS/DCF and export a concrete DCF when a GUI workflow is useful. |
| `dcfgen` from `dcf-tools` | Build a CANopen Master DCF from YAML and remote device descriptions; also emits concise DCF data for slave configuration. |
| `tools/compact_master_dcf.py` | Reduce large Manager `CompactSubObj` ranges and materialize supported Master-local initialization data. |
| Lely `dcf2c` | Convert DCF/EDS into C99 `const struct co_sdev` static initialization. |
| `tools/gen_sdev.ps1` | Repository-wide Windows entry point around the YAML/DCF-to-static-SDEV flow. |
| `tools/gen_cfg_dcf.py` | Extract/validate one concise DCF blob and wrap it as application C/H data for manual NMT configuration. |

CANopenEditor's CANopenNode `OD.c`/`OD.h` exporter is not used by the Lely target.

## 2. Windows environment

The repository contains a Windows x86-64 `tools\dcf2c.exe`. Check it first:

```powershell
.\tools\dcf2c.exe --help
py -3 --version
```

The checked-in Python dependency set for the current Host workflow is:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install --force-reinstall `
    "setuptools==81.0.0" `
    "empy==3.3.4" `
    "dcf-tools==2.4.2"
.\.venv\Scripts\dcfgen.exe --help
```

A `pkg_resources is deprecated` warning is not itself a failure if `dcfgen` continues and produces normal help/output.

`tools\setup_dcfgen_windows.ps1` is an optional convenience wrapper; the commands above remain the transparent setup path.

## 3. Generate the checked-in Master

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

This is the single Master + Node1 generation entry point. There is no separate example-specific wrapper.

The important safety switches are not cosmetic:

- `-CompactMaster` contracts `dcfgen` Manager ranges before `dcf2c` so a one-node example does not expand hundreds of unnecessary dynamic `co_sub_t` objects on the MCU.
- `-ErrorHistoryDepth 8` bounds the generated `0x1003` history for this fixture.
- `-MaxMasterSubObjects 256` fails generation when the compacted OD still exceeds the example footprint gate.
- `-NoStrings` omits optional OD name strings.
- `-NoHeader` preserves the project-maintained declaration header.
- `-MetaFile` refreshes generation provenance metadata.

Do not feed an un-compacted `dcfgen` Master DCF directly into target `dcf2c` for this MCU profile.

## 4. Why Master DCF compaction exists

Lely's standard Master template can create Manager arrays with `CompactSubObj=127/254`. Expanding those ranges into target-side objects increases heap consumption even when the network has only Node1.

The compactor uses the highest configured remote Node-ID to shrink node-indexed Manager arrays, caps error history according to the command-line policy, and materializes supported Master-local initialization writes produced by `dcfgen` before `dcf2c` runs.

For the checked-in fixture, supported values include identity-related Master-local initialization such as revision/serial entries used by the generated configuration. File-backed `UploadFile`/`DownloadFile` values are rejected because the target is built with `LELY_NO_CO_OBJ_FILE=1`.

The generator also corrects static SDEV default initializers from the compact DCF `DefaultValue` where needed so a current `ParameterValue` is not accidentally treated as the reset/default value.

## 5. Generate Node1 static reference

The Node1 static C file is retained as provenance/reference and is not the MCU's local Master OD. To regenerate it:

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\examples\node1 `
    -NoHeader `
    -MetaFile node1_sdev.meta
```

Direct `dcf2c` use is also possible:

```powershell
.\tools\dcf2c.exe `
    -o .\examples\node1\node1_sdev.c `
    .\examples\node1\node1.dcf `
    node1_sdev
```

On Linux/macOS, prefer a `dcf2c` built from the same Lely revision as the frozen target sources. Because `metadata/UPSTREAM.lock` does not yet prove an exact upstream ref, an arbitrary system-installed `dcf2c` is an unverified path; if it is used for exploratory regeneration, review the generated diff and confirm the target build separately.

## 6. Generate application concise DCF data

Manual NMT configuration can register a copied concise DCF before runtime start. The checked-in Node1 configuration example writes `0x1017:00 = 1000 ms`.

Generate its C/H data with:

```powershell
.\.venv\Scripts\python.exe .\tools\gen_cfg_dcf.py `
    --yml .\examples\master_node1\master_cfg.yml `
    --node node1 `
    --symbol master_node1_cfg_dcf `
    --basename master_cfg_dcf `
    --out-dir .\examples\master_node1 `
    --expect-entries 1
```

`dcfgen` performs CANopen-aware value encoding. `gen_cfg_dcf.py` stages the generation, validates concise-DCF framing, selects the requested slave blob, and wraps it as C data. It does not populate automatic NMT boot configuration implicitly.

## 7. CANopenEditor workflow

For a product device, a practical flow is:

1. open a vendor EDS or create a CANopenEditor project;
2. configure concrete Node-ID, identity, bitrate capability, Heartbeat, SDO, PDO, and manufacturer objects;
3. export a CiA 306-1 Device Configuration File (`.dcf`);
4. use that DCF as the Lely Host-generation input;
5. regenerate and review the resulting static C diff.

For the checked-in Node1 fixture, the manufacturer range uses `0x2000:00` and `0x2001:00` for simple RPDO/TPDO smoke values.

One parser-compatibility detail is version-specific: the current Lely parser expects the DCF section spelling `[DeviceComissioning]`. Do not 'correct' this text in a file consumed by that parser without checking the exact upstream version.

## 8. Upstream identity caveat

`metadata/UPSTREAM.lock` currently records the original archive identity separately from an observed public GitHub mirror commit. An observed commit is not proof that the frozen archive bytes are identical to that commit. Until an exact ref is established by a controlled import, use the lock file plus vendor manifest as the source identity evidence.

See [Upstream maintenance](upstream-maintenance.md).
