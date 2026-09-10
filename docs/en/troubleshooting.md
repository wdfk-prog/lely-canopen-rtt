# Troubleshooting

> 摘要：Diagnoses Host generation, source selection, runtime startup, CAN driver, lifecycle, and Master-service failures.

[中文](../zh/troubleshooting.md)

Troubleshooting is most reliable when each symptom is first assigned to the layer that can actually produce it. Do not use a later timeout or shell error as the root cause when earlier evidence already shows an owner, generation, or CAN-driver failure.

## 1. `SConscript` reports a missing upstream source

Relevant evidence:

- `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` contains the candidate path;
- the frozen `upstream/` snapshot contains or does not contain that file;
- the selected Kconfig feature requires or removes it.

Check vendor integrity first:

```sh
./tools/check_vendor.sh
```

If the source is intentionally new after an upstream update, update both the vendor review boundary and the target source policy as needed. Do not fix the error by scanning/compiling the entire upstream tree.

## 2. ABI or undefined-symbol errors around threads/TLS/atomics

The target policy is single-owner and intentionally disables Lely's internal thread/atomic backends. If target objects reference C11 threads, pthread, compiler TLS helpers, or an unexpected Lely backend, compare all translation units against `port/rtthread/include/lely/features.h` and `lely_rtt_config.h`.

Do not define conflicting `LELY_NO_*` macros in generated OD files or application compile flags. ABI-affecting feature policy must be identical across Lely and consumers of public structures.

## 3. `runtime start` times out

`start_timeout_ms` only bounds the READY handshake. A timeout does not prove the owner was merely slow.

Check earlier diagnostics for:

- CAN device lookup/open/configuration failure;
- unsupported bitrate or controller command;
- missing non-blocking TX support;
- filter/status hook failure;
- static Master creation/NMT creation failure;
- generated OD memory pressure or an earlier owner fatal diagnostic.

A known class of bad Master generation is leaving large `CompactSubObj=127/254` Manager ranges un-compacted. That can inflate target object allocation and cause owner initialization to fail before READY. The correction is to regenerate with the repository's `-CompactMaster` path, not simply increase the startup timeout.

## 4. Runtime cannot be destroyed after stop timeout

This is expected by contract. A stop timeout means the caller has not yet observed owner EXIT. Runtime storage must remain allocated. Call `stop()` again from a normal non-owner thread until cleanup completes, then call `destroy()`.

Also ensure one application owner serializes `start()`/`stop()`/`destroy()` for that runtime. The READY/EXIT event bits are not a multi-caller lifecycle lock.

## 5. RX works intermittently after startup

The runtime installs an RX callback, but callback registration does not replay notifications for frames already buffered during startup. The implementation performs an explicit FIFO probe after `io_can_net` starts to cover that window.

If frames are still missing, inspect the actual BSP CAN FIFO, hardware filter configuration, callback delivery, and `rx_batch` behavior. A restrictive `filter_setup` must accept every valid CANopen COB-ID used by the network.

## 6. CAN TX fails or the owner appears to spin

The port requires non-blocking TX. Verify the BSP path really provides the expected non-blocking send operation and queue semantics.

A full RT TX ring is intentionally treated as no-buffer. Do not change it into an immediate repost loop in the owner executor; that can convert backpressure into a busy spin.

## 7. CAN FD length is wrong

Check both layers:

- package/runtime CAN FD enabled;
- `can_fd_len_mode` matches the BSP interpretation of `rt_can_msg.len`.

If the BSP stores raw DLC but the runtime is configured for payload bytes, lengths above eight bytes will be misinterpreted. Do not infer the mode from controller type alone; verify the BSP driver contract.

## 8. CAN status or bus-off reporting looks wrong

The generic mapper only has conservative information. If `rt_can_status.errcode`/`lasterrtype` or counters have controller-specific meanings, provide `status_mapper` and translate them explicitly in owner context.

If status indication is disabled, status sampling depends on RX/timer wakeups, so detection latency is not equivalent to an interrupt-driven status callback.

## 9. SDO post succeeds but the operation fails

A successful SDO post means queue admission succeeded. The authoritative result is the request's terminal classification.

Distinguish:

- protocol SDO abort code;
- local admission/allocation/runtime error;
- protocol timeout;
- explicit application cancellation;
- shutdown/reset cancellation.

Only one application transaction may be active per remote node. NMT boot/configuration and reset transitions can temporarily own or block the predefined SDO connection.

## 10. Manual configuration does nothing during automatic boot

Application concise DCF registered by `lely_rtt_runtime_configure_nmt_dcf()` is manual-only. It is consumed by explicit manual configuration requests, not automatic NMT boot.

For the checked-in Node1 example, the generated application DCF writes `0x1017:00 = 1000`. Use the manual CFG API/MSH path when testing that source.

## 11. Local OD access is rejected

The owner-safe local OD API is deliberately restricted to `0x2000..0x5FFF`. Communication/profile objects outside that range are not exposed through this bridge.

For remote nodes, use Client-SDO. For local communication-object changes, use the dedicated SYNC/PDO/TIME/EMCY controls that have explicit semantics.

## 12. `dcfgen`/`dcf2c` generation fails on Windows

Check in this order:

```powershell
.\tools\dcf2c.exe --help
.\.venv\Scripts\python.exe --version
.\.venv\Scripts\dcfgen.exe --help
```

Then run the repository command from the repository root. Confirm the fixed Python package versions used by the project. A deprecation warning from `pkg_resources` is informational only if the command proceeds normally.

If `dcf2c.exe` is missing, restore it from the trusted project source or build Lely's tool yourself; do not download an unrelated executable with the same name.

## 13. A generated Node1 C file was accidentally linked as the local device

For the Master example, the target local device is `examples/master_node1/master_sdev.c`. `examples/node1/node1_sdev.c` is provenance/reference for the remote fixture and must not replace the local Master OD.

If the runtime starts with the wrong static device, the Master role check is expected to fail rather than silently run as a slave.

## 14. Evidence boundary

Host/source tests and static review can narrow many failures, but BSP behavior is still external to this repository. Mark target build, board execution, CAN timing, and HIL results separately rather than reporting them as implied by source-level checks.
