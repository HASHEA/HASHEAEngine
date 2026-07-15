# Mini SDD: Terrain Save/外部写竞态测试确定性同步

## Status

Done

## Goal

修复 `Terrain save completion does not swallow a later external write` 测试自身的未排序并发。测试必须确定性地证明：Save 已完成容器 commit，worker 已冻结包含 committed revision 与 source write time 的完整成功结果、但尚未 resolve/被 Editor 主线程消费时，随后发生的外部写不会被旧 Save completion 吞掉。

## Non-goals

- 不改变生产环境 Save、容器 commit lease、Busy、revision、write-time 或外部变更轮询语义。
- 不给 Save 增加重试，也不放宽现有事务合同。
- 不修改 Terrain UI、shader、场景、配置或运行时公开接口。

## Files

- `project/src/editor/Services/TerrainEditorService.h`
- `project/src/editor/Services/TerrainEditorService.cpp`
- `project/src/tests/Editor/terrain_editor_service_tests.cpp`
- 本 Mini SDD

## Approach

当前测试在提交 Save 后反复调用 `load_terrain_container`，loader 的最终 revision inspect 与 Save writer 使用同一 named commit lease；writer 只做一次 non-blocking acquire，因此测试可能先把 worker 推入 `Busy`。即便 loader 读到新 generation，worker 仍可能尚未完成 write-time 采样和 future resolve，随后 remove/rewrite 外部文件又形成第二个窗口。

在现有 `ASH_TESTS` 专用 `FileJobTestHook` 中增加 `AfterSaveResultCaptured` 点。它只在 Save 成功、checked container 调用已返回并释放 commit lease、committed revision 已验证、source write time 已成功捕获且完整 `TerrainFileJobResult` 已冻结后触发，位置紧邻 `dispatchState->Resolve(...)` 之前。此时 worker 不再读取目标文件，因此测试可用现有 blocker 等待该线性化点，单次验证已提交 generation，写入外部 generation，再释放 worker并由正常 `Update()` 消费 completion。该回归使用约 2 秒的有界 hook 等待；超时时只做一次 `Update()` 消费已经就绪的 worker failure，并让断言输出 operation error，避免 Save 在到达 hook 前失败时挂死整套测试。生产构建不包含该枚举、hook 或分支；Optimize 不触发该点。

## Verification

- RED：`RunTests.bat Debug --test-case="Terrain save completion does not swallow a later external write"` 在起始 HEAD `3c09f48ba22a600a204f8064171f3f3ec4aafdcb` 稳定 `0/1`；原测试 line 3630 得到 generation 1、期望 2。
- GREEN：初始同步实现连续 3 次通过，每次均为 `1/1` test case、`18/18` assertions、exit 0；加入 bounded-wait 防挂死后再次连续 3 次通过，每次均为 `1/1` test case、`19/19` assertions、exit 0。
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `RunArchGate.bat`

## Risk / rollback

风险限于 `ASH_TESTS` 下 hook 时序。hook 放在 commit lease 已释放之后；回归用例以有界等待拒绝永久阻塞，超时或断言失败时 blocker 析构仍通过现有 RAII release 解除可能稍后到达的 worker。focused 与全量测试覆盖。若同步点不能证明原竞态语义，可整笔回退，不影响生产二进制。

## Result

- RED：起始 HEAD 上 focused 用例稳定 `0/1`，失败为 saved generation 实际 1、期望 2；根因是测试 loader 与一次性 Save writer 竞争同一 zero-timeout commit lease，且随后外部 rewrite 仍可与 worker metadata/result 捕获竞争。
- Focused GREEN：实现后连续运行 3 次同一 Debug focused 命令，三次均为 `1/1` test case、`18/18` assertions、exit 0。
- Bounded-wait GREEN：质量复核修订后再次连续运行 3 次同一 Debug focused 命令，三次均为 `1/1` test case、`19/19` assertions、exit 0。
- Fresh 全量：`RunTests.bat Debug` 为 `419/419` test cases、`24897/24897` assertions；`RunTests.bat Release` 为 `419/419` test cases、`24896/24896` assertions，均 exit 0。
- `RunArchGate.bat` PASS，仅报告 35 条既有 legacy WARN；规格与质量复核均为 clean、P0/P1/P2 为 0。
