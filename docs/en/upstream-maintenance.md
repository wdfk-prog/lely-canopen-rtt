# Frozen Upstream Maintenance

> 摘要：How lock files, allowlists, manifests, and tools preserve frozen Lely source identity and target boundaries.

[中文](../zh/upstream-maintenance.md)

The project does not treat an arbitrary Lely checkout as target source. A frozen subset is imported under an explicit vendor policy, hashed, and then narrowed again by the RT-Thread target source allowlist.

## 1. Four metadata files, four responsibilities

| File | Responsibility |
|---|---|
| `metadata/UPSTREAM.lock` | Records origin, archive/ref identity, and verification status. |
| `metadata/VENDOR_ALLOWLIST.txt` | Defines which upstream modules/files may be imported into the frozen snapshot. |
| `metadata/VENDOR_MANIFEST.sha256` | Proves the current frozen bytes have not changed unexpectedly. |
| `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` | Defines which imported `.c` files are eligible for RT-Thread target builds. |

These files are maintenance/build policy and do not enter the firmware as runtime data.

## 2. Current baseline identity

The lock file records the original baseline archive SHA-256. Because the original archive did not provide enough Git metadata to prove an exact source commit, exact-ref fields remain unresolved until a controlled update establishes one.

The lock also records a public GitHub mirror commit observed during earlier work. That observation is a reference point only; it must not be described as a proven byte-for-byte identity for the original archive.

When identity is unresolved, the archive hash plus `VENDOR_MANIFEST.sha256` are the authoritative evidence for the frozen bytes currently used by the project.

## 3. Vendor allowlist versus target source allowlist

`VENDOR_ALLOWLIST.txt` answers "what may be imported from Lely at all?". The retained scope covers the public/include and source areas needed by the pure-C `libc`, `util`, `can`, `co`, `ev`, and `io2` architecture plus license notices.

`RTTHREAD_SOURCE_ALLOWLIST.txt` answers "which imported C files may be candidates for the MCU build?". `SConscript` consumes this list and removes feature-specific files when their Kconfig service is disabled.

Keeping the two policies separate allows the vendor snapshot to preserve a reviewed module closure without compiling every retained file into every firmware profile.

## 4. Integrity check

Run from the repository root:

```sh
./tools/check_vendor.sh
```

The script checks the required metadata, allowed module boundary, symlink policy, retained public-header closure, and SHA-256 manifest.

A PASS proves vendor-snapshot integrity against the checked-in policy. It is not a compiler, linker, ABI, or target-runtime test.

## 5. Update from a Git tag or commit

Preferred form:

```sh
./tools/update_lely.sh --ref <tag-or-commit>
```

An explicit mirror can be supplied when needed:

```sh
./tools/update_lely.sh \
    --ref <tag-or-commit> \
    --remote https://github.com/lely-industries/lely-core.git
```

The tool imports only vendor-allowlisted content and updates the lock/manifest. A resolved Git fetch can then record the concrete commit and verification method.

## 6. Update from a local tree

For a clean Lely Git worktree:

```sh
./tools/update_lely.sh --source /path/to/lely-core
```

For a plain export without `.git`, a known ref can be recorded as user-asserted:

```sh
./tools/update_lely.sh \
    --source /path/to/lely-core-export \
    --ref <known-tag-or-commit>
```

In the export case, the manifest remains the byte-level authority; the asserted ref is not independently verified by the tool.

## 7. What must be reviewed after an upstream update

An upstream update can change feature macros, source dependencies, public structure layout, CANopen APIs, and build assumptions. At minimum review:

- `include/lely/features.h` and ABI-affecting feature macros;
- retained module `Makefile.am` source lists;
- new/removed sources under retained modules;
- `RTTHREAD_SOURCE_ALLOWLIST.txt` coverage;
- Kconfig/SConscript feature-to-source mapping;
- `port/rtthread/lely_rtt_config.h` policy;
- RT-Thread adaptation calls against changed Lely APIs;
- Host generation behavior and generated OD diffs;
- vendor check, Host/source tests, real BSP build, and target CAN validation.

Do not treat a clean manifest as proof that a new upstream version is port-compatible.

## 8. License files

`LICENSE` and `NOTICE` stay visible at the repository root because they are redistribution notices, not internal metadata.
