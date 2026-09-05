# DCF、CANopenEditor 与 Lely `dcf2c` 使用指南

本文说明 `lely-canopen-rtt` 中静态对象字典的完整来源和生成链路，重点回答四个问题：

1. `dcf2c` 从哪里获得；
2. 如何用 `dcf2c` 把 EDS/DCF 转成 Lely 可编译的 C 静态设备描述；
3. `.dcf` 从哪里来，以及如何用 CANopenNode/CANopenEditor 创建或导出；
4. `examples/node1/node1.dcf` 是怎样得到的、包含哪些人为设计决策。

> 重要：CANopenEditor 的 **Export CanOpenNode...** 和 Lely 的 `dcf2c` 是两条不同工具链。
> CANopenEditor 可以为 **CANopenNode 协议栈**生成 `OD.c/OD.h`；本项目使用 **Lely**，目标产物是
> `const struct co_sdev`，因此正确路径是“CANopenEditor 导出 `.dcf` → Lely `dcf2c` 生成 `.c`”。

## 1. 本项目采用的对象字典链路

```text
CANopenEditor 工程 / 厂商 EDS
        |
        | 配置远端 Node1
        v
      node1.dcf
        |
        | Host: dcfgen -r + master.yml
        v
   master.full.dcf
        |
        | Host: compact_master_dcf.py
        v
      master.dcf
        |
        | Host: Lely dcf2c --no-strings
        v
     master_sdev.c              master_sdev.h
     (生成目标)                  (项目薄声明头)
        |                              |
        +---------------+--------------+
                        v
                RT-Thread target build
                        |
                        v
              co_dev_create_from_sdev()
                        |
                        v
                   co_nmt_create()
                   (NMT Master)
```

目标 MCU 上不解析 `.dcf`。本项目配置 `LELY_NO_CO_DCF=1`，DCF 解析与 `dcf2c` 都属于 **Host 开发工具链**；
在旧 Node1 从站示例中，目标端曾编译 `node1_sdev.c`。Issue #3 修正主站角色后，
该文件只保留作远端 DCF provenance/reference；Master 示例的目标端编译
`examples/master_node1/master_sdev.c`。

## 2. `dcf2c` 是什么

`dcf2c` 是 Lely CANopen 自带的 EDS/DCF-to-C 工具。官方文档说明它读取 EDS 或 DCF，并生成
`struct co_sdev` 的 C99 静态初始化代码，主要用于没有资源在运行时解析 EDS/DCF 的嵌入式目标。

官方用法：

```text
dcf2c [--no-strings] [-o <file> | --output=<file>] <filename> <variable_name>
```

本项目实际使用：

```sh
dcf2c examples/node1/node1.dcf node1_sdev \
    -o examples/node1/node1_sdev.c
```

其中：

- `examples/node1/node1.dcf`：输入设备配置文件；
- `node1_sdev`：生成的全局变量名；
- `node1_sdev.c`：输出文件。

生成结果的核心形式是：

```c
#include <lely/co/sdev.h>

const struct co_sdev node1_sdev = {
    /* generated object dictionary */
};
```

## 3. Windows 下如何获得并运行 `dcfgen` / `dcf2c`

本项目的 MCU Master 生成链使用 `dcfgen`、项目内置的 DCF 裁剪器和 `dcf2c`：

```text
dcfgen -r master.yml       -> staging master.full.dcf
compact_master_dcf.py      -> master.dcf
dcf2c --no-strings         -> master_sdev.c
```

不要把 `dcfgen` 的完整 `master.dcf` 直接用于 RT-Thread MCU 的 `dcf2c` 输入。Lely 的标准 Master 模板会为多组 CANopen Manager 对象生成 `CompactSubObj=127/254`；vendored `upstream/src/co/dcf.c` 会把 compact entry 逐个展开，而目标运行时 `co_dev_create_from_sdev()` 又会为这些 entry 建立动态对象。对只有 Node1 的网络，这会把本来只需要少量 sub-index 的对象扩成大量 `co_sub_t`，显著增加 heap 压力。

如果旧生成物已经带到目标板，典型故障链会表现为 owner 初始化期间 Lely value/object cleanup assertion、`lelyown` 提前退出，随后 `runtime READY wait failed`。READY timeout 是 owner 已经异常退出后的二次现象，不应通过单纯增大 `start_timeout_ms` 掩盖。更新生成工具后重新生成/替换 `master_sdev.c` 才是正确处理。

Windows Host 统一只使用通用原生 PowerShell 入口：

```text
tools\gen_sdev.ps1
```

项目不再保留示例专用生成包装器。Master + Node1 也通过同一个通用脚本传参生成，避免以后新增网络配置时继续复制专用脚本。

