# SDD-2026-07-16-terrain-instance-initializer: Terrain GBuffer 实例初始化修复

## Status

Done（2026-07-16）

## Context

`fefd2e0` 为 `AshTerrainInstance` 增加了 `implicit_layer_zero`，并同步了 packed decode、VS varying 和材质权重 fallback，但 `TerrainSurface.hlsl::PSMain` 的局部 `weight_instance` 聚合初始化仍只有旧字段。DXC 编译 `TERRAIN_GBUFFER=1` 时因此报告 `expected 8 elements, have 7`，DX12 的 `SceneRenderer` 初始化失败；既有 source-contract test 只检查符号存在，没有覆盖该初始化器的完整性。

风险为 S1：单一 Terrain pass 的局部 shader bugfix，不改变 RHI、RenderGraph、资源绑定、材质约定、采样数学或可见输出。

## Decision

- 在 `weight_instance` 初始化器末尾显式传递 `input.implicit_layer_zero != 0u`，使局部实例与公共结构定义一致。
- 扩展现有 Terrain shader contract test，要求该字段同时从 packed instance 进入 VS varying，并进入 PS 局部实例初始化。
- 用仓库 DXC 直接编译 GBuffer PS 的 DXIL 与 SPIR-V permutation，作为运行时前置回归证据。
- 继续执行四组合 readiness、non-bless RenderGate 和 Standard PerfGate；任一失败停止，不 bless。

## Verification

- RED：`RunTests.bat Debug --test-case="Terrain surface treats empty weights*"`
  在完整初始化器断言处精确失败（1 case，7/8 assertions）。
- GREEN：同一 focused case 通过（1/1 case，8/8 assertions）；随后 Debug/Release
  全量测试均通过（489/489 cases，25949/25949 assertions，1 skipped）。
- DXC：`PSMain / TERRAIN_GBUFFER=1` 的 `ps_6_5` DXIL 与 SPIR-V permutation
  均编译成功。
- `run.bat all Debug --smoke-test-seconds=120` 四组合通过，8 份 fresh 日志的
  error/critical/validation/device-lost/fatal/assert/compile 拒绝词为 0。
- non-bless `RunRenderGate.bat` 通过，报告
  `Intermediate/test-reports/render-gate/20260716-111044-267-65292-4226f0af`；
  Sandbox Vulkan/DX12/cross SSIM 为 0.996278/0.996177/0.999747，Particles
  三项均为 1。
- `RunPerfGate.bat -Profile Standard` 通过，报告
  `Intermediate/test-reports/perf-gate/20260716-111328-1938263-e9a23a34`；
  四组合均 PASS，Warnings/Failures 为空。
- 上述 GPU 门禁均未 bless；每次运行后 Engine.ini、EditorSettings、
  ViewportLayout 与 imgui.ini 均按运行前字节快照恢复，最终 effective roots 为 0。
