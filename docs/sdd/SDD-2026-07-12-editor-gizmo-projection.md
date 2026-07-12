# Mini SDD: Editor Gizmo 方向参照与平面手柄透视修复

级别：S1（Editor 单模块视觉与命中 bugfix）

## Goal

- Scene viewport 右上角方向指示器使用当前 Editor 相机的 right/up/forward，和中央
  Move/World gizmo 使用同一 view matrix，不再固定显示 identity 朝向或让 Z 轴坍缩。
- Move/Scale 的 XY/XZ/YZ 平面手柄由世界平面四角逐点做透视投影，绘制轮廓、填充与
  命中区域使用同一投影四边形，不再用固定的屏幕轴对齐正方形。

## Non-goals

- 不改变中央 X/Y/Z 轴线的现有 `projection * view * world` 投影；已确认其 LH_ZO、
  屏幕 Y 翻转和场景相机 override 一致。
- 不调整 Editor GUI 与 scene presentation 的帧阶段，不处理仅在相机运动期间可能出现的
  一帧背景/overlay 延迟。
- 不改 gizmo 拖拽语义、坐标空间、pivot、snap、RHI、RenderGraph 或 Engine UIContext API。
- 不做 GPU/3D gizmo 重写。

## Files

- `project/src/editor/Services/EditorGizmoMath.{h,cpp}`：可单测的 view basis 提取与凸四边形命中。
- `project/src/editor/Services/EditorGizmoViewport.{h,cpp}`：世界平面手柄四角的统一透视投影。
- `project/src/editor/Services/EditorGizmoTypesInternal.h`：平面手柄视觉由 `UIRect` 改为四角。
- `project/src/editor/Services/MoveScaleGizmoTool.cpp`：构建、绘制并命中投影四边形。
- `project/src/editor/Panels/ViewportPanelCanvas.cpp`：从当前 viewport view matrix 构建方向指示器参数。
- `project/src/editor/Widgets/ViewportAxisIndicator.{h,cpp}`：移除可静默回退 identity 的默认实参，
  并按相机 forward depth 由远到近绘制轴。
- `project/src/tests/Editor/editor_gizmo_projection_tests.cpp`、`project/src/tests/premake5.lua`：
  编译纯 Editor gizmo math/projection 源并覆盖相机 basis、透视四边形与命中。
- `docs/specs/modules/editor.md`：回写方向参照与平面手柄投影约束。

## Approach

1. 从 inverse-view 的前三列提取并归一化 camera right/up/forward；中央 gizmo 的
   `ComputeCameraForward` 和右上角方向指示器共用该实现。方向指示器调用必须显式传参，
   相机上下文尚不可用时跳过绘制，避免以后再次无声使用 identity。
2. 平面手柄在 gizmo world length 的 `[0.26, 0.42]` 区间构造四个世界角点，按稳定绕序
   逐点走现有 `TryProjectWorldToViewport`。这保留透视除法，不能用投影后轴端点做线性插值；
   投影面积低于 1px² 时视为退化，不绘制也不命中。
3. UIContext 没有多边形接口；Editor 用随投影跨度自适应、上限 64 的插值扫描线填充
   凸四边形，再画四条边。
   命中测试使用同一四角的凸多边形 half-space 判断，保证看到的位置就是可拖拽区域。
4. Tests 工程只额外编译无 UI 状态的 `EditorGizmoMath.cpp` 与 `EditorGizmoViewport.cpp`；
   不链接 Editor 可执行程序，也不新增第三方依赖。

## Verification

对照 `docs/VERIFY.md` 的 Editor UI 与测试基建矩阵：

1. TDD red/green：生成 solution 后过滤运行新增 doctest，先确认缺少新契约时编译失败，
   实现后新增用例通过。
2. `RunTests.bat Debug`
3. `RunArchGate.bat`
4. `build_editor.bat Debug`
5. `run.bat editor dx12 Debug --smoke-test-seconds=120`
6. `run.bat editor vulkan Debug --smoke-test-seconds=120`
7. 人工/截图检查：Move/Scale/Rotate、World/Local、orbit/pan/zoom/F focus；方向指示器和
   Move/World 轴方向一致，三组平面手柄随相机形成正确透视四边形，拖拽命中不偏移；
   `product/logs` 无 Error/validation。

纯 Editor overlay 不进入 Sandbox RenderGate golden，也不是性能敏感渲染 Pass，因此不跑
`RunRenderGate.bat` / PerfGate。

实际结果（2026-07-12）：

- 在基于 `3c515295801c33324ea4e39ca5b91106d8876c49` 的独立验证 worktree 中删除旧
  solution 后重新运行 `generate_vs2022.bat`，Tests 工程包含新增 Editor gizmo 源。
- TDD RED 阶段因新增契约尚不存在而编译失败；GREEN 后定向 doctest 通过
  （2 cases / 27 assertions），完整 `RunTests.bat Debug` 通过（39 cases / 469 assertions）。
- `build_editor.bat Debug` 通过（0 errors；既有编译警告仍在），PostBuild artifact 同步成功；
  `RunArchGate.bat` 通过（35 条均为既有 legacy warning，无新增越界）。
- DX12、Vulkan Editor readiness smoke 均在 frame 3 就绪并以 0 退出；Vulkan 报告无 live
  allocations，两个后端日志均无 Error、VUID 或 validation 报错。
- DPI-aware DX12 实机截图确认：右上角 XYZ 与中央 Move/World gizmo 逐轴方向一致；三组
  平面手柄是随相机形成的透视四边形，绘制区域与新增命中单测使用同一四角。
- 扩展的合成输入回放未作为验证证据：短重试中 mouse down/up 被同帧合并，截图显示模式
  未切换。此次修复的实际 UI 路径已由 Move 目视检查覆盖，Scale 共用同一投影/绘制/命中
  路径并由单测覆盖；Rotate 未修改。
- `scripts/AIDevDoctor.ps1 -Mode ValidatePlan` 与 gizmo 文件范围的 `git diff --check` 均以
  0 退出；独立复审结论为 Ready，无 Critical、Important 或 Minor finding。

## Risk / rollback

- 轴接近视线时，真实投影会让对应平面手柄变窄；这是正确透视，轴线与其他平面仍可操作。
- 扫描线填充只覆盖约 16px 级小手柄，最多 65 条 UI draw calls；避免缩放后出现空隙，
  同时保持明确上限。
- 单一提交可 `git revert`；无数据、配置或资产迁移。

## Status

Done（2026-07-12；相机方向参照与透视平面手柄修复完成，验证见上）
