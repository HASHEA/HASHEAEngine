# Mini SDD: Directory 路径安全与事务式状态更新

## Status

Done

## Goal

- 消除 `Directory::path[512]` 上未经边界检查的复制与拼接，避免越界写、越界读和非法终止符写入。
- 让目录打开和子目录切换具备事务语义：新目录完整校验并成功打开后才替换原路径与句柄；任何失败均保持原状态不变。
- 消除重复打开造成的目录查找句柄泄漏，并让关闭操作可重复调用。
- 保持现有 `Directory` 布局、函数签名及已接受的路径片段语义兼容。

## Non-goals

- 不在本次 S1 修复中把固定容量路径改为动态字符串，也不引入 RAII 目录类型；此类 ABI/API 重构另行按 S2 设计。
- 不收紧调用者可传入的路径片段：`..`、嵌套分隔符、正反斜杠及绝对路径形态仍按 Win32 现有解析规则处理。
- 不改变文件读写、删除、枚举等其他 `hfile` API，也不修改 Graphics、RenderGraph 或双后端代码。
- 不通过静默截断来接受超长路径。

## Files

- `project/src/engine/Base/hfile.h`
- `project/src/engine/Base/hfile.cpp`
- `project/src/tests/Base/hfile_tests.cpp`
- `docs/specs/modules/base.md`
- `docs/sdd/SDD-2026-07-14-directory-path-safety.md`

## Approach

1. 保持 `Directory` 的字段顺序、容量和现有函数签名不变；为目录状态提供安全的默认空值，并仅给测试和外部调用需要的现有目录函数补齐 DLL 导出标记，不增加新的公共抽象。
2. 在 `hfile.cpp` 内使用局部固定容量缓冲区构造目录搜索模式。输入为空、解析结果超出 `k_max_path`、追加分隔符或通配符后无法完整容纳时直接失败，禁止截断。`GetFullPathNameA` 使用 `k_max_path` 而不是 `MAX_PATH`；API 返回所需长度大于等于容量时拒绝，API 自身失败时仅允许在原始字符串可完整容纳的前提下沿用原始路径。
3. 目录搜索模式保持兼容：末尾已有 `*` 时不重复追加；末尾为 `\\` 或 `/` 时直接追加 `*`；其他情况追加 `\\*`。不规范化或拒绝调用者提供的 `..`、嵌套路径和绝对路径形态。
4. `file_open_directory` 先在局部对象中完成路径构造和 `FindFirstFileA`。仅在新句柄有效、且旧句柄（若有）可正常关闭后，一次性提交新路径与句柄；任一步失败都关闭新句柄并保持输出对象原样。
5. `file_sub_directory` 从当前路径的局部副本中移除末尾搜索通配符，再原样追加调用者提供的片段，并复用事务式打开流程。超长、空指针或目标不存在均返回失败且不修改原路径/句柄。
6. `directory_current` 先把当前目录写入局部缓冲区，仅在 Win32 返回长度处于 `1..k_max_path-1` 时继续；若对象已有搜索句柄，则成功释放旧句柄后再提交路径并清空句柄。路径查询或旧句柄关闭失败时保持传入对象不变。
7. `file_close_directory` 在成功关闭后立即清空句柄；空句柄及重复关闭视为成功。`file_parent_directory` 继续通过临时 `Directory` 打开父目录，并只在成功后替换当前对象。
8. doctest 覆盖正常打开/子目录/父目录、超长子路径不越界、目标不存在时状态保持、重复关闭，以及当前目录写入的容量边界；测试使用真实临时目录与可验证的 canary/state，不依赖未定义行为制造崩溃。

## Verification

- RED：构建并运行新增的定向目录安全 doctest，确认现有实现至少在失败状态保持或重复关闭契约上失败。
- GREEN：运行定向目录安全 doctest。
- `RunTests.bat Debug`
- `RunArchGate.bat`
- `build_editor.bat Debug`
- `build_sandbox.bat Debug`
- `run.bat all Debug --smoke-test-seconds=120`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `git diff --check`，并确认仅暂存本 SDD 允许的文件，不包含共享工作区或 LFS 噪声。

## Risk / rollback

- 兼容性风险主要来自失败语义变为“原状态不变”、关闭变为幂等，以及重复打开时会主动释放旧句柄；仓库内没有目录导航 API 的外部调用点，新增测试固定这些更安全的契约。
- 固定 512 字节容量仍是已知限制；本次明确拒绝超长输入而不是支持 Win32 long path，避免把 S1 修复扩大为 ABI 重构。
- 若发现兼容性回归，可整体回退本包的 `hfile`、测试与 Base spec 提交；不涉及持久化数据、配置格式或渲染基线。
