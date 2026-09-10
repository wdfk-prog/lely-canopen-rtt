# Remote Node1 Fixture

> 摘要：说明仓库 remote Node1 测试 DCF 的来源、CANopen 对象、默认 CAN-ID、PDO mapping 与重新生成命令。

[English](README.md)

`node1.dcf` 是本仓库为 Node-ID 1 维护的最小 remote CANopen slave fixture。它不是设备厂商文件，也不是 Master 示例中 MCU 的本地 Object Dictionary。

该 fixture 参考 Lely 测试 DCF 的文件结构，再按本仓库 NMT/Heartbeat/SDO/PDO/EMCY smoke path 做了调整。CANopen 通信对象语义按项目使用的 CiA 301 组织；EDS/DCF 文件本身属于 Host 侧配置格式。

## 关键默认值

```text
Node-ID   = 1
EMCY      = 0x081
TPDO1     = 0x181
RPDO1     = 0x201
SSDO TX   = 0x581
SSDO RX   = 0x601
Heartbeat = 0x701
```

Heartbeat producer period 为 `1000 ms`。

## PDO 测试值

| 对象 | 类型 | 作用 |
|---|---|---|
| `0x2000:00` | `UNSIGNED32` | RPDO/SDO smoke value。 |
| `0x2001:00` | `UNSIGNED32` | TPDO smoke value。 |

RPDO1 通过 mapping entry `0x20000020` 映射 `0x2000:00`；TPDO1 通过 `0x20010020` 映射 `0x2001:00`。checked-in fixture 中二者都保持 event-driven。

在 Master 示例里，Node1 TPDO1 进入 Master RPDO1/local `0x2000:01`；Master local `0x2200:01` 映射到 Master TPDO1，再发送到 Node1 RPDO1。

## 重新生成

从 repository root 执行：

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\examples\node1 `
    -NoHeader `
    -MetaFile node1_sdev.meta
```

也可以直接调用 `dcf2c`：

```powershell
.\tools\dcf2c.exe `
    -o .\examples\node1\node1_sdev.c `
    .\examples\node1\node1.dcf `
    node1_sdev
```

`node1_sdev.c` 是 generated static-device reference，`node1_sdev.h` 是项目维护的薄声明头。在 Master 示例中，不要把 `node1_sdev.c` 链接为 MCU 本地设备；本地设备应是 `../master_node1/master_sdev.c`。

详见[对象字典](../../docs/zh/object-dictionary.md)与[Host 生成工具链](../../docs/zh/host-generation.md)。
