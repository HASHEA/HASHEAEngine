# Mini SDD: Terrain 图层列表稳定三列布局

## Status

Implementing

## Goal

修复 Terrain Mode 在新增图层后名称、Visible 与 Locked 控件位置混乱及点击区域重叠的问题。图层列表固定为 Layer、Visible、Locked 三列；选择图层只作用于第一列，两个复选框各自拥有稳定且互不重叠的交互区域。

## Non-goals

- 不改变图层数据模型、稳定 ID、默认可见/锁定状态或命令语义。
- 不修改 `UIContext` 公共接口，不直接依赖 ImGui。
- 不在本修复中实现实时雕刻或调整笔刷行为。

## Files

- `project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp`
- `project/src/tests/Editor/terrain_editor_contract_tests.cpp`
- `docs/specs/modules/editor.md`
- 本 Mini SDD

## Approach

使用现有 `UIContext` table facade 绘制三列表格：Layer 为 stretch 列，Visible 与 Locked 为固定宽度列。每个图层继续以稳定 `TerrainLayerId` 建立 ID scope；selectable 仅位于 Layer 单元格，两个 checkbox 使用隐藏标签并分别位于自己的单元格。先增加源码契约 RED，约束三列、表头、逐行换列及禁止回退到 `same_line()` 拼接。

## Verification

- `RunTests.bat Debug --test-case="Terrain layer list uses stable independent columns"`
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `RunArchGate.bat`
- `build_editor.bat Debug`
- `run.bat editor Debug --smoke-test-seconds=120`
- 由人类测试者在 Vulkan 与 DX12 Editor 中新增多个长短名称图层，验证选择、Visible、Locked 不串扰；AI 不代签。

## Risk / rollback

风险局限于 Terrain Mode 列宽与控件命中区。若表格 facade 在窄面板中表现异常，可整笔回退到修复前实现；不涉及资产或场景数据迁移。

## Result

`DrawLayerList` 已改为带表头的稳定三列表格；稳定 layer ID、选择与 Visible/Locked action 语义保持不变。Focused 源码契约在实现前因缺少 table/column 设置按预期 RED，实现后 GREEN。Fresh `RunTests.bat Debug` / `Release` 分别通过 `419/419` test cases、`24897/24897` assertions 与 `419/419` test cases、`24896/24896` assertions；Editor/Sandbox Debug/Release 构建、`RunArchGate.bat` 与 AIDevDoctor ValidatePlan 均通过。自动化实现/测试已完成，但四组合 readiness 与 Vulkan/DX12 人工 UI 验证尚未完成，因此未宣告 SDD Done。