### 3.1 Windows：推荐做法

#### 第 1 步：确认项目自带的 `dcf2c.exe`

本项目 ZIP 已经包含：

```text
tools\dcf2c.exe
```

它是 Windows x86-64 控制台程序，所以正常情况下**不需要另外下载 `dcf2c`**。在项目根目录打开
PowerShell，先检查：

```powershell
.\tools\dcf2c.exe --help
```

如果这里能看到 Lely `dcf2c` 的帮助信息，就直接使用这个文件。

如果 `tools\dcf2c.exe` 丢失，不建议随便从第三方网站下载同名 EXE。优先从本项目原始 ZIP 恢复；
也可以按 Lely 官方源码/Windows Cygwin 构建说明自己构建。Lely 官方 `dcf2c` 文档：

```text
https://opensource.lely.com/canopen/docs/dcf2c/
```

Lely 官方安装/Windows 说明：

```text
https://opensource.lely.com/canopen/docs/installation/
```

#### 第 2 步：安装 Python 3

`dcfgen` 不是 `dcf2c.exe` 里的子命令，它来自 Lely 的 Python 包 `dcf-tools`。

Windows Python 下载页：

```text
https://www.python.org/downloads/windows/
```

安装时建议启用 Python Launcher（`py.exe`）。安装后在 PowerShell 检查：

```powershell
py -3 --version
```

如果你的环境没有 `py`，但有 `python`，下面命令中的 `py -3` 可以替换成 `python`。

#### 第 3 步：按已验证版本创建 `.venv` 并安装 `dcfgen`

PyPI 上的包名是 `dcf-tools`：

```text
https://pypi.org/project/dcf-tools/
```

本项目不要直接执行 `pip install dcf-tools` 后让依赖自动漂移。已经在 Windows PowerShell 实机跑通的依赖组合是：

```text
dcf-tools==2.4.2
empy==3.3.4
setuptools==81.0.0
```

在项目根目录依次执行：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install --force-reinstall `
    "setuptools==81.0.0" `
    "empy==3.3.4" `
    "dcf-tools==2.4.2"
```

然后必须直接执行 `dcfgen` 自检：

```powershell
.\.venv\Scripts\dcfgen.exe --help
```

只要能看到类似下面的 usage，说明 `dcfgen` 已经可以工作：

```text
usage: dcfgen [-h] [-d DIR] [-r] [-S] [-v] filename
```

当前这套旧版 `dcf-tools` 可能同时打印：

```text
UserWarning: pkg_resources is deprecated as an API
```

这是 warning，不是本项目的失败判据。只要 `dcfgen --help` 后仍正常打印 usage，或者生成命令继续执行，就不要因为这个 warning 再升级/降级依赖。

如果看到下面这种真正的错误：

```text
ModuleNotFoundError: No module named 'pkg_resources'
```

说明当前 `.venv` 里的依赖不是上述固定组合。直接重新执行带 `--force-reinstall` 的三包安装命令即可，不需要删除整个项目。

仓库还提供可选辅助脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\setup_dcfgen_windows.ps1
```

它只是把“创建 `.venv` + 安装固定依赖 + 执行 `dcfgen --help`”封装起来。主流程仍以上面的手工命令为准；即使不运行这个 setup 脚本，也可以正常生成。

Lely 的 `dcfgen` 用于根据 YAML 生成 CANopen Master DCF；本项目使用 `-r` 生成 remote PDO mapping：

```text
https://opensource.lely.com/canopen/docs/dcf-tools/
```
#### 第 4 步：使用通用 `gen_sdev.ps1`

Windows 主入口现在是：

```text
tools\gen_sdev.ps1
```

这个脚本不再绑定 `master_node1`，而是通过参数选择输入、C 符号和输出目录。它有两种模式。

**模式 A：YAML -> MCU-safe Master DCF -> C/H**

```powershell
.\tools\gen_sdev.ps1 `
    -Yml .\examples\master_node1\master.yml `
    -Name master_sdev `
    -OutDir .\generated\master_node1 `
    -DcfFileName master.dcf `
    -RemotePdo `
    -CompactMaster `
    -NoStrings `
    -MetaFile master_sdev.meta
