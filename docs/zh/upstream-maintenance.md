# Frozen Upstream 维护

> 摘要：说明 UPSTREAM.lock、vendor/source allowlist、manifest 与维护工具如何保证 frozen Lely 子集身份和 target 边界可审计。

[English](../en/upstream-maintenance.md)

本项目不会把任意 Lely checkout 直接当成 target source。上游源码先按明确 vendor policy 导入 frozen subset，生成完整性 hash，再由 RT-Thread target source allowlist 进一步收窄 MCU 编译范围。

## 1. 四个 metadata 文件分别负责什么

| 文件 | 职责 |
|---|---|
| `metadata/UPSTREAM.lock` | 记录来源、archive/ref identity 与 verification status。 |
| `metadata/VENDOR_ALLOWLIST.txt` | 定义哪些 upstream module/file 可以进入 frozen snapshot。 |
| `metadata/VENDOR_MANIFEST.sha256` | 证明当前 frozen bytes 没有被意外修改。 |
| `metadata/RTTHREAD_SOURCE_ALLOWLIST.txt` | 定义哪些已导入 `.c` 有资格进入 RT-Thread target build。 |

这些文件属于维护/构建 policy，不作为 runtime 数据进入 firmware。

## 2. 当前 baseline 身份

lock file 记录原始 baseline archive 的 SHA-256。因为原 archive 没有提供足够 Git metadata 来证明 exact source commit，所以在受控 update 建立 exact identity 之前，对应 ref 字段保持 unresolved。

lock 中还记录了早期工作期间观察到的 public GitHub mirror commit。它只是 reference point，不能描述成已经证明与原 archive byte-for-byte 相同。

identity 未解析时，archive hash + `VENDOR_MANIFEST.sha256` 才是当前 frozen bytes 的身份/完整性证据。

## 3. Vendor allowlist 与 target source allowlist

`VENDOR_ALLOWLIST.txt` 回答“哪些 Lely 内容允许被导入仓库”。当前保留 pure-C `libc`、`util`、`can`、`co`、`ev`、`io2` 所需 include/source 范围和 license notice。

`RTTHREAD_SOURCE_ALLOWLIST.txt` 回答“哪些已导入 C file 有资格参与 MCU build”。`SConscript` 加载该列表后，再根据 Kconfig 关闭的 feature 删除对应 source。

两层策略分离后，vendor snapshot 可以保留经过审查的模块闭包，而不需要让每个 firmware profile 编译全部 retained file。

## 4. 完整性检查

从仓库根目录执行：

```sh
./tools/check_vendor.sh
```

它检查必要 metadata、allowed module boundary、symlink policy、retained public-header closure 和 SHA-256 manifest。

PASS 只证明 vendor snapshot 与当前 policy 一致，不是 compiler、linker、ABI 或 target runtime 测试。

## 5. 从 Git tag/commit 更新

推荐：

```sh
./tools/update_lely.sh --ref <tag-or-commit>
```

需要指定 mirror 时：

```sh
./tools/update_lely.sh \
    --ref <tag-or-commit> \
    --remote https://github.com/lely-industries/lely-core.git
```

tool 只导入 vendor allowlist 允许的内容，并更新 lock/manifest。通过受控 Git fetch 解析后，才能把 concrete commit 和 verification method 写入 lock。

## 6. 从本地 tree 更新

已有 clean Lely Git worktree：

```sh
./tools/update_lely.sh --source /path/to/lely-core
```

只有普通 export、没有 `.git` 时，可以附带已知 ref：

```sh
./tools/update_lely.sh \
    --source /path/to/lely-core-export \
    --ref <known-tag-or-commit>
```

这种 ref 只能标记为 user-asserted；byte-level authority 仍然是 manifest。

## 7. Upstream 更新后必须审查什么

新 upstream 可能改变 feature macro、source dependency、public struct layout、CANopen API 和 build assumption。至少检查：

- `include/lely/features.h` 与 ABI-affecting feature macro；
- retained module 的 `Makefile.am` source list；
- retained module 新增/删除的 source；
- `RTTHREAD_SOURCE_ALLOWLIST.txt` coverage；
- Kconfig/SConscript feature-to-source mapping；
- `port/rtthread/lely_rtt_config.h` policy；
- RT-Thread 适配层对 changed Lely API 的调用；
- Host generation 行为和 generated OD diff；
- vendor check、Host/source test、真实 BSP build 与 target CAN validation。

manifest clean 不等于新 upstream 已经适配完成。

## 8. License 文件

`LICENSE` 与 `NOTICE` 继续放在 repository root，因为它们是第三方源码再分发 notice，不是内部 metadata。
