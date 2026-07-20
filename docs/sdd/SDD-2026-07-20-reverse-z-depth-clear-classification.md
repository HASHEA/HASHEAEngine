# Mini SDD: Reverse-Z 深度背景判定修复

## Status

Done

## Goal

修复大尺度场景中有效的远端 reverse-Z 深度被误判为背景的问题。以 8 km Terrain 为回归场景：GBuffer 已覆盖的远端像素必须继续进入级联调试、阴影、延迟光照、环境光、AO、体积光、粒子软深度与 Depth Debug 路径，不能因设备深度落入 `(0, 1e-6]` 而被丢弃。

## Non-goals

- 不修改 Terrain LOD、网格索引、材质、阴影投射开关或资产格式。
- 不修改 Editor 动态 near/far 拟合、reverse-Z 投影或 shadow cascade split。
- 不改变 RenderGraph、RHI、shader 资源绑定、GBuffer 格式或性能/渲染基线。
- 不处理世界坐标重建中独立的除零保护 epsilon。

## Files

- 新增 `project/src/engine/Shaders/Scene/SceneDepthCommon.hlsli`，提供统一的 scene-depth clear 判定。
- 迁移以下 8 个消费者：
  - `Deferred/DeferredCommon.hlsli`
  - `Deferred/EnvironmentCommon.hlsli`
  - `Deferred/AmbientOcclusionCommon.hlsli`
  - `Deferred/VolumetricLightingCommon.hlsli`
  - `Shadow/DirectionalShadowMask.hlsl`
  - `Shadow/DirectionalShadowCascadeDebug.hlsl`
  - `Debug/RenderDebugView.hlsl`
  - `Particles/ParticleSystem.hlsl`
- 新增 `project/src/tests/Function/scene_depth_clear_contract_tests.cpp`。
- 回写 `docs/specs/modules/render.md`、`docs/specs/features/deferred-lighting.md`、`docs/specs/features/shadows.md` 与 `docs/specs/features/render-debug-view.md`。

## Approach

设备深度的背景语义取决于 depth target 的精确 clear 端点：reverse-Z 为 `0.0`，普通 Z 为 `1.0`。共享 helper 只把该端点及其范围外值判为背景，即 reverse-Z `depth <= 0.0`、普通 Z `depth >= 1.0`；不再使用固定 epsilon。D32 clear 值可精确表示，而光栅化后仍位于 far clip 内的有效几何深度严格落在开区间 `(0, 1)`。

测试先用 `near=0.03`、`far=8701.6`、`view-z=8292` 证明合法 reverse-Z 深度约为 `1.7e-7`，再约束所有 8 个消费者统一调用共享 helper，禁止重新引入 `1e-6/0.999999` 背景阈值。实现不新增纹理绑定或运行时分支数量，只替换既有判据。

## Verification

- RED/GREEN：`RunTests.bat Debug --test-case="*scene depth clear*"`
- 全量单测：`RunTests.bat Debug`
- 架构门禁：`RunArchGate.bat`
- 双目标构建：`build_editor.bat Debug`、`build_sandbox.bat Debug`
- 双后端/双目标 readiness：`run.bat all Debug --smoke-test-seconds=120`
- Vulkan 与 DX12 validation short smoke，fresh 日志不得出现 validation/debug-layer error。
- 渲染门禁：`RunRenderGate.bat`（non-bless）。
- 性能门禁：`RunPerfGate.bat -Profile Standard`，不得 FAIL；WARN 需单独裁定。
- 人工同机位复核：远角在 GBuffer E、SunLight Cascade Index 与最终光照中均保持覆盖；关闭 Terrain Cast Shadow 不改变覆盖；相机远近移动不得重新出现黑楔。

## Result

实现提交 `a4608b4` 新增统一 exact-clear helper，并迁移 8 个 screen-depth 消费者。focused contract 为 3/3、66/66，全量 Debug doctest 为 509/509、26354/26354；ArchGate PASS（35 条既有 legacy warning），Editor/Sandbox Debug 构建均 PASS。四组合 readiness exit 0；双后端 Debug TimingValidation 报告 `Intermediate/test-reports/perf-gate/20260720-151455-9734369-70a8ea78` PASS；non-bless RenderGate 报告 `Intermediate/test-reports/render-gate/20260720-151525-568-73252-f60c3464` PASS；Standard PerfGate 报告 `Intermediate/test-reports/perf-gate/20260720-151626-9346482-f636e52b` 四组合 PASS 且 warnings/failures 为空。自动矩阵与两次人工 Editor 共 32 份 fresh 日志拒绝词为 0，Vulkan/DX12 同机位人工 A/B 均确认 GBuffer E、Cascade Index、最终光照、Cast Shadow 开关与相机远近移动正确。未 bless/import；五份配置与性能 baseline 已逐字节恢复。

## Risk / rollback

风险是原先被 epsilon 吸收的极小非零深度现在会进入重建与光照。该值对 D32 scene depth 表示 far clip 内的合法几何；NaN 不会被当背景，仍由现有有限值/validation 路径暴露。双后端 validation、RenderGate 与 8 km 人工 A/B 覆盖该风险。若出现回归，整体回退共享 helper 与 8 个迁移点；不需要迁移资产、配置或基线。
