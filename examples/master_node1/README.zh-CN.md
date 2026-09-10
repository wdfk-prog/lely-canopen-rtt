# Master + Remote Node1 示例

> 摘要：说明仓库内 local Master OD、remote Node1 策略、PDO/EMCY/SYNC 路径和 RT-Thread 示例使用的 Host 重新生成契约。

[English](README.md)

这个示例体现当前正确的 CANopen 角色：MCU 持有本地 `master_sdev`，`../node1/node1.dcf` 描述 remote slave，只作为 Host generation input。

## 生成链

```text
../node1/node1.dcf
        -> master.yml
        -> dcfgen -r
        -> compact_master_dcf.py
        -> master.dcf
        -> dcf2c --no-strings
        -> master_sdev.c
```

`master_sdev.h` 是项目维护的声明头。target 保持 `LELY_NO_STDIO=1`、`LELY_NO_CO_DCF=1`、`LELY_NO_CO_OBJ_FILE=1`，不会在运行时解析这两个 DCF。

## Checked-in 示例策略

当前生成配置是可运行 fixture，不是产品 policy：

| 项目 | 当前值/行为 |
|---|---|
| Local Master Node-ID | `127`，示例值。 |
| Remote Node1 | 在 network list 中，开启 NMT boot。 |
| Mandatory node | 否。 |
| Reset Communication | 当前 YAML 示例开启。 |
| Automatic NMT Start | 关闭。 |
| Node1 Heartbeat producer | `1000 ms`。 |
| Master Heartbeat consumer timeout | YAML multiplier 得到 `3000 ms`。 |
| Identity check | Product 来自 Node1 DCF；revision/serial 固定为 `1`；vendor/device-type 为零时不作为 check。 |

CAN HIL 或产品使用前必须确认/替换这些值，并重新生成 OD，不要把产品 policy 移到 `runtime.c`。

## PDO 路径

checked-in OD 同时包含两个方向：

```text
Node1 TPDO1 0x181
    -> Master RPDO1 0x1400/0x1600
    -> Master local 0x2000:01

Master local 0x2200:01
    -> Master TPDO1 0x1800/0x1A00
    -> Node1 RPDO1 0x201
    -> Node1 0x2000:00
```

共享 fixture 默认保持 RPDO1/TPDO1 event-driven，这样未启用 SYNC/PDO 时仍保留简单 TPDO-event smoke path。

启用 `PKG_LELY_USING_MASTER_PDO_TX` 后，应用可通过 owner-safe local OD API 更新 mapped value，再调用 `lely_rtt_runtime_tpdo_event(runtime, 1)` 触发 TPDO1。

## SYNC 与同步 PDO

启用 `PKG_LELY_USING_MASTER_SYNC_PDO` 后，同一 static mapping 可以在 runtime 中切换 transmission type，无需 dynamic remapping：

```text
co sync period 1000000
co pdo trans rx 1 1
co pdo trans tx 1 1
co sync status
```

application 可见的 SYNC snapshot/callback 会在本次 SYNC 对应的 synchronous TPDO 与 RPDO 工作都按 owner 顺序完成后发布。

TPDO type 0 支持 event-on-next-SYNC；1..240 为 synchronous；254/255 保留 event-driven。RTR-only/reserved mode 不在当前 bridge 范围。

## EMCY

Node1 使用 EMCY COB-ID `0x081`，Master OD 包含对应 consumer entry。开启 `PKG_LELY_USING_MASTER_EMCY` 后，`co emcy`/`co emcy 1` 读取 bounded owner-published history，不向 MSH/application 暴露 Lely 对象。

## Manual NMT configuration

启用 `PKG_LELY_USING_MASTER_NMT_CFG` 时，checked-in `master_cfg_dcf.c` 包含一条 application concise-DCF write：

```text
0x1017:00 = 1000 ms
```

auto-init 在 `start()` 前注册复制后的 DCF。显式应用使用同样顺序：

```c
lely_rtt_runtime_configure_master(runtime, &master_sdev);
lely_rtt_runtime_configure_nmt_dcf(runtime, 1,
        master_node1_cfg_dcf, master_node1_cfg_dcf_size);
lely_rtt_runtime_start(runtime);
```

这份 application DCF 是 manual-only，automatic NMT boot 不会消费它。

## Windows 重新生成

从 repository root 执行：

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

manual application DCF：

```powershell
.\.venv\Scripts\python.exe .\tools\gen_cfg_dcf.py `
    --yml .\examples\master_node1\master_cfg.yml `
    --node node1 `
    --symbol master_node1_cfg_dcf `
    --basename master_cfg_dcf `
    --out-dir .\examples\master_node1 `
    --expect-entries 1
```

详见[Host 生成工具链](../../docs/zh/host-generation.md)和[Master 控制面](../../docs/zh/master-control-plane.md)。
