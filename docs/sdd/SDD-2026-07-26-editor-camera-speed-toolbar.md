# Mini SDD: SceneView 相机速度控件迁移到视口工具栏

> 补写说明：本 SDD 为实现后补写。原因：实现时误将本变更定级为 S0 跳过了 Mini SDD，
> 经用户指出后按 S1 补齐流程。Status: Done。

## Goal

现有 "Camera Speed" drag 控件位置过于隐蔽（Scene 视口工具栏 → View 弹出菜单 → Interaction 子菜单），
将其迁移到 Scene 视口工具栏行尾右对齐位置，提高可发现性。控件手感（drag_float、范围
[0.25, 256]、持久化到 `fSceneViewportCameraSpeed`）保持不变。

## Non-goals

- 不改速度语义（曾讨论过 10^n 指数输入，用户确认现有线性 drag 手感可以，不改）
- 不改 clamp 范围、默认值、持久化机制
- 不影响 Sandbox 相机（`SandboxFreeCameraController` 独立实现，本就无关）

## Files

- `project/src/editor/Panels/ViewportPanelToolbar.cpp`：新增右对齐 `DrawSceneCameraSpeedControl`
  （label "CameraSpeed"）；`DrawViewportOptionsPopup` 签名简化（不再需要 deps/viewportId）
- `project/src/editor/Panels/ViewportPanelSupport.cpp/.h`：`DrawViewportInteractionOptionsMenu`
  移除 Camera Speed 项并简化签名；Scene 视口不再显示空的 Interaction 菜单（非 Scene 视口的
  "Accept Input" 保留）

## Approach

速度控件逻辑原样搬迁：读写仍走 `EditorViewportCameraService::Get/SetMoveSpeed` +
`EditorSettingsService.fSceneViewportCameraSpeed`。右对齐用
`same_line(get_window_width() - 控件宽 - label 宽 - 边距)` 实现，窗口过窄时允许与
gizmo 按钮重叠（与 ImGui 工具栏惯例一致）。

## Verification

对照 `docs/VERIFY.md` "Editor 面板 / UI" 行：

- [x] `build_editor.bat Debug` — 0 错误
- [x] `RunArchGate.bat` — PASS（仅 legacy 名单警告）
- [x] `run.bat editor Debug --smoke-test-seconds=120` — readiness smoke 通过
- [x] `run.bat editor` 手动走查：工具栏右侧 CameraSpeed 控件可拖动/可输入、位置合适、
      View→Interaction 菜单在 Scene 视口不再出现（用户已确认；label 按用户要求由
      "Speed" 改为 "CameraSpeed"）

## Risk / rollback

纯 Editor UI 布局变更，不触及渲染/引擎层。风险为窄窗口下控件重叠（可接受）。
回滚 = revert 上述三个文件。
