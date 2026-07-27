# Mini SDD: Asset Browser Reimport 路径生命周期

## Status

Implemented; automated validation passed, awaiting focused manual recheck

## Goal

修复 Asset Browser 对选中资产执行 Reimport 时的两处确定性 use-after-free：

1. `ReimportAsset` 会同步 refresh catalog，并使 action 调用前取得的 `AssetInfo*` 失效；
   调用者必须在 refresh 前按值保存资产相对路径。
2. Asset Browser 本帧构建的 `AssetBrowserFrameData` 借用 catalog 中的 `AssetInfo*`；
   item context menu、toolbar refresh 或 drag/drop move 在绘制过程中同步 refresh 后，
   当前帧必须停止使用全部借用指针，并从新 catalog 重建 frame data。

## Non-goals

- 不改变 Reimport 的 refresh 范围、AssetId 规则或 Terrain cache/publication 语义。
- 不改变 Asset Browser 的选择模型或其他文件操作流程。
- 不把全部 `AssetInfo` 复制进每帧数据，也不把 Engine `AssetDatabase` 的内部
  `catalog_generation` 暴露为新的跨模块 API。
- 不处理已记录的笔刷半径相关卡顿及 Import/Export UI 优化建议。

## Files

- `project/src/editor/Panels/AssetBrowserPanel.cpp`
- `project/src/editor/Panels/AssetBrowser/AssetBrowserContentView.cpp`
- `project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.cpp`
- `project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.h`
- `project/src/editor/Services/AssetDatabaseService.cpp`
- `project/src/editor/Services/AssetDatabaseService.h`
- `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- `docs/specs/modules/editor.md`
- `README.md`
- 本 Mini SDD

## Approach

1. 增加源码契约回归测试，锁定 `ExecuteReimportSelected` 必须在调用
   `ReimportAsset` 前复制 `relative_path`，且调用后不得再解引用旧 `AssetInfo*`。
2. 在面板 action 中按值保存路径；Reimport、重新选择和成功/失败日志统一使用该副本。
3. 为 `AssetDatabaseService` 维护 Editor-local catalog revision；每次实际进入 Engine
   refresh 后都推进，因为缺失 root 等失败路径也可能清空 catalog。空 root 未调用 Engine
   时不推进。`AssetBrowserFrameData` 在构建时捕获该 revision。
4. Asset Browser 每个子视图返回后比较 revision 并按需重建 frame data；list/icon item
   绘制在任一可能触发同步 mutation 的交互返回后先比较 revision，失配就完成 UI 栈配对
   并终止本帧循环。
5. 回写 Editor 长期规格，明确 catalog mutation 会使此前取得的 `AssetInfo*` 失效。

## Verification

- 目标 doctest：Asset Browser Reimport 路径生命周期契约
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `build_editor.bat Debug`
- `RunArchGate.bat`
- `run.bat editor vulkan Debug --smoke-test-seconds=8`
- `run.bat editor dx12 Debug --smoke-test-seconds=8`
- 人工在 Vulkan/DX12 对相同未变化 Terrain 执行 Reimport，确认不崩溃且日志无
  stale-generation/application/validation error

## Risk / rollback

风险一是路径副本与 refresh 后 catalog 中的目标不一致；Reimport 本身已经以同一复制前路径
定位磁盘对象，refresh 后再按该稳定路径查询是预期语义。风险二是 revision 在 refresh 失败但
catalog 未变化时产生保守的 frame-data 重建；该 false positive 不改变资产状态，代价仅是一帧
内重建一次 Asset Browser 派生数据。回滚可移除路径副本、revision 防护与对应契约测试，但会
恢复两份 dump 已分别证实的 action-local 与 frame-local use-after-free。

## Crash evidence

- Dump：`C:\Users\huyizhou\AppData\Local\CrashDumps\Editor.exe.71400.dmp`
- Exception：`0xc0000005`，`Engine.dll`
- 异常栈：`AssetBrowserPanel::ExecuteReimportSelected` →
  `SelectAssetByPath` → `AssetDatabase::find_asset_by_path` →
  `std::filesystem::path::lexically_normal`
- 异常 path 缓冲区为 Debug Heap 释放填充值 `0xDD`；参数地址位于刷新前
  `AssetInfo::relative_path` 成员中。
- 首次修复提交 `1669c19` 上连续执行两次 Reimport：第一次成功，第二次仍崩溃。
  第二份 dump 为
  `C:\Users\huyizhou\AppData\Local\CrashDumps\Editor.exe.35048.dmp`，异常
  `0xc0000005` 位于 `AssetBrowserContentView.cpp:291` 的
  `DrawAssetIconView`，正在解引用 refresh 前 `vecVisibleItems` 保存的
  `AssetInfo*`。这证明 action-local 路径副本已生效，但 frame-local 借用列表仍需
  catalog revision 失效边界。

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
- `1669c19` 人工复测：第一次 Reimport 成功，第二次在
  `DrawAssetIconView` 使用失效 frame-data 指针时崩溃；本记录因此未通过，后续自动与
  人工验证需在 frame-catalog 修复后重新执行。
- Follow-up TDD RED：目标 frame-catalog 测试 1/1 case 中 6/14 assertions 通过，
  缺少 service revision、frame capture 与绘制边界检查的 8 项按预期失败。
- Follow-up TDD GREEN：目标测试 1/1 case、14/14 assertions；两条 Asset Browser
  契约合跑为 2/2 cases、22/22 assertions。
- Follow-up `RunTests.bat Debug`：625/625 cases、30,869/30,869 assertions，
  0 failed（2 skipped）。
- Follow-up `RunTests.bat Release`：625/625 cases、30,869/30,869 assertions，
  0 failed（2 skipped）。
- Follow-up `build_editor.bat Debug`：PASS。
- Follow-up `RunArchGate.bat`：PASS，35 条均为既有 legacy warnings。
- Follow-up Vulkan/DX12 Editor readiness smoke：均 PASS，分别在 frame 88 / frame 4
  达成 readiness；无 validation、device-loss、assertion 或 crash 信号。
- 隐藏后台包装器运行 Standard PerfGate `20260727-114857-913-1540-1333a2a9` 时，
  两条 Vulkan PASS；两条 DX12 在采样边界收到 `Window minimized`，因 telemetry
  不完整而退出 1。无 crash 或门限超限，该报告不作为通过证据。
- 仓库原生前台复跑 Standard PerfGate：
  `20260727-115341-549-55588-ec71ae82`，Editor/Sandbox × Vulkan/DX12 四组合全部
  PASS，无 failures/warnings，未 bless baseline。
- frame-catalog 修复后的连续 Reimport 人工复测待执行。