```

这里：

- `-Yml`：选择任意 `dcfgen` Master YAML；
- `-Name`：传给 `dcf2c` 的 C 变量名，同时作为默认 `.c/.h` 文件基名；必须是合法 C 标识符；
- `-OutDir`：输出目录，不存在时自动创建；
- `-DcfFileName`：YAML 模式下保存生成 DCF 的文件名；省略时默认为 `<Name>.dcf`；
- `-RemotePdo`：向 `dcfgen` 传递 `-r`；
- `-CompactMaster`：对 `dcfgen` 的 Master DCF 做 MCU 内存裁剪；Master target 推荐始终启用；
- `-ErrorHistoryDepth`：`0x1003` error history 保留深度，默认 `8`；
- `-MaxMasterSubObjects`：裁剪后估算 sub-object 总数上限，默认 `256`；超过时停止生成，避免再次把明显过大的 OD 带到 MCU；
- `-NoStrings`：向 `dcf2c` 传递 `--no-strings`，不把可选对象/子对象名称复制到目标运行时 heap；
- `-MetaFile`：可选，在 `-OutDir` 下生成一个 metadata 文件，记录输入、DCF、SDEV 的 SHA-256 和关键生成选项；
- `-NoStrict`：向 `dcfgen` 传递 `-S`；
- `-VerboseDcfGen`：向 `dcfgen` 传递 `-v`。

上面的命令会得到：

```text
generated\master_node1\master.dcf
generated\master_node1\master_sdev.c
generated\master_node1\master_sdev.h
generated\master_node1\master_sdev.meta
```

`dcfgen` 本身固定生成名为 `master.dcf` 的完整 Master DCF。通用脚本先在 staging 目录接收该文件；启用 `-CompactMaster` 时，随后调用 `tools\compact_master_dcf.py` 把 `CompactSubObj` 的大范围收缩到当前网络实际需要的范围，再把裁剪结果按 `-DcfFileName` 发布到目标目录。原始完整 DCF 只存在于 staging 目录，成功/失败后都会清理。脚本在 staging 中生成一份临时 YAML，把其中每个 `dcf:` 相对路径先按所选 YAML 所在目录解析为绝对路径，再交给 `dcfgen`。因此类似 `../node1/node1.dcf` 的输入不会依赖用户启动 PowerShell 或 Python 子进程的当前工作目录；仓库内原始 YAML 不会被改写。

当前裁剪规则只改变 Host 生成 DCF 的 compact 展开规模，不修改 `master.yml` 中的产品策略：Node-ID、heartbeat multiplier、mandatory、自动 NMT Start/Reset Communication 等仍由 YAML 决定。对于 Node-ID 索引的 Manager 对象，裁剪器保留到当前 `0x1F81` 中最高配置的远端 Node-ID；`0x1003` 单独使用 `-ErrorHistoryDepth`。如果裁剪后估算 sub-object 数仍超过 `-MaxMasterSubObjects`，生成流程 fail closed，不发布新的 `master.dcf/master_sdev.c`。

最终发布同样 fail closed：`gen_sdev.ps1` 会在替换第一个正式产物前先备份本次所有目标文件，随后发布 C/H/DCF/META；任一替换失败都会恢复整组旧文件，避免出现“新 C + 旧 DCF/META”的混合 generation。META 中的输入/DCF/SDEV SHA-256 统一按文本换行归一化为 LF 后计算，并写入 `HASH_MODE=LF_NORMALIZED_TEXT`，所以 Git 的 CRLF/LF 转换不会改变这些 provenance hash。

**模式 B：已有 DCF -> C/H**

如果已经有 DCF，不需要再经过 `dcfgen`：

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\generated\node1
```

输出：

```text
generated\node1\node1_sdev.c
generated\node1\node1_sdev.h
```

DCF 模式默认不复制输入 DCF；如果希望同时复制到输出目录，可额外指定输出文件名：

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\generated\node1 `
    -DcfFileName node1.dcf
```

两种模式都可以显式指定工具：

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\device.dcf `
    -Name device_sdev `
    -OutDir .\generated\device `
    -Dcf2C "C:\path\to\dcf2c.exe"
```

YAML 模式还可以增加：

```text
-DcfGen <path-to-dcfgen.exe>
```

默认查找顺序仍是：

```text
dcfgen -> .venv\Scripts\dcfgen.exe（优先）或 PATH 中的 dcfgen.exe
dcf2c  -> tools\dcf2c.exe（优先）或 PATH 中的 dcf2c.exe
```

默认会生成声明头 `<Name>.h`，内容是 `extern const struct co_sdev <Name>;`。如果工程已经维护自己的声明头，
使用 `-NoHeader`，只生成 `.c`。

