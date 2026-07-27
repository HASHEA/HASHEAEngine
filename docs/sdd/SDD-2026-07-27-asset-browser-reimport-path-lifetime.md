# Mini SDD: Asset Browser Reimport 路径生命周期

## Status

Implemented; awaiting focused manual recheck

## Goal

修复 Asset Browser 对选中资产执行 Reimport 时的确定性 use-after-free。`ReimportAsset`
会同步 refresh catalog，并使调用前取得的 `AssetInfo*` 失效；调用者必须在任何可能刷新
catalog 的操作前按值保存资产相对路径，之后只使用该稳定副本重新选择资产和写日志。

## Non-goals

- 不改变 Reimport 的 refresh 范围、AssetId 规则或 Terrain cache/publication 语义。
- 不改变 Asset Browser 的选择模型或其他文件操作流程。
- 不处理已记录的笔刷半径相关卡顿及 Import/Export UI 优化建议。

## Files

- `project/src/editor/Panels/AssetBrowserPanel.cpp`
- `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- `docs/specs/modules/editor.md`
- 本 Mini SDD

## Approach

1. 增加源码契约回归测试，锁定 `ExecuteReimportSelected` 必须在调用
   `ReimportAsset` 前复制 `relative_path`，且调用后不得再解引用旧 `AssetInfo*`。
2. 在面板 action 中按值保存路径；Reimport、重新选择和成功/失败日志统一使用该副本。
3. 回写 Editor 长期规格，明确 catalog mutation 会使此前取得的 `AssetInfo*` 失效。

## Verification

- 目标 doctest：Asset Browser Reimport 路径生命周期契约
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `build_editor.bat Debug`
- `RunArchGate.bat`
- `run.bat editor vulkan Debug --smoke-test-seconds=120`
- `run.bat editor dx12 Debug --smoke-test-seconds=120`
- 人工在 Vulkan/DX12 对相同未变化 Terrain 执行 Reimport，确认不崩溃且日志无
  stale-generation/application/validation error

## Risk / rollback

风险仅为路径副本与 refresh 后 catalog 中的目标不一致；Reimport 本身已经以同一复制前路径
定位磁盘对象，refresh 后再按该稳定路径查询是预期语义。回滚可移除路径副本与对应契约测试，
但会恢复 dump 已证实的 use-after-free。

## Crash evidence

- Dump：`C:\Users\huyizhou\AppData\Local\CrashDumps\Editor.exe.71400.dmp`
- Exception：`0xc0000005`，`Engine.dll`
- 异常栈：`AssetBrowserPanel::ExecuteReimportSelected` →
  `SelectAssetByPath` → `AssetDatabase::find_asset_by_path` →
  `std::filesystem::path::lexically_normal`
- 异常 path 缓冲区为 Debug Heap 释放填充值 `0xDD`；参数地址位于刷新前
  `AssetInfo::relative_path` 成员中。

## Validation record

- TDD RED：目标测试 3/4 assertions，通过构建但因 mutation 前未复制路径而按预期失败。
- TDD GREEN：目标测试 1/1 case、8/8 assertions。
- `RunTests.bat Debug`：624/624 cases、30,855/30,855 assertions，0 failed（2 skipped）。
- `RunTests.bat Release`：624/624 cases、30,855/30,855 assertions，0 failed（2 skipped）。
- `build_editor.bat Debug`：PASS。
- `RunArchGate.bat`：PASS，35 条均为既有 legacy warnings。
- Vulkan/DX12 Editor readiness smoke：均 PASS，分别在 frame 86 / frame 4 达成 readiness；
  对应最新日志无 stale-generation、validation、device-loss、assertion 或 crash 信号。
- Standard PerfGate：`20260727-112950-626-55536-0eab5554`，Editor/Sandbox ×
  Vulkan/DX12 四组合全部 PASS，无 failures/warnings，未 bless baseline。
- 人工 Reimport 复测仍待在包含本修复的精确 commit 上执行。
