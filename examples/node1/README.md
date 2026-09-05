# Node1 示例与 DCF 来源

`node1.dcf` 是 `lely-canopen-rtt` B4 阶段为 Node-ID 1 手工建立的最小 CANopen 从站测试 DCF，
不是设备厂商文件，也不是 CANopenEditor 自动导出的原始文件。它不在用户最初上传的 B3 项目 ZIP 中，
而是上一轮 B4.0-B4.4 实现时新增的文件。

其来源和设计依据是：

1. 文件结构与基础对象参考 Lely upstream `test/co-nmt-slave.dcf`；B4 当时记录的 GitHub mirror
   观察 ref 为 `620d1858eb8520dbc3dc5e1a7314565becd54199`；对应文件：
   <https://github.com/lely-industries/lely-core/blob/620d1858eb8520dbc3dc5e1a7314565becd54199/test/co-nmt-slave.dcf>。
2. NMT、heartbeat、SSDO、TPDO、Identity 等对象按 CiA 301 通信语义配置；EDS/DCF 文本格式属于
   CiA 306-1。
3. 为当前 B4 smoke test 增加 `0x1200`、`0x1800/0x1A00`、`0x2000`、`0x2001`，并固定 Node-ID 为 1。

它不是 upstream `co-nmt-slave.dcf` 的直接复制。upstream 示例使用 Node-ID 2、启用 LSS，并包含
`0x1F50/0x1F51/0x1F56/0x1F57` program download/control 测试对象；这些不属于当前 Node1 最小闭环，
因此没有保留。

当前 Node1 的关键默认 CAN-ID：

```text
Heartbeat = 0x701
SSDO RX   = 0x601
SSDO TX   = 0x581
TPDO1     = 0x181
```

TPDO1 映射 `0x2001:00` 的 32-bit 值，mapping entry 为 `0x20010020`。`0x2000:00` 当前用于 SDO
读写 smoke；虽然它声明了 `PDOMapping=1`，但当前 DCF 没有 RPDO communication/mapping 对象，不能把它
当作已经完成的 RPDO 测试通道。

完整的 CANopenEditor 创建 DCF、获取/构建 Lely `dcf2c`、生成 `node1_sdev.c` 以及 `.h` 声明方式见：

[DCF、CANopenEditor 与 Lely dcf2c 使用指南](../../docs/DCF_DCF2C_CANOPENEDITOR.md)

重新生成：

```sh
./tools/gen_node1_sdev.sh
```

如果 `dcf2c` 不在 `PATH`：

```sh
DCF2C=/absolute/path/to/dcf2c ./tools/gen_node1_sdev.sh
```

`node1_sdev.c` 是生成文件；`node1_sdev.h` 是项目维护的薄声明头，Lely `dcf2c` 本身不会生成 `.h`。
