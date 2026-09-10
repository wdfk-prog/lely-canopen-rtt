# Lely CANopen RT-Thread Documentation

> 摘要：Manual for RT-Thread integrators using the Lely single-owner runtime, static OD generation, and Master services.

[中文](../zh/index.md)

## Recommended reading

1. [Quick start](quick-start.md) — integrate the package, select the minimum configuration, and start the checked-in Master example.
2. [RT-Thread integration](rt-thread-integration.md) — understand the single-owner boundary, callback lifetime, CAN/time bridge, and shutdown ordering.
3. [Configuration](configuration.md) — map Kconfig options to runtime and Lely feature policy.
4. [Object Dictionary](object-dictionary.md) — distinguish the local Master OD from remote-node descriptions and choose the correct static-OD workflow.
5. [Host generation](host-generation.md) — regenerate `master_sdev.c`, concise DCF application data, and related metadata on the Host.
6. [Master control plane](master-control-plane.md) — use NMT, SDO, local OD, PDO/SYNC, EMCY, TIME, and MSH without crossing the owner boundary.
7. [Upstream maintenance](upstream-maintenance.md) — update the frozen Lely source subset without widening the target source boundary accidentally.
8. [Troubleshooting](troubleshooting.md) — diagnose build selection, startup, CAN, SDO, generation, and lifecycle failures.

## Document map

| Document | Primary question |
|---|---|
| [Quick start](quick-start.md) | What is the shortest path from package integration to a running runtime? |
| [RT-Thread integration](rt-thread-integration.md) | Why must Lely stay in one owner thread and how are callbacks/lifecycle serialized? |
| [Configuration](configuration.md) | Which Kconfig option owns each runtime or CANopen capability? |
| [Object Dictionary](object-dictionary.md) | Which OD belongs to the MCU, which DCF only describes a remote node, and how are they related? |
| [Host generation](host-generation.md) | How are YAML/DCF inputs converted into target-safe static C artifacts? |
| [Master control plane](master-control-plane.md) | How can non-owner application code safely control and observe CANopen Master services? |
| [Upstream maintenance](upstream-maintenance.md) | How is the frozen upstream identity and target source set kept auditable? |
| [Troubleshooting](troubleshooting.md) | Which evidence separates configuration, BSP, protocol, and lifecycle failures? |

## Repository entry pages

- [English README](../../README.md)
- [中文 README](../../README.zh-CN.md)