如果当前 PowerShell ExecutionPolicy 不允许直接运行 `.ps1`，可使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\generated\node1
```

#### 第 5 步：直接使用通用脚本刷新 Master + Node1 示例

Master + Node1 不再有专用 wrapper。要刷新仓库内示例，直接在项目根目录执行：

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

这条命令就是 Master + Node1 示例的唯一生成入口：

- `master.dcf`：由 `dcfgen -r` 生成后先经过 MCU-safe 裁剪；
- `master_sdev.c`：由裁剪后的 DCF 经 `dcf2c --no-strings` 生成；
- `master_sdev.meta`：由通用脚本记录生成输入/输出 hash 和关键选项；hash 使用 `HASH_MODE=LF_NORMALIZED_TEXT`，因此 CRLF/LF checkout 不会改变 provenance；
- `master_sdev.h`：因为指定 `-NoHeader`，继续使用仓库内项目维护版本，不被覆盖。

正常输出中必须先看到 `Master DCF footprint estimate:`，再看到 `Generated C/DCF/META`。没有 footprint 行时不要继续拿生成物做 MCU build。

#### 第 6 步：确认 Master + Node1 示例结果

```powershell
Get-Item .\examples\master_node1\master.dcf
Get-Item .\examples\master_node1\master_sdev.c
Get-Content .\examples\master_node1\master_sdev.meta
Select-String -Path .\examples\master_node1\master_sdev.c -Pattern "const struct co_sdev master_sdev"
```

如果项目本身在 Git 仓库中，再检查生成 diff：

```powershell
git diff -- examples/master_node1/master.dcf examples/master_node1/master_sdev.c examples/master_node1/master_sdev.meta
```

### 3.3 其他 Host 平台

项目已经删除 Master+Node1 专用 `.sh` wrapper，避免同一生成策略在 PowerShell 和 POSIX Shell 中维护两份。当前仓库的权威通用入口是 Windows PowerShell 的 `tools\gen_sdev.ps1`。

Linux/macOS 如需 Host 生成，可直接使用 Lely 自身的 `dcfgen`、本仓库 `tools/compact_master_dcf.py` 和 `dcf2c` 组合；但不要重新引入示例专用 wrapper。无论在哪个平台，MCU Master 都必须保留 `dcfgen -> compact_master_dcf.py -> dcf2c --no-strings` 这条语义链。

### 3.4 Ubuntu：使用 Lely 官方 PPA

Lely 官方安装文档给出的 Debian/Ubuntu 安装方式是：

```sh
sudo add-apt-repository ppa:lely/ppa
sudo apt-get update
sudo apt-get install liblely-coapp-dev liblely-co-tools python3-dcf-tools
```

其中 CANopen 命令行工具由 Lely 的 tools 构建；`dcf2c` 在 upstream `tools/Makefile.am` 中属于安装的
`bin_PROGRAMS`。安装完成后先验证：

```sh
command -v dcf2c
dcf2c --help
```

对本仓库，优先建议使用与 vendored Lely 版本匹配的 `dcf2c`。PPA 更适合快速试用；若后续已经解析出
`metadata/UPSTREAM.lock` 的精确 upstream tag/commit，则应从那个版本构建 Host `dcf2c`，保证工具与目标头文件/结构定义同源。

### 3.5 从 Lely 源码构建

Lely 官方源仓库：

```text
https://gitlab.com/lely_industries/lely-core.git
```

官方 GitHub 镜像：

```text
https://github.com/lely-industries/lely-core.git
```

Lely 官方 Windows 文档目前主要描述 Cygwin 构建；原生 Visual Studio 流程官方没有完整文档。因此本项目
Windows 使用场景优先采用 ZIP 内已经提供的 `tools\dcf2c.exe`，而不是要求每个使用者先自行构建 Lely。

Linux/Cygwin 的典型源码构建流程是：

```sh
git clone https://gitlab.com/lely_industries/lely-core.git
cd lely-core

autoreconf -i
mkdir -p build
cd build
../configure --disable-cython
make -j"$(nproc)"
```

构建成功后，out-of-tree build 的工具通常位于：

```text
build/tools/dcf2c
```

Host 工具构建时不能关闭 stdio、DCF parser 或 static-device 支持，因为 `dcf2c` 的构建条件是：

```text
!NO_STDIO
&& !NO_CO_DCF
&& !NO_CO_SDEV
```

这与 MCU target 配置并不冲突：

- Host：需要 DCF parser，因为 `dcf2c` 要解析 `.dcf`；
- RT-Thread target：保持 `LELY_NO_CO_DCF=1`，只使用生成好的 static `co_sdev`。

### 3.5 当前仓库 upstream 版本状态

当前 `metadata/UPSTREAM.lock` 仍记录：

```text
BASELINE_EXACT_UPSTREAM_REF=UNRESOLVED
CURRENT_UPSTREAM_REF=UNRESOLVED
OBSERVED_GITHUB_MIRROR_MASTER=620d1858eb8520dbc3dc5e1a7314565becd54199
```

所以 `620d1858...` 只是本项目 B0 阶段观察到的 GitHub mirror 参考点，不应被描述成已经证明的原始 ZIP 精确版本。

在 exact upstream ref 仍未解析的情况下：

1. Windows 开发优先使用本项目自带 `tools\dcf2c.exe`；
2. `dcfgen` 使用项目 `.venv` 内固定的 `dcf-tools==2.4.2`、`EmPy==3.3.4` 和 `setuptools==81.0.0`；
3. 重新生成后先检查 `master.dcf`/`master_sdev.c` diff；
4. 一旦 `UPSTREAM.lock` 的 exact ref 被确定，再从 exact ref 重新生成并复核一次。

## 4. `dcf2c` 怎么生成 `.c` 和 `.h`

这里需要纠正一个容易混淆的点：**Lely `dcf2c` 本身只输出 C 源文件/标准输出，不生成配套 `.h`。**

### 4.1 生成 `.c`

直接执行：

```sh
dcf2c examples/node1/node1.dcf node1_sdev \
    -o examples/node1/node1_sdev.c
