# 对象字典模型

> 摘要：区分 MCU 本地静态 Master OD 与 remote node DCF 输入，说明仓库内 Node1 fixture 如何参与生成而不会在 target 上变成本地设备。

[English](../en/object-dictionary.md)

本仓库最重要的对象字典规则是 ownership：MCU 只实例化本地静态 CANopen device description；remote node DCF 始终留在 Host 侧，作为网络配置和生成输入。

## 1. Local 与 remote 的角色

仓库内示例关系为：

```text
examples/node1/node1.dcf
        | 只描述 remote slave
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

`master_sdev` 才是 MCU 本地 Object Dictionary。`node1.dcf` 不会在 MCU 上实例化，也不会在 runtime 中解析。

target policy 明确保持：

```text
LELY_NO_CO_DCF=1
LELY_NO_CO_OBJ_FILE=1
LELY_NO_STDIO=1
```

因此文本 DCF 和 file-backed OD 处理都属于 Host 工具链。

## 2. CANopenEditor 输出与 Lely static OD 不是同一种产物

CANopenEditor 可以为 CANopenNode 协议栈导出 `OD.c`/`OD.h`。这些文件不是 Lely `co_sdev` 格式，不能直接作为本项目 target Object Dictionary。

本仓库使用 CANopenEditor 时，主要把它当作 EDS/DCF 编辑器：

```text
CANopenEditor / vendor EDS
        -> DCF
        -> Lely dcf2c
        -> const struct co_sdev
```

仓库内 `.h` 是项目维护的薄声明头。`dcf2c` 核心职责是生成静态 C device description，并不决定本项目 header 维护方式。

## 3. Node1 fixture

`examples/node1/node1.dcf` 是测试 fixture，不是厂商产品 DCF。它固定 Node-ID 1，并提供当前 Master smoke path 所需的通信对象。

关键默认 CAN-ID：

```text
EMCY      0x081
TPDO1     0x181
RPDO1     0x201
SSDO TX   0x581
SSDO RX   0x601
Heartbeat 0x701
```

两个 manufacturer object 用作简单 PDO/SDO 测试值：

| 对象 | 作用 |
|---|---|
| `0x2000:00` | `UNSIGNED32`，映射到 Node1 RPDO1。 |
| `0x2001:00` | `UNSIGNED32`，映射到 Node1 TPDO1。 |

Node1 默认 PDO 为 event-driven。RPDO1 通过 `0x20000020` 映射 `0x2000:00`；TPDO1 通过 `0x20010020` 映射 `0x2001:00`。

## 4. 生成后的 Master OD 包含什么

`dcfgen -r` 会把 remote PDO 关系映射进 Master 描述。当前示例中，Master application object 为这些 remote PDO 提供本地端点，同时 static Master OD 还包含 NMT manager 信息、Node1 identity expectation、Heartbeat/NMT boot 信息以及 remote EMCY consumer 配置。

这些 checked-in 值是示例策略，不是产品 ABI。Local Master Node-ID、Node1 是否 mandatory、reset policy、identity check、heartbeat multiplier 等都应在产品使用前重新确认。

## 5. 本地 application OD 访问

启用 `PKG_LELY_USING_LOCAL_OD` 后，Application/MSH thread 只允许访问本地 manufacturer-specific 范围：

```text
0x2000..0x5FFF
```

request 通过 owner queue，真正的 `co_dev_t` 访问仍发生在 Lely owner thread。已有 Lely download indication 会被链式保留，因此 Server-SDO 与 RPDO write 不会因为 bridge 而失效；bridge 还会发布最近一次本地 write 的 metadata snapshot，应用无需直接碰 `co_dev_t`。

如果产品需要把 MCU application 的运行时状态直接作为 OD 读值，可额外启用 `PKG_LELY_USING_MASTER_OD_HOOKS`。`lely_rtt_runtime_configure_local_od_upload_ind()` 在 startup 前按 index/sub-index 注册动态 upload hook；Server-SDO、TPDO 和 owner-safe local read 只要走到同一 `co_sub_up_ind()`，都会看到该动态值。注册项在每次 runtime start 时绑定，在 stop 时恢复原 upload indication，并跨 stop/start 保留。

同一选项还提供 `lely_rtt_runtime_configure_local_od_change_ind()`。它在已有 download indication 已接受并提交最终非空写入、且 metadata snapshot 已稳定发布后，在 owner thread 中发送通知。该 notification 不能事后 veto 已提交写入；需要范围/类型/业务写入拒绝时，应继续由 OD/Lely download indication 在 commit 前完成。

两类 callback 都运行在 Lely owner thread，必须 bounded/non-blocking，不能调用会等待 owner completion 的 runtime API。公开 API 不返回 `co_dev_t` 或 `co_sub_t`。

这个 API 不是 generic remote OD API。remote object 仍通过 Client-SDO 访问。

## 6. 静态 mapping 与 runtime reconfiguration 的边界

控制面可以触发已经配置好的 TPDO；开启 SYNC/PDO bridge 后，也可以修改本地 RPDO/TPDO transmission type。但当前不提供 dynamic PDO remapping。

产品需要修改 PDO layout 时，应改 Host 侧 OD/network 配置并重新生成 static artifact，而不是把新的 mapping policy 塞进 RT-Thread runtime。

## 7. 产品化替换

产品通常应替换示例中的 Master/Node DCF/YAML 配置，包括 identity、通信参数、PDO layout、Heartbeat、startup policy 和 manufacturer object，同时保留同一架构原则：

- 产品配置放在 Host；
- MCU 使用生成的 local static `co_sdev`；
- remote DCF/EDS 只作为 Host 输入；
- 除非明确改变架构，否则 target 不解析文本 DCF。

具体生成流程见[Host 生成工具链](host-generation.md)。
