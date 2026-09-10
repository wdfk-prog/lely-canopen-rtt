# Lely CANopen RT-Thread Documentation Portal {#mainpage}

This site combines the Lely CANopen RT-Thread project manual with generated API reference for the RT-Thread port, checked-in CANopen examples, and the vendored Lely public headers used by the integration.

## Project documentation

### English

- [Documentation index](en/index.md)
- [Quick start](en/quick-start.md)
- [RT-Thread integration](en/rt-thread-integration.md)
- [Configuration](en/configuration.md)
- [Object Dictionary](en/object-dictionary.md)
- [Host generation](en/host-generation.md)
- [Master control plane](en/master-control-plane.md)
- [Upstream maintenance](en/upstream-maintenance.md)
- [Troubleshooting](en/troubleshooting.md)

### 中文

- [文档首页](zh/index.md)
- [快速接入](zh/quick-start.md)
- [RT-Thread 集成](zh/rt-thread-integration.md)
- [配置说明](zh/configuration.md)
- [对象字典](zh/object-dictionary.md)
- [Host 生成工具链](zh/host-generation.md)
- [Master 控制面](zh/master-control-plane.md)
- [Upstream 维护](zh/upstream-maintenance.md)
- [问题排查](zh/troubleshooting.md)

## API reference

- [File list](files.html)
- [Global symbols](globals.html)
- [Data structures](annotated.html)

The API pages are generated from the configured source tree and are not stored in the repository.

## Source layout

- RT-Thread integration and public runtime API: `port/rtthread/`
- Checked-in Master example and static Object Dictionary: `examples/master_node1/`
- Remote Node1 DCF/static reference fixture: `examples/node1/`
- Vendored Lely public headers: `upstream/include/lely/`