```

本仓库已经封装成：

```sh
./tools/gen_node1_sdev.sh
```

如果 `dcf2c` 不在 `PATH`：

```sh
DCF2C=/absolute/path/to/dcf2c ./tools/gen_node1_sdev.sh
```

脚本先写临时文件，`dcf2c` 成功后再替换正式 `node1_sdev.c`，避免生成失败把已有可用文件截断。

### 4.2 `.h` 是项目自己提供的薄声明头

本项目的 `examples/node1/node1_sdev.h` 不是 `dcf2c` 生成物。它只负责导出生成变量的声明：

```c
#ifndef LELY_RTT_EXAMPLE_NODE1_SDEV_H_
#define LELY_RTT_EXAMPLE_NODE1_SDEV_H_

#include <lely/co/sdev.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct co_sdev node1_sdev;

#ifdef __cplusplus
}
#endif

#endif
```

`.c` 与 `.h` 的变量名必须一致：

```text
dcf2c ... node1_sdev ...
             ^^^^^^^^^

extern const struct co_sdev node1_sdev;
                                  ^^^^^^^^^
```

如果以后改成 `motor_sdev`，必须同步修改 header 声明和引用方。

### 4.3 不要把 CANopenEditor 的 `OD.c/OD.h` 当成这里的输出

CANopenEditor 的 `CANOPENNODE_V4` exporter 会生成适用于 **CANopenNode v4** 的对象字典文件对 `.c/.h`。
官方仓库把这种格式明确列为 “CANopenNode Object Dictionary file pairs”。

它和 Lely `dcf2c` 的输出模型不同：

| 工具 | 输入 | 输出 | 面向协议栈 |
| --- | --- | --- | --- |
| CANopenEditor `CANOPENNODE_V4` | 工程/XDD/EDS 等 | `OD.c` + `OD.h` | CANopenNode |
| Lely `dcf2c` | EDS/DCF | `const struct co_sdev` C 源码 | Lely CANopen |

因此本项目使用 CANopenEditor 时，主要把它当作 **EDS/DCF 对象字典编辑器**，最终导出 `.dcf` 给 Lely `dcf2c`。

## 5. `.dcf` 是什么，通常从哪里来

DCF 是 Device Configuration File。CANopenEditor 当前文档把 EDS/DCF 归类为 CiA 306-1 格式：

- EDS：描述一种设备支持哪些对象以及默认能力；
- DCF：描述一个具体设备/节点实际采用的配置值。

常见来源有三类。

### 5.1 从设备厂商 EDS 得到 DCF

实际项目最常见：

```text
设备厂商提供 .eds
    -> CANopenEditor 打开
    -> 设置具体 Node-ID / bitrate / PDO / heartbeat / Actual Value
    -> Export DeviceConfigurationFile
    -> xxx.dcf
