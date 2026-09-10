# Lely CANopen RT-Thread 中文文档

> 摘要：面向 RT-Thread 固件集成者，解释 Lely single-owner runtime、静态对象字典、Host 生成链和可选 CANopen Master 控制面。

[English](../en/index.md)

## 推荐阅读顺序

1. [快速接入](quick-start.md) — 把 package 接入 BSP，选择最小配置并启动仓库内 Master 示例。
2. [RT-Thread 集成](rt-thread-integration.md) — 理解 single-owner 边界、callback 生命周期、CAN/time bridge 与 shutdown 顺序。
3. [配置指南](configuration.md) — 对应 Kconfig、runtime 配置和 Lely feature policy。
4. [对象字典](object-dictionary.md) — 区分 MCU 本地 Master OD 与 remote node DCF，并理解静态 OD 关系。
5. [Host 生成工具链](host-generation.md) — 在 Host 上重建 `master_sdev.c`、application concise DCF 与生成元数据。
6. [Master 控制面](master-control-plane.md) — 在不越过 owner 边界的前提下使用 NMT、SDO、本地 OD、PDO/SYNC、EMCY、TIME 与 MSH。
7. [Upstream 维护](upstream-maintenance.md) — 更新 frozen Lely 子集，同时保持目标源码边界可审计。
8. [问题排查](troubleshooting.md) — 区分构建选择、startup、CAN、SDO、Host 生成和生命周期问题。

## 文档地图

| 文档 | 主要回答的问题 |
|---|---|
| [快速接入](quick-start.md) | 从 package 接入到 runtime 启动，最短路径是什么？ |
| [RT-Thread 集成](rt-thread-integration.md) | 为什么 Lely 必须由单 owner 串行访问，callback 与 lifecycle 如何安全协作？ |
| [配置指南](configuration.md) | 每项 runtime/CANopen 能力分别由哪个 Kconfig 选项控制？ |
| [对象字典](object-dictionary.md) | MCU 本地 OD、remote DCF 和生成产物分别是什么角色？ |
| [Host 生成工具链](host-generation.md) | YAML/DCF 如何变成适合 MCU 的静态 C 产物？ |
| [Master 控制面](master-control-plane.md) | 非 owner 线程如何安全观察和控制 CANopen Master？ |
| [Upstream 维护](upstream-maintenance.md) | 如何保证 frozen upstream 身份和 target source set 可追踪？ |
| [问题排查](troubleshooting.md) | 如何用证据区分配置、BSP、协议和生命周期故障？ |

## 仓库入口

- [中文 README](../../README.zh-CN.md)
- [English README](../../README.md)
