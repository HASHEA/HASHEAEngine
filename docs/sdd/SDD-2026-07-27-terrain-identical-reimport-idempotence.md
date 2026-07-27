# Mini SDD: Terrain 相同物理版本 Reimport 幂等

## Status

Done（2026-07-27；自动验证与最终 Vulkan 三连 Reimport 人工复测通过）

## Goal

修复 Asset Browser 对未变化 `.AshTerrain` 执行 Reimport/全目录 refresh 后，`AssetDatabase` 返回内容相同但指针不同的 immutable snapshot，继而被 `RenderAssetManager` 误报为 stale generation 的问题。

相同规范化资产路径下，若旧、新 snapshot 具备相同 `asset_id`、`content_generation`、`residency_revision`，且两者有效 `TerrainContainerRevision` 完全相同，则请求按相同物理版本幂等处理：保留既有 accepted/published snapshot、readiness 与 activity epoch，不产生应用错误。

## Non-goals

- 不放宽 `TerrainRenderAsset` 对普通同代不同指针 snapshot 的 stale 拒绝。
- 不接受无效或不同 `TerrainContainerRevision` 的同代 snapshot。
- 不改变 Asset Browser 的 refresh 范围、Terrain 容器格式、RHI、RenderGraph 或双后端实现。
- 不处理本轮人工验收记录的笔刷半径相关卡顿及 Import/Export UI 优化建议。

## Files

- `project/src/tests/Terrain/terrain_render_asset_tests.cpp`
- `project/src/engine/Function/Render/RenderAssetManager.cpp`
- `docs/specs/features/terrain.md`
- `docs/specs/modules/render.md`
- `docs/verification/terrain/2026-07-26-b2b60ec-manual-signoff.md`
- 本 Mini SDD

## Approach

1. 先增加 manager 回归测试，用两个不同 snapshot 指针模拟 refresh 后重载同一物理容器；锁定无错误、accepted pointer 不变、readiness/activity epoch 不变。
2. 增加反例，确保相同 generation/residency 但不同或无效物理 revision 仍走既有 stale 拒绝。
3. 在 `RenderAssetManager::request_terrain_asset` 的 pointer-equal 快路径中加入“相同已提交物理版本”判定，复用既有幂等 bookkeeping；不进入 `TerrainRenderAsset::accept_snapshot`。
4. 回写 Terrain/Render 长期规格并记录自动与人工验证结论。

## Verification

- `RunTests.bat Debug --test-case="Terrain manager treats a reloaded identical container revision as the same request"`
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `build_editor.bat Debug`
- `build_sandbox.bat Debug`
- `RunArchGate.bat`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `RunRenderGate.bat`
- 修复后由人工分别在 Vulkan/DX12 对相同未变化 Terrain 执行 Reimport，确认无 stale-generation application error；其他未签项目仍按原清单执行。

## Risk / rollback

主要风险是把真实不同内容误判为幂等。缓解条件同时要求相同资产身份、相同 generation/residency、双方有效且完全相同的 `TerrainContainerRevision`；该 revision 是容器 spec 定义的物理提交版本权威。回滚只需移除 manager 等价请求分支与对应规格说明，既有严格 stale 行为即恢复。

## Validation record

- TDD RED：目标测试 15/16 assertions，通过构建但因 stale error 非空而按预期失败。
- TDD GREEN：相同物理 revision 幂等与不同物理 revision 仍 stale 两项，2/2 cases、24/24 assertions。
- `RunTests.bat Debug`：623/623 cases、30,848/30,848 assertions，0 failed（2 skipped）。
- `RunTests.bat Release`：623/623 cases、30,847/30,847 assertions，0 failed（2 skipped）。
- `build_editor.bat Debug`、`build_sandbox.bat Debug`：PASS。
- `RunArchGate.bat`：PASS，35 条均为既有 legacy warnings。
- Standard PerfGate：`20260727-110113-515-39348-fc6504c6`，Editor/Sandbox × Vulkan/DX12 四组合全部 PASS，无 failures/warnings，未 bless baseline。
- 非 bless RenderGate：`20260727-110503-655-57552-f3038e03`，四个 backend/scene smoke、golden SSIM 与跨后端比较全部 PASS，未 bless golden。
- AIDevDoctor：`20260727-030647`，4/4 fresh PASS，PerfGate/logs fresh，无 validation evidence gap；fresh gate 日志未命中 stale-generation、validation/debug-layer、device loss、assertion 或 resource-leak 拒绝模式。
- Editor 设置/config 已恢复到人工验收前四文件 SHA-256 基线。
- 最终人工复测在包含本修复及 Asset Browser catalog-lifetime follow-up 的 commit
  `5f50407f622a82f888cc52aa13d0069732c91fd0` 上执行。用户在 Vulkan Editor 中对
  未变化的 `terrain/ManualB2VulkanRect.AshTerrain` 连续 Reimport 3 次并明确报告
  “均成功且未闪退”；三次成功日志时间为 12:03:41、12:03:48、12:03:52。关联日志
  无 stale-generation application error，且没有新 crash dump。