```

如果你正在为已有商业从站生成 Lely static device，这通常比从零手写 DCF 更可靠。

### 5.2 用 CANopenEditor 从零创建

适合本项目的自研从站。

### 5.3 手写 DCF

EDS/DCF 本质是 INI 风格文本，所以可以手写；Lely 自己的 test 目录也包含手写测试 DCF。

但产品对象多以后不建议长期手写，原因包括：

- 容易漏 Mandatory/Optional/Manufacturer object 列表；
- SDO/PDO 的子索引、数据类型和 access 容易不一致；
- Node-ID 变化后 COB-ID 容易漏改；
- DCF 文件格式属于 CiA 306-1，不只是 CiA 301 对象字典语义。

另外一个容易“修错”的细节：本版本 Lely parser 查找的 section 名就是：

```text
[DeviceComissioning]
```

代码中也是字符串 `"DeviceComissioning"`。不要自行改成看起来更符合英语拼写的
`[DeviceCommissioning]`，否则当前 Lely parser 不会按预期读取 Node-ID 等字段。

## 6. 下载和启动 CANopenNode/CANopenEditor

官方仓库：

```text
https://github.com/CANopenNode/CANopenEditor
```

官方 Releases：

```text
https://github.com/CANopenNode/CANopenEditor/releases
```

CANopenEditor README 对普通用户的建议是下载 **latest release 的 binary zip**，不要下载 Source code 压缩包。
本次文档核查日期为 2026-09-04，GitHub `releases/latest` 返回的版本是 `v4.2.3`，asset 名为：

```text
CANopenEditor-v4.2.3-binary.zip
```

官方当前 Windows 使用流程是：

1. 下载 release binary zip；
2. 解压；
3. 进入 `net8.0-windows` 目录；
4. 运行其中的 GUI `.exe`。

以后如果 release 版本更新，优先按 Releases 页面和对应 README，而不是固定下载本文写的版本号。

## 7. 用 CANopenEditor 创建一个新的从站 DCF

下面按 CANopenEditor 的典型 GUI 流程操作。不同版本菜单文字可能有轻微差异，但 exporter 名称和文件格式以实际版本为准。

### 7.1 新建工程

```text
File -> New
```

不要一开始手工逐个创建全部 CiA 301 通信对象。

### 7.2 插入 DS301 profile

```text
Insert Profile -> DS301_profile.xpd
```

`DS301_profile.xpd` 提供 CANopen 通信对象的 profile 模板。普通从站不要插入 NMT Master profile。

### 7.3 填 Device Info

至少明确：

- Product name；
- Product ID / product code；
- Vendor name；
- Vendor ID；
- Revision；
- 实际支持的 CAN bit rates；
- 是否真的支持 LSS。

学习节点可以使用测试 Vendor ID，但产品设备应使用真实分配的身份值。

### 7.4 配置具体 Node

在 Device commissioning 页面配置：

```text
Concrete node ID = 1
Node name        = node1
```

需要生成具体 DCF 时，Node-ID 不应只停留在“通用 EDS”的未配置状态。

### 7.5 启用最小通信对象

Node1 最小闭环至少需要：

```text
0x1000 Device type
0x1001 Error register
0x1017 Producer heartbeat time
0x1018 Identity object
0x1200 Server SDO 1 parameter
0x1F80 NMT startup
```

若还要测试 TPDO，再加入：

```text
0x1800 TPDO1 communication parameter
0x1A00 TPDO1 mapping parameter
```

### 7.6 添加厂商对象

CANopenEditor 中厂商自定义对象放在 `0x2000~0x5FFF`。

本示例使用：

```text
0x2000:00  UNSIGNED32  rw  SDO smoke value (future RPDO candidate)
0x2001:00  UNSIGNED32  rw  TPDO test value
```

`0x2001:00` 允许 PDO mapping，并被 TPDO1 映射。`0x2000:00` 虽然同样声明 `PDOMapping=1`，
但当前 Node1 DCF 没有 `0x1400/0x1600` RPDO communication/mapping 对象，所以 B4.2 只把它当作 SDO
读写 smoke 对象；RPDO 要在后续阶段显式增加通道和 mapping 后才能验证。

### 7.7 导出 DCF

执行：

```text
File -> Export...
```

在 exporter/format 中选择：

```text
DeviceConfigurationFile (.dcf)
```

CANopenEditor 官方格式列表中，该 exporter 对应 CiA 306-1 Device Configuration File。

推荐先保存工程本身（例如 `.xdd`），再单独导出 `.dcf`，这样以后能回到 GUI 修改，而不是只维护导出的文本文件。

## 8. 如果还需要 CANopenNode 自己的 `OD.c/OD.h`

这一步只针对另一个协议栈 CANopenNode，不是 Lely target 所需。

先设置：

```text
Tools -> Preferences -> Selected exporter -> CANOPENNODE_V4
```

再执行：

```text
File -> Export CanOpenNode...
```

会生成 CANopenNode 的 `.c/.h` 对象字典文件对。

对于 `lely-canopen-rtt`，不把这些文件加入构建。仍然回到：

```text
CANopenEditor -> remote .dcf -> dcfgen master.dcf -> Lely dcf2c -> master_sdev.c
```

## 9. 如何在 CANopenEditor 中重建本项目的 Node1

`examples/node1/node1.dcf` 当前语义如下。

### 9.1 设备级参数

| 项目 | 当前 Node1 值 |
| --- | --- |
| Node-ID | `0x01` |
| Node name | `node1` |
| Vendor name | `Lely RT-Thread Example` |
| Vendor ID | `0x00000000`（学习示例值） |
| Product name | `Node1 smoke slave` |
| Product code | `0x00000001` |
| Revision | `0x00000001` |
| LSS | disabled |
| Producer heartbeat | `1000 ms` |

### 9.2 通信对象

| Index | 关键值 | 用途 |
| --- | --- | --- |
| `0x1000` | `UNSIGNED32`, `ro`, default `0` | Device type |
| `0x1001` | `UNSIGNED8`, `ro`, default `0` | Error register |
| `0x1017` | `UNSIGNED16`, `rw`, `1000` | Heartbeat producer |
| `0x1018` | vendor/product/revision/serial | Identity |
| `0x1200:01` | `0x601` | Client → Node1 SSDO |
| `0x1200:02` | `0x581` | Node1 SSDO → Client |
| `0x1800:01` | `0x181` | TPDO1 COB-ID |
| `0x1800:02` | `255` | Event-driven TPDO |
| `0x1A00:00` | `1` | 1 mapped object |
| `0x1A00:01` | `0x20010020` | map `0x2001:00`, 32 bit |
| `0x1F80` | `0x00000004` | NMT startup |

### 9.3 厂商对象

| Index | 类型 | Access | PDO mapping | 用途 |
| --- | --- | --- | --- | --- |
| `0x2000:00` | `UNSIGNED32` | `rw` | yes | 当前用于 SDO smoke；可作为后续 RPDO 候选 |
| `0x2001:00` | `UNSIGNED32` | `rw` | yes | TPDO event smoke value |

配置完后导出为：

```text
examples/node1/node1.dcf
```

再执行：

```sh
./tools/gen_node1_sdev.sh
```

## 10. `examples/node1/node1.dcf` 到底从哪里来的

这个文件**不是从设备厂商下载的，也不是 CANopenEditor 自动导出的原始文件**。它也不在用户最初上传的
B3 项目 ZIP 中；它是上一轮 B4.0-B4.4 实现时新增的文件。

它是在 B4 Node1 实现阶段为 `lely-canopen-rtt` 手工整理出的测试 DCF，目标是最小覆盖：

```text
Boot-up + Heartbeat + NMT slave + SSDO + 一个 event-driven TPDO
```

它的主要参考来源有三部分。

### 10.1 Lely upstream `test/co-nmt-slave.dcf`

B4 实现时参考了 Lely upstream 自带的：

```text
test/co-nmt-slave.dcf
```

本项目当时记录的 GitHub mirror 观察 ref 是：

```text
620d1858eb8520dbc3dc5e1a7314565becd54199
```

对应 upstream 文件可直接查看：

<https://github.com/lely-industries/lely-core/blob/620d1858eb8520dbc3dc5e1a7314565becd54199/test/co-nmt-slave.dcf>

这个 upstream 测试 DCF 提供了可被 Lely parser 接受的文件组织形式，以及这些基础对象的示例：

```text
0x1000
0x1001
0x1017
0x1018
0x1F80
```

但当前 Node1 **不是它的直接复制**。

upstream test 文件原本使用 Node-ID `0x02`、LSS，并包含 `0x1F50/0x1F51/0x1F56/0x1F57`
program download/control 相关测试对象；这些并不是本 B4 smoke slave 的目标，所以没有保留。

### 10.2 CiA 301 通信语义

Node1 所需的 NMT、heartbeat、SSDO、TPDO、Identity 等对象语义按 CANopen/CiA 301 约束组织。

需要区分：

- CiA 301 负责 CANopen 通信对象和协议语义；
- EDS/DCF 的文件格式本身属于 CiA 306-1。

### 10.3 本项目 B4 测试需求

为了验证当前 port，不是做通用产品 DCF，因此又加入了本项目专用对象：

```text
0x1200          SSDO server
0x1800/0x1A00  TPDO1
0x2000          SDO smoke value (future RPDO candidate)
0x2001          TPDO smoke value
```

并把 Node-ID 固定为 `1`，因此默认预定义连接得到：

```text
Heartbeat = 0x701
SSDO RX   = 0x601
SSDO TX   = 0x581
TPDO1     = 0x181
```

所以更准确的 provenance 描述是：

> `node1.dcf` 是本项目 B4 阶段手工建立的、面向 Node-ID 1 的最小从站测试 DCF；
> 文件结构和基础对象参考 Lely upstream `test/co-nmt-slave.dcf`，协议对象按 CiA 301 语义整理，
> SSDO/TPDO/0x2000/0x2001 则由本项目 smoke-test 需求定义。

## 11. 生成后的验证建议

### 11.1 Windows / PowerShell（本项目推荐）

先确认两个 Host 工具都能运行：

```powershell
.\.venv\Scripts\dcfgen.exe --help
.\tools\dcf2c.exe --help
```

先验证通用 DCF 模式（输出到临时/开发目录）：

```powershell
.\tools\gen_sdev.ps1 `
    -Dcf .\examples\node1\node1.dcf `
    -Name node1_sdev `
    -OutDir .\generated\node1
```

