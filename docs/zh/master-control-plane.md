# CANopen Master 控制面

> 摘要：解释 NMT、SDO、本地 OD、PDO/SYNC、EMCY、TIME 的 owner-safe application API，以及可选 RT-Thread MSH 命令面。

[English](../en/master-control-plane.md)

所有 control-plane 功能都继续服从 single-owner 规则。Application/MSH thread 不会拿到原始 `co_dev_t`、`co_nmt_t`、`co_csdo_t`、PDO、EMCY、SYNC 或 TIME service pointer。

## 1. 通过 snapshot 观察状态

runtime 会发布 non-owner thread 可安全读取的 snapshot：

- local Master NMT state；
- remote node 最近可用 NMT state；
- remote NMT boot 最近一次完成结果；
- local OD write metadata；
- 已处理 SYNC 的 sequence/counter/role/period；
- retained remote EMCY event；
- 最近一次收到的 CANopen TIME。

snapshot API 不进入 Lely。在 owner 尚未发布可用值时，可能返回 not-ready/busy 类结果。

## 2. NMT command ingress

`PKG_LELY_USING_MASTER_COMMAND` 为每个 runtime 创建一个 RT-Thread message queue。`lely_rtt_runtime_post_nmt()` 成功只表示 NMT command 已进入 owner dispatch queue，并不表示 remote node 已经完成状态切换。

因此“queue 成功”和“节点状态已变化”必须分开判断。后者继续读取 remote NMT snapshot。

shutdown 开始时会先关闭 command admission，未执行工作不能越过 teardown 生命周期边界。

## 3. Client-SDO transaction

`PKG_LELY_USING_MASTER_SDO` 提供 request-object 形式的 upload/download，包括 block transfer 和显式 cancel。

request 生命周期：

```text
create request
    -> post upload/download
    -> owner 启动并持有 Lely CSDO operation
    -> wait 或 cancel
    -> 读取 terminal result
    -> destroy request
```

关键契约：

- 每个 remote Node-ID 同时最多一个 active application SDO transaction，并允许最多 `PKG_LELY_MASTER_SDO_QUEUE_DEPTH` 个 request 在该节点的 FIFO 中等待；
- 不同 remote Node-ID 的 application SDO transaction 可以并行 active；
- 默认路径按需创建 application-owned、基于 CiA 301 predefined connection 的 Client-SDO，不借用 NMT boot Client-SDO；
- `lely_rtt_runtime_configure_sdo_channel()` 可在 startup 前为节点选择 `1..128` 的 custom Client-SDO，对应 local Master `0x1280..0x12FF`；传 `0` 保持 predefined path；
- custom Client-SDO 从 Lely NMT service manager 借用，application bridge 不会 stop/destroy 它；
- custom communication parameter 会在 local NMT reset 后验证，并在 queued request 真正变成 active 前再次验证；如果运行期参数漂移，则本地失败而不是使用过期/冲突通道启动传输；
- download payload 在成功 post 返回前完成复制；
- upload result data 由 request 持有，直到 request destroy 成功；
- protocol timeout、SDO abort、local error、application cancel、shutdown cancel 是不同 terminal outcome。

reset/stop、remote Boot-up 与 manual NMT configuration 会在整个节点级 application SDO 边界上执行 cancel/block；predefined 与选中的 custom channel 都受此仲裁，避免 application request 与 NMT boot/configuration 交错。

## 4. Manual NMT configuration

`PKG_LELY_USING_MASTER_NMT_CFG` 把 `co_nmt_cfg_req()` 放到 owner 边界内。有效 manual request 需要受支持的 configuration source，其中包括 startup 前注册的 application concise DCF。

`lely_rtt_runtime_configure_nmt_dcf()` 只能在 startup 前调用，并复制/校验 concise DCF。注册的数据明确是 manual-only：automatic NMT boot 不会消费它。

