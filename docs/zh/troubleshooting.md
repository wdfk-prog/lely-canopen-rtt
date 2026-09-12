# 问题排查

> 摘要：按可观察边界区分 Host 生成、source selection、runtime startup、CAN driver、lifecycle 与 CANopen Master 控制面故障，避免把二次 timeout 当根因。

[English](../en/troubleshooting.md)

排查时先判断现象属于哪一层，再沿该层能提供的证据向前找首次异常。不要在 owner、Host generation 或 CAN driver 已经提前报错时，把后续 timeout 或 shell error 当成根因。

## 1. `SConscript` 报 upstream source 缺失

需要同时看：

- `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` 是否包含该路径；
- frozen `upstream/` 是否真的包含文件；
- 当前 Kconfig feature 是否要求或应删除该 source。

先执行：

```sh
./tools/check_vendor.sh
```

如果是 upstream update 后新增的目标 source，应同步审查 vendor boundary 与 target source policy。不要用“扫描并编译整个 upstream tree”绕过 allowlist。

## 2. 出现 threads/TLS/atomics 相关 ABI 或 undefined symbol

当前 target policy 是 single-owner，并有意关闭 Lely 内部 thread/atomic backend。如果 target object 仍引用 C11 threads、pthread、compiler TLS helper 或意外 Lely backend，应检查所有 translation unit 是否统一经过 `port/rtthread/include/lely/features.h` 与 `lely_rtt_config.h`。

不要在 generated OD 或 application compile flag 中重新定义冲突 `LELY_NO_*`。影响 ABI 的 feature policy 必须对 Lely 和 public-structure consumer 完全一致。

## 3. `runtime start` timeout

`start_timeout_ms` 只限制 READY handshake，并不证明 owner 只是“启动太慢”。

先检查更早的 diagnostic：

- CAN device find/open/configuration 失败；
- bitrate 或 controller command 不受 BSP 支持；
- non-blocking TX path 缺失；
- filter/status hook 失败；
- static Master create/NMT create 失败；
- generated OD 导致过大内存压力或 owner 先出现 fatal diagnostic。

一种已知坏生成模式是保留巨大 `CompactSubObj=127/254` Manager range，target 展开后造成大量对象分配。正确处理是按仓库 `-CompactMaster` 流程重新生成，而不是单纯增大 startup timeout。

## 4. stop timeout 后不能 destroy runtime

这是 API contract 的预期行为。stop timeout 表示 caller 尚未观察到 owner EXIT，runtime storage 必须继续保留。应从正常 non-owner thread 再次 `stop()`，直到 cleanup 完成，然后再 `destroy()`。

同一个 runtime 的 `start()`/`stop()`/`destroy()` 还必须由应用外部串行化。READY/EXIT event bit 不是 multi-caller lifecycle lock。

## 5. Startup 后 RX 偶尔丢第一批 frame

安装 RX callback 不会补发 startup 阶段已经进入 FIFO 的旧 notification，因此 runtime 在 `io_can_net` 启动后会主动 probe 一次 FIFO。

如果仍然丢帧，应检查真实 BSP CAN FIFO、hardware filter、callback delivery 与 `rx_batch`。只要网络可能使用某个 CANopen COB-ID，restrictive `filter_setup` 就必须允许它。

## 6. CAN TX 失败或 owner 像在 busy-spin

port 需要 non-blocking TX。确认 BSP 实际 driver path 提供了对应 send operation 和 queue 语义。

RT TX ring 满时，当前实现故意返回 no-buffer。不要改成立即 repost 同一个 write 的循环，否则 backpressure 可能变成 single-owner busy spin。

## 7. CAN FD 长度错误

同时检查：

- package/runtime CAN FD 已开启；
- `can_fd_len_mode` 与 BSP 的 `rt_can_msg.len` 语义一致。

如果 BSP 存 raw DLC，而 runtime 配成 payload bytes，8 字节以上长度就会解释错误。不要按 controller 型号猜测，直接核对 BSP driver contract。

## 8. CAN status 或 bus-off 判断异常

通用 mapper 只有保守信息。如果 `rt_can_status.errcode`/`lasterrtype` 或 counter 带有控制器专用含义，应提供 `status_mapper`，在 owner context 中做明确转换。

status indication 关闭时，status sampling 依赖 RX/timer wakeup，因此检测延迟不等于 interrupt-driven status callback。

## 9. SDO post 成功但 transaction 失败

SDO post 成功只代表 queue admission 成功。最终以 request terminal result 为准。

需要区分：

- protocol SDO abort code；
- local admission/allocation/runtime error；
- protocol timeout；
- explicit application cancel；
- shutdown/reset cancel。

每个 remote node 同时最多一个 active application transaction，但可以有最多 `PKG_LELY_MASTER_SDO_QUEUE_DEPTH` 个 request 在该节点 FIFO 中等待。post 成功只证明进入全局 owner queue；如果 owner dispatch 时 per-node FIFO 已满，该 request 会以 `LOCAL_ERROR` 和 `-RT_EBUSY` 结束。NMT boot/configuration 与 reset transition 可能临时占用或阻塞整个节点级 application SDO 边界，包括已选择的 custom CSDO。

## 10. Automatic boot 期间 application DCF 没有生效

`lely_rtt_runtime_configure_nmt_dcf()` 注册的 application concise DCF 明确是 manual-only，只由 explicit manual configuration request 消费，不会自动加入 NMT boot。

当前 Node1 示例中的 application DCF 写 `0x1017:00 = 1000`。验证它时应使用 manual CFG API/MSH path。

## 11. Local OD 访问被拒绝

owner-safe local OD API 故意只开放 `0x2000..0x5FFF`。超出该范围的 communication/profile object 不通过这个 generic bridge 暴露。

remote node 使用 Client-SDO；local communication object 的可控变化使用已经定义明确语义的 SYNC/PDO/TIME/EMCY 接口。

## 12. Windows 上 `dcfgen`/`dcf2c` 生成失败

按顺序确认：

```powershell
.\tools\dcf2c.exe --help
.\.venv\Scripts\python.exe --version
.\.venv\Scripts\dcfgen.exe --help
```

然后从 repository root 执行仓库命令，并核对项目固定 Python dependency version。`pkg_resources` deprecation warning 只要没有阻止命令继续执行，就不是失败本身。

如果 `dcf2c.exe` 丢失，应从可信项目源恢复或自行构建 Lely tool，不要下载来源不明的同名 executable。

## 13. 错把 Node1 generated C 当成本地设备链接

Master 示例的 target local device 是 `examples/master_node1/master_sdev.c`。`examples/node1/node1_sdev.c` 只是 remote fixture 的 provenance/reference，不能替代 local Master OD。

如果 runtime 绑定了错误 static device，Master role check 应失败，而不是静默按 slave 运行。

## 14. 证据边界

Host/source test 与静态审查可以缩小很多问题，但 BSP 行为仍然在仓库之外。真实 target build、目标板执行、CAN 时序与 HIL 结果应单独记录，不能由 source-level check 推导为“已通过”。