需要刷新仓库内 Master + Node1 示例时，直接执行通用脚本：

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

正常输出中应先看到 `Master DCF footprint estimate: ... sub-objects <before>-><after>`，然后才出现 `Generated ...master_sdev.c`。如果没有 footprint 行，说明没有走 MCU-safe Master 裁剪链，不要继续拿生成物做 MCU build。

确认生成变量：

```powershell
Select-String -Path .\examples\master_node1\master_sdev.c `
    -Pattern "const struct co_sdev master_sdev"
```

确认元数据已经更新：

```powershell
Get-Content .\examples\master_node1\master_sdev.meta
```

如果项目在 Git 仓库中，建议检查：

```powershell
git diff -- examples/master_node1/master.dcf examples/master_node1/master_sdev.c examples/master_node1/master_sdev.meta
```

### 11.2 Linux/macOS

仓库不再提供 Master+Node1 专用 POSIX wrapper。Linux/macOS 若需要 Host 生成，按 3.3 节直接组合 `dcfgen`、`tools/compact_master_dcf.py` 和 `dcf2c`；不要绕过裁剪步骤，也不要把标准 dcfgen 的完整 Master DCF 直接用于 MCU。Host 生成成功仍不能替代目标 BSP 编译和 CAN HIL，Boot-up、Heartbeat、SDO、NMT、PDO 仍需目标板验证。

## 12. 官方资料

- Lely CANopen Installation: <https://opensource.lely.com/canopen/docs/installation/>
- Lely `dcf2c`: <https://opensource.lely.com/canopen/docs/dcf2c/>
- Lely core primary repository: <https://gitlab.com/lely_industries/lely-core>
- Lely core GitHub mirror: <https://github.com/lely-industries/lely-core>
- CANopenNode/CANopenEditor: <https://github.com/CANopenNode/CANopenEditor>
- CANopenEditor Releases: <https://github.com/CANopenNode/CANopenEditor/releases>
- CANopenNode: <https://github.com/CANopenNode/CANopenNode>

本项目自身版本/provenance 以 `metadata/UPSTREAM.lock` 和 `examples/node1/` 下的文件为准；外部网页的 latest
版本只作为工具获取入口，不替代仓库内冻结版本信息。


## B4 Master + remote Node1 generation chain

Issue #3 changes the target role: `examples/node1/node1.dcf` describes a remote
slave and must not be instantiated as the MCU local `co_dev_t`. The Host chain
is now:

```text
examples/node1/node1.dcf
  -> examples/master_node1/master.yml
  -> dcfgen -r -> staging full Master DCF
  -> tools/compact_master_dcf.py -> examples/master_node1/master.dcf
  -> dcf2c --no-strings -> examples/master_node1/master_sdev.c