当前 bridge 拒绝 `0x1F8A` restore/reset path，因为该路径与现有 Master Boot-up/NMT-boot ownership 模型冲突。

## 5. Local OD 与 TPDO

`PKG_LELY_USING_LOCAL_OD` 提供本地 `0x2000..0x5FFF` 对象的同步 owner-dispatched read/write。

`PKG_LELY_USING_MASTER_PDO_TX` 用于触发已经配置好的 static Master TPDO。典型路径：

```text
write mapped local OD value
    -> trigger TPDO event
    -> owner 调用已配置 TPDO service
```

这不是 dynamic PDO remapping。

## 6. SYNC 与同步 PDO

`PKG_LELY_USING_MASTER_SYNC_PDO` 增加：

- owner-safe `0x1006` period 控制；
- 本地 RPDO/TPDO transmission type get/set；
- processed-SYNC snapshot；
- 可选 startup-registered application callback。

transmission type 控制支持同步 `0..240` 和现有 event-driven `254/255`。TPDO type 0 表示 event-on-next-SYNC。RTR-only/reserved mode 与 dynamic PDO remapping 不在当前 bridge 范围。

application SYNC indication 在 Lely 已处理 synchronous TPDO、并提交 synchronous RPDO data 之后发布，所以它代表该周期的 post-PDO boundary。

callback 在 owner thread 中执行，必须 bounded/non-blocking，不能再调用会等待 owner completion 的 runtime API。

## 7. EMCY

`PKG_LELY_USING_MASTER_EMCY` 提供两个方向：

- remote EMCY indication 被复制进 bounded per-runtime history ring；
- local EMCY producer 操作通过 owner dispatch。

history 使用固定 storage + atomic publication，不为每个 event 分配 heap。深度由 `PKG_LELY_MASTER_EMCY_HISTORY_DEPTH` 控制。

当 NMT 生命周期导致 Lely 重建 local EMCY service 时，runtime 会在 owner 中重新绑定 indication。

## 8. TIME

`PKG_LELY_USING_MASTER_TIME` 明确把 CANopen TIME 与 transport clock 分开。收到的 TIME 发布成 snapshot；producer send 使用 caller 显式提供的 absolute time。

runtime 不会把 monotonic `rt_tick_get()` uptime 自动解释成 wall clock，也不会自行启动 periodic TIME producer。

## 9. MSH 命令面

启用 `PKG_LELY_USING_MSH` 后，default auto-init Master 导出一个根命令 `co`：

```text
co status
co node <node-id>
co boot <node-id>
co nmt start|stop|preop|reset-node|reset-comm <node-id|all>
co cfg <node-id> <timeout-ms>
co od status
co od read <index> <subindex> <type>
co od write <index> <subindex> <type> <value>
co tpdo event <pdo-number>
co sync status
co sync period <microseconds>
co pdo trans rx|tx <pdo-number> [0..240|254|255]
co emcy [node-id]
co emcy push <eec> <error-register> [msef-10hex]
co emcy pop|clear
co time status
co time mode off|consumer|producer|both
co time send <unix-sec> <nanoseconds>
co sdo read <node> <index> <subindex> <type> <timeout-ms>
co sdo write <node> <index> <subindex> <type> <value> <timeout-ms>
```

只有对应 Kconfig feature 开启的命令才会出现/可用。MSH 标量 OD/SDO 类型为 `bool|u8|u16|u32|i8|i16|i32`。Block SDO 与显式 cancel 属于 application API，不是 MSH scalar command。

## 10. 当前不提供什么

现 API 有意不暴露 raw Lely Client-SDO ownership、运行期修改 custom `0x1280..0x12FF` communication parameter、通用 dynamic PDO remapping，也没有自动 wall-clock synchronization policy。Custom CSDO 只通过上述 startup-only channel selector 选择；application request active 期间直接操作同一个 raw Lely CSDO 仍不在公开契约内。
