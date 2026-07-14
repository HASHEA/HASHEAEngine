# Mini SDD: 移除未使用的 hserialization 模块

Status: Done

## Goal

删除 Base 层未使用且不可安全实例化的 `hserialization` 模块，消除无边界 blob 访问、错误内存所有权、越界写入和失效模板形成的误用风险。

仓库全量引用扫描只发现模块自身、对应 doctest 和 Base spec；用户确认不存在仓外调用者、历史 blob 或兼容性要求。因此本变更以完整移除代替修补或重写。

## Non-goals

- 不设计新的二进制序列化格式或公共 API。
- 不保留 `Blob`、`Serializer`、`RelativePointer`、`RelativeArray` 或 `RelativeString` 兼容桩。
- 不引入第三方序列化依赖。
- 不顺带重构 Base 容器、内存分配器、文件系统或其他模块。

## Files

允许修改的文件范围：

- 删除 `project/src/engine/Base/hserialization.h`
- 删除 `project/src/engine/Base/hserialization.cpp`
- 删除 `project/src/tests/Base/hserialization_tests.cpp`
- 更新 `docs/specs/modules/base.md`
- 新增 `docs/plans/2026-07-14-remove-hserialization.md`
- 更新本 SDD 的状态与完成结论

## Approach

1. 删除实现、声明和只覆盖该模块的两项 doctest。
2. 更新 Base 长期 spec，移除“提供二进制序列化”的职责、文件和验证描述；记录本次删除结论。
3. 重新生成 Visual Studio 工程，确保 Premake 的文件收集结果不再包含已删除文件。
4. 用全仓文本扫描确认没有 `hserialization` include，也没有上述五类已删除符号的代码引用。

删除后 Base 不提供通用二进制序列化能力。未来若出现真实消费者，应基于明确的数据格式、边界校验和所有权模型单独立项，不恢复当前实现。

## Verification

- `generate_vs2022.bat`
- `RunTests.bat`
- `RunArchGate.bat`
- `build_editor.bat Debug`
- `build_sandbox.bat Debug`
- `run.bat all Debug --smoke-test-seconds=120`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `git diff --check`
- `rg -n 'hserialization|\b(BlobHeader|Serializer|RelativePointer|RelativeArray|RelativeString)\b' project/src premake5.lua`

`RunRenderGate.bat` 与 `RunPerfGate.bat -Profile Standard` 不在本包验证范围：仓内外均无消费者，删除不会改变渲染、内存或线程运行路径。

## Risk / rollback

- 风险：遗漏未纳入仓库扫描的消费者。用户已确认不存在仓外依赖；仓内遗漏会由重新生成、全量测试和两个可执行目标构建暴露。
- 风险：生成工程仍缓存已删除源文件。通过重新运行 Premake 并检查生成结果消除。
- 回滚：整体 revert 本包提交即可恢复原模块、测试和文档描述；不涉及资产迁移或持久化数据转换。

## Outcome

已完整删除 `hserialization` 的声明、实现和专属 doctest，重新生成工程后仓内代码不存在残留引用。Base 长期 spec 已同步移除二进制序列化职责；未增加兼容层或替代格式。
