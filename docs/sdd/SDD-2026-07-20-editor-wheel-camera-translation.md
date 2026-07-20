# Mini SDD: Editor 相机移动速度与滚轮平移修正

## Status

Done

## Goal

- Scene Viewport 内滚轮沿相机朝向前后平移；连续滚动时不因接近 orbit target 而减速或停止。
- 普通输入使用当前默认相机速度；按住 Shift 时，滚轮前后、MMB Pan 与 Alt+RMB Dolly 的当次移动速度均乘以 2。
- Shift 与滚轮输入均不得改写默认相机速度或持久化设置。

## Non-goals

- 不修改 Camera Speed 设置项及其持久化格式。
- 不修改 Alt+LMB Orbit 的旋转灵敏度、F Focus 的瞬时定位或 Gizmo 输入。
- 不改 Engine 输入接口、Graphics/RHI、RenderGraph 或渲染后端。

## Files

- `project/src/editor/Services/EditorViewportCameraService.cpp`
- `project/src/editor/Services/EditorViewportCameraService.h`（仅在测试接缝确有需要时）
- `project/src/tests/Editor/editor_viewport_camera_tests.cpp`
- `project/src/tests/premake5.lua`
- `docs/specs/modules/editor.md`
- 本 Mini SDD

## Approach

当前滚轮路径按 `orbit distance * scroll factor * camera speed factor` 缩短 orbit distance，并将其钳制到最小距离。因此前滚会让后续步长持续变小，最终无法继续前进。

改为按每次滚轮输入计算固定平移量：基础速度取当前默认相机速度。将同一位移同时应用到相机位置与 orbit target，保持 orbit distance 不变，使连续滚轮输入可持续前后移动，并保留后续 Orbit 的中心关系。

统一解析本帧相机移动速度：未按 Shift 时使用默认速度，按下 Shift 时使用两倍默认速度。滚轮前后、MMB Pan 与 Alt+RMB Dolly 均消费该解析结果，但不得写回默认速度。Alt+LMB Orbit 继续使用固定旋转灵敏度，F Focus 继续瞬时定位。计算逻辑以最小可测单元接入 doctest，避免通过测试专用公共接口暴露内部状态。

## Verification

- `RunTests.bat Debug --test-case="*viewport camera*"`：验证普通滚轮步长稳定、前后方向对称且 orbit distance 不变；验证 Shift 对滚轮、Pan、Dolly 恰为两倍且不修改默认速度。
- `RunTests.bat`：完整单元测试回归。
- `build_editor.bat Debug`：Editor 构建。
- `RunArchGate.bat`：Editor/Engine 依赖边界检查。
- `run.bat editor vulkan Debug --smoke-test-seconds=120`：Editor Vulkan readiness smoke。
- `run.bat editor dx12 Debug --smoke-test-seconds=120`：Editor DX12 readiness smoke。
- 人工检查 Scene Viewport：连续前滚不会减速或停住；反向滚轮可后退；滚轮、Pan、Dolly 的普通速度稳定且 Shift 为两倍；Orbit/F Focus 行为不变；日志无新增错误。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `git diff --check`

## Risk / rollback

风险集中在滚轮手感和 Orbit 中心连续性。通过保持相机位置与 orbit target 的相对向量不变，避免滚轮后 Orbit 跳变。若回归，可回退滚轮平移分支及对应测试/规格，不涉及序列化或资产迁移。

## Result

实现将滚轮改为 camera position 与 orbit target 的同步固定步长平移，并统一对滚轮、Pan、Dolly 应用非持久化的 Shift ×2 倍率。长期行为契约已回写 `docs/specs/modules/editor.md`。
