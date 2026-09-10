# Host 生成工具链：DCF、dcfgen、dcf2c 与 CANopenEditor

> 摘要：说明当前 Windows Host 如何把网络 YAML/DCF 输入转换为适合 RT-Thread target 的 compact Lely static SDEV 产物。

[English](../en/host-generation.md)

target 不解析 DCF。文本解析、网络配置展开、Master DCF 裁剪和 C 代码生成全部在 firmware build 之前由 Host 完成。

## 1. 各工具职责

| 工具 | 本仓库中的职责 |
|---|---|
| CANopenEditor | 需要 GUI 时用于编辑/导入 EDS/DCF，并导出具体 DCF。 |
| `dcfgen`（`dcf-tools`） | 从 YAML 与 remote device 描述生成 CANopen Master DCF，也可产生 slave concise DCF。 |
| `tools/compact_master_dcf.py` | 收缩过大的 Manager `CompactSubObj` 范围，并物化受支持的 Master-local 初始化数据。 |
| Lely `dcf2c` | 把 DCF/EDS 转成 C99 `const struct co_sdev` 静态初始化。 |
| `tools/gen_sdev.ps1` | 仓库统一 Windows YAML/DCF -> static SDEV 入口。 |
| `tools/gen_cfg_dcf.py` | 选择并校验 concise DCF，再包装成 manual NMT configuration 使用的 application C/H。 |

CANopenEditor 为 CANopenNode 导出的 `OD.c`/`OD.h` 不属于 Lely target 生成链。

## 2. Windows 环境

仓库自带 Windows x86-64 `tools\dcf2c.exe`，先确认：

```powershell
.\tools\dcf2c.exe --help
py -3 --version
```

当前 Host workflow 固定的 Python 依赖为：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install --force-reinstall `
    "setuptools==81.0.0" `
    "empy==3.3.4" `
    "dcf-tools==2.4.2"
.\.venv\Scripts\dcfgen.exe --help
```

如果 `dcfgen` 只打印 `pkg_resources is deprecated` warning，但后续仍正常输出 help 或生成结果，则该 warning 本身不是失败。

`tools\setup_dcfgen_windows.ps1` 只是可选 convenience wrapper，上面的命令仍是透明、可审查的准备路径。

## 3. 生成 checked-in Master

从仓库根目录执行：

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

Master + Node1 不再有单独 wrapper，统一使用这个通用入口。

几个安全参数不是装饰：

- `-CompactMaster` 在 `dcf2c` 前收缩 `dcfgen` Manager range，避免单 Node 网络在 MCU 上展开大量无用 `co_sub_t`。
- `-ErrorHistoryDepth 8` 给当前 fixture 限制 `0x1003` error history。
- `-MaxMasterSubObjects 256` 在 compact 后仍过大时直接失败。
- `-NoStrings` 去掉可选 OD name string。
- `-NoHeader` 保留项目维护的声明头。
- `-MetaFile` 刷新生成 provenance metadata。

当前 MCU profile 不应把未经 compact 的 `dcfgen` Master DCF 直接交给 target `dcf2c`。

## 4. 为什么需要 Master DCF compaction

Lely 标准 Master template 会生成带 `CompactSubObj=127/254` 的 Manager array。即使网络只有 Node1，展开后仍会在 target 形成大量对象并增加 heap 压力。

compactor 根据最高 remote Node-ID 收缩 node-indexed Manager array，按命令行限制 error history，并在 `dcf2c` 之前把 `dcfgen` 产生的、当前明确支持的 Master-local initialization write 物化进 DCF。

当前 fixture 中包括生成配置所需的 revision/serial 等 Master-local identity 初始化。因为 target 使用 `LELY_NO_CO_OBJ_FILE=1`，所以 `UploadFile`/`DownloadFile` 这类 file-backed OD value 会被拒绝。

生成链还会根据 compact DCF 的 `DefaultValue` 校正 static SDEV default initializer，避免把当前 `ParameterValue` 错当成 reset/default value。

## 5. 重建 Node1 static reference

Node1 static C 仅保留为 provenance/reference，不是 MCU 的本地 Master OD。重建命令：

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

Linux/macOS 应优先使用与 frozen target source 对应 Lely revision 构建的 `dcf2c`。当前 `metadata/UPSTREAM.lock` 尚未证明 exact upstream ref，因此任意系统安装的 `dcf2c` 只能视为未验证路径；若用于探索性重生成，必须审查生成 diff，并把 target build 作为独立验证。

## 6. 生成 application concise DCF

manual NMT configuration 可以在 runtime start 前注册一份复制后的 concise DCF。仓库内 Node1 示例写入 `0x1017:00 = 1000 ms`。

生成 C/H：

```powershell
.\.venv\Scripts\python.exe .\tools\gen_cfg_dcf.py `
    --yml .\examples\master_node1\master_cfg.yml `
    --node node1 `
    --symbol master_node1_cfg_dcf `
    --basename master_cfg_dcf `
    --out-dir .\examples\master_node1 `
    --expect-entries 1
```

CANopen datatype/SDO byte encoding 仍由 `dcfgen` 完成；`gen_cfg_dcf.py` 负责 staging、concise-DCF framing 校验、选择指定 slave blob 和 C array 包装。它不会把这份 application DCF 隐式变成 automatic NMT boot configuration。

## 7. CANopenEditor 路径

产品设备可以按以下方式维护 DCF：

1. 打开厂商 EDS 或新建 CANopenEditor 工程；
2. 配置具体 Node-ID、identity、bitrate capability、Heartbeat、SDO、PDO 和 manufacturer object；
3. 导出 CiA 306-1 Device Configuration File（`.dcf`）；
4. 将 DCF 作为 Lely Host-generation 输入；
5. 重新生成并审查 static C diff。

当前 Node1 fixture 的 manufacturer range 使用 `0x2000:00` 与 `0x2001:00` 作为简单 RPDO/TPDO smoke value。

还有一个与当前 Lely parser 绑定的细节：它使用 DCF section 拼写 `[DeviceComissioning]`。不要只因为英语拼写看起来不正确就改成 `[DeviceCommissioning]`，除非已经核对目标 upstream parser。

## 8. Upstream 身份边界

`metadata/UPSTREAM.lock` 把原始 archive identity 与观察到的 public GitHub mirror commit 分开记录。观察 commit 不代表已经证明 frozen archive bytes 等于该 commit。在受控 Git ref 导入建立 exact identity 前，应以 lock file + vendor manifest 作为当前源码身份证据。

详见[Upstream 维护](upstream-maintenance.md)。
