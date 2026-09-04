# Lely Upstream 元数据与维护说明

## 1. 为什么有 `metadata/`

`upstream/` 是冻结的第三方 Lely 源码。为了能够回答下面几个问题，需要在源码之外保留机器可读元数据：

- 这些源码最初从哪里来？
- 当前能不能证明对应某个 Git commit/tag？
- 更新 upstream 时允许导入哪些模块？
- frozen snapshot 有没有被意外修改？
- RT-Thread 最终允许编译哪些 `.c`？

因此四个机器文件统一放在：

```text
metadata/
```

它们不会进入目标固件。

## 2. 当前 frozen baseline 身份

当前 `metadata/UPSTREAM.lock` 记录的初始来源是用户提供的：

```text
BASELINE_ARCHIVE_NAME=lely-core(1).zip
BASELINE_ARCHIVE_SHA256=e1690e9925f213ec45f823e757461378432ce1764ec05321850f5dccd38fbc2e
BASELINE_EXACT_UPSTREAM_REF=UNRESOLVED
CURRENT_UPSTREAM_REF=UNRESOLVED
```

另外记录了 2026-09-02 当时公开 GitHub mirror `master` 的观察值：

```text
620d1858eb8520dbc3dc5e1a7314565becd54199
```

这个 commit **只是参考观察值，不代表当前 frozen snapshot 已被证明等于该 commit**。在从明确 Git ref 重新导入之前，当前源码身份以原始 archive SHA-256 和 `VENDOR_MANIFEST.sha256` 为准。

## 3. 四个 metadata 文件是否都需要

### `UPSTREAM.lock`

**建议保留，更新工具直接使用。**

它记录 upstream 来源和版本身份，例如：

```text
UPSTREAM_PRIMARY_URL
UPSTREAM_MIRROR_URL
BASELINE_ARCHIVE_SHA256
CURRENT_UPSTREAM_REF
CURRENT_REF_VERIFICATION
```

当前初始 baseline 来自用户提供的 `lely-core(1).zip`。该压缩包缺少足够 Git 元数据，因此精确 upstream ref 仍记录为 `UNRESOLVED`，而不是猜一个 commit。

后续通过 `update_lely.sh --ref ...` 从 Git 导入后，工具会把解析到的 commit 和验证方式写入该文件。

### `VENDOR_ALLOWLIST.txt`

**需要保留。**

它定义“允许从完整 Lely upstream 导入哪些目录”。当前主要保留：

```text
include/lely/{libc,util,can,co,ev,io2}
src/{libc,util,can,co,ev,io2}
LICENSE
NOTICE
```

它的作用是阻止 `coapp`、legacy `io`、`tap` 或未来新增模块在没有 Review 的情况下进入 frozen snapshot。

消费方：

```text
tools/update_lely.sh
tools/check_vendor.sh
```

### `VENDOR_MANIFEST.sha256`

**需要保留。**

它记录 frozen `upstream/`、`LICENSE` 和 `NOTICE` 的 SHA-256。

用途：

```text
./tools/check_vendor.sh
```

可以检测第三方源码是否被手工编辑、丢失或替换。

这个文件不是版本号；它是当前 frozen bytes 的完整性证明。

### `RTTHREAD_SOURCE_ALLOWLIST.txt`

**需要保留。**

它不是 vendor 更新文件，而是 RT-Thread build policy。`SConscript` 读取它决定哪些 Lely `.c` 有资格进入 MCU 构建。

详细说明见 [Kconfig、SCons 与 Source Allowlist](BUILD_AND_CONFIG.md)。

## 4. 为什么把它们集中在 `metadata/`

旧结构把这些文件全部放在根目录，会和 `Kconfig`、`SConscript`、`LICENSE` 混在一起。

整理后职责更明确：

```text
根目录
  -> 工程入口、构建入口、许可证

metadata/
  -> 机器可读 policy / lock / manifest

docs/
  -> 给人阅读的中文说明

tools/
  -> 操作 metadata 和 frozen upstream 的维护工具
```

因此旧的根级 `UPSTREAM.md` 不再单独保留，其说明内容已经合并进本中文文档。

## 5. `tools/` 是否需要

需要保留在源码仓库，但 **不进入 firmware build**。

### `tools/check_vendor.sh`

作用：检查 frozen upstream 是否仍满足 vendor policy。

执行：

```sh
./tools/check_vendor.sh
```

它会检查：

1. `metadata/` 中必要文件存在；
2. `VENDOR_ALLOWLIST.txt` 要求的目录/文件都存在；
3. 被排除的 top-level module 没有混进来；
4. frozen `upstream/` 中没有 symlink；
5. 保留源码引用的 `<lely/...>` public header 在当前 snapshot 中闭合；
6. `metadata/VENDOR_MANIFEST.sha256` 全部匹配。

它不是编译测试。

### `tools/update_lely.sh`

作用：从新的 upstream ref 或本地 source tree 重新生成 frozen `upstream/`。

它只导入 `metadata/VENDOR_ALLOWLIST.txt` 允许的内容，并更新：

```text
metadata/UPSTREAM.lock
metadata/VENDOR_MANIFEST.sha256
```

## 6. 从 Git tag/commit 更新

推荐方式：

```sh
./tools/update_lely.sh --ref <tag-or-commit>
```

默认 remote 从 `metadata/UPSTREAM.lock` 的 `UPSTREAM_PRIMARY_URL` 读取。

也可以显式选择 mirror：

```sh
./tools/update_lely.sh \
    --ref <tag-or-commit> \
    --remote https://github.com/lely-industries/lely-core.git
```

工具使用临时 sparse/partial checkout，不要求项目目录里维护完整 Lely Git submodule。

成功后：

```text
CURRENT_UPSTREAM_REF=<resolved commit>
CURRENT_REF_VERIFICATION=GIT_FETCHED
```

## 7. 从本地 upstream tree 更新

已有干净 Git worktree：

```sh
./tools/update_lely.sh --source /path/to/lely-core
```

要求：

- `--source` 必须是 Git repository root；
- worktree 必须 clean；
- 工具自动记录 HEAD commit。

如果只是普通 export 目录，没有 `.git`：

```sh
./tools/update_lely.sh \
    --source /path/to/lely-core-export \
    --ref <known-tag-or-commit>
```

这种情况下 ref 只能标记为 user-asserted；真正的 byte-level authority 仍然是 manifest。

## 8. Upstream 更新后的 Review 清单

每次更新后至少检查：

1. `include/lely/features.h` 是否新增/修改 feature macro；
2. `src/libc/Makefile.am`、`src/util/Makefile.am`、`src/can/Makefile.am`、`src/co/Makefile.am`；
3. `src/ev/Makefile.am` 与 `src/io2/Makefile.am`；
4. retained module 是否有新增/删除 `.c`；
5. public struct layout 是否受新的 `LELY_NO_*` 控制；
6. `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` 是否仍覆盖正确 target source；
7. `Kconfig`、`SConscript`、`lely_rtt_config.h` 是否需要同步；
8. 执行 `./tools/check_vendor.sh`；
9. 再进行 RT-Thread 配置/编译/目标板验证。

不要只因为 `check_vendor.sh` 通过，就认为新的 upstream 已适配完成。

## 9. LICENSE / NOTICE 为什么不放进 `metadata/`

`LICENSE` 和 `NOTICE` 是第三方源码再分发需要直接可见的许可证文件，因此继续留在 package root。

`metadata/` 只放项目维护用的机器 policy、lock 和 manifest。