```

On Windows, `tools\gen_sdev.ps1` is the single generic entry point for either YAML -> DCF -> C/H or direct DCF -> C/H conversion. The checked-in Master+Node1 example calls the same script with `-CompactMaster -NoStrings -NoHeader -MetaFile master_sdev.meta`; no example-specific PowerShell or POSIX wrapper remains. The generic script prefers `.venv\Scripts\dcfgen.exe` (when YAML input is used) and the project-bundled `tools\dcf2c.exe`. The generated translation unit must be compiled
through the package `SConscript`, so it sees the same `lely/features.h` overlay
as the rest of the target and therefore the same conditional
`co_sdev/co_sobj/co_ssub` layout. The target still does not parse `.dcf` files at
runtime.

Parameters such as the product Master Node-ID, heartbeat multiplier, mandatory
slave policy, automatic NMT Start and Reset Communication belong in
`master.yml`. Do not move these product decisions into `runtime.c`. The checked-in
example currently uses Master Node-ID `127`; `0xFF` is Lely's unconfigured
sentinel and does not advance through the normal NMT boot-up state.

`dcf2c` generates `master_sdev.c`; `master_sdev.h` is a project-maintained thin
declaration header. With `dcfgen -r`, Node1 TPDO1 is represented on the Master
side by the RPDO/mapping and remote-PDO metadata objects. This generation step
does not by itself prove target compilation, CAN traffic, Boot-up, SDO or PDO
behavior.
