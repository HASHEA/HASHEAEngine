# Mini SDD: TAA debug_view 接入 RenderDebugView overlay

级别：S1（单模块，`Function/Render/SceneRenderer.cpp` 单点接线 + 文档）

## Goal

TAA 的三个 debug 模式（MotionVectors/HistoryWeight/Variance）目前只能改 scene
JSON（`temporal_aa.debug_view`）静态开启，且开启后直接污染主输出无 UI 开关；
而 RenderDebugView overlay 已有成熟的运行时下拉选择。目标：三个模式成为
RenderDebugView 可选条目，运行时切换、不改 scene 资产。

排查确认其余 feature 无需接线：Bloom（Setup/Mip1-6/Final/CompositeHDR）与
Volumetric（Density/…/ScreenSpaceFinal）的 debug 枚举各模式均已是 overlay 里
注册过的**中间纹理**条目；只有 TAA 的三个模式是"计算型"编码——resolve shader
按配置把可视化写进输出，不开配置就不存在对应纹理。

## Non-goals

- 不改 TemporalAAResolve.hlsl / TemporalAAPass（shader 已支持三模式且历史写入
  与 debug 输出隔离）。
- 不动 scene JSON 的 `temporal_aa.debug_view` 语义（仍作为静态默认值生效）。
- 不清理 Bloom/Volumetric 的冗余 debug_view 枚举（另行任务）。
- 不给 RenderDebugView 增加新 visualization 模式（debug 值已是编码后的可显示值，
  Color 直通即可）。

## Approach

SceneRenderer 内固定表驱动的"选择→配置覆盖"：

1. 定义常量表：`{ "SceneTemporalAADebugMotionVectors", "Temporal AA Debug:
   Motion Vectors", TemporalAADebugView::MotionVectors }` 等三条。
2. `add_passes` 前读 `get_runtime_render_debug_view_config()`：enabled 且
   selected 命中表内条目时，拷贝 `frame.render_config.temporal_aa` 并覆盖
   `debug_view` 为对应模式（未命中则保持 scene 配置原值），以有效配置调用
   `m_taa_pass.add_passes`。
3. `taa_outputs.resolved` 有效时按表注册三个条目，纹理均指向 resolved
   （visualization=Color：debug 值已编码，直通显示）。

语义自洽：debug 输出写入 resolve 输出会经 tonemap 进主画面，但 overlay pass
全屏覆盖最终输出，被污染的主画面不可见；历史缓冲由 shader 保证永远写干净
resolved 色，切换/关闭无残留。选择变更经 UI 下拉 → runtime config，次帧生效。

## Files

- `project/src/engine/Function/Render/SceneRenderer.cpp`：常量表 + 配置覆盖 +
  三条注册。
- `docs/specs/features/taa.md`：验证节改写（debug_view 可经 overlay 运行时切换）。
- `docs/specs/features/render-debug-view.md`：补记"feature 计算型 debug 条目"
  语义（选中即驱动 feature 配置覆盖，TAA 为首个消费者）。

## Verification

1. `build_editor.bat Debug` + `build_sandbox.bat Debug`。
2. Sandbox（TAA 开启）：overlay 下拉出现三个 TAA Debug 条目，逐个切换画面
   符合预期（运动向量/历史权重/方差），切回 Off 主画面正常无残留。
3. 双后端 smoke 日志无 validation 报错。
4. `RunRenderGate.bat`：Selected=Off 时零覆盖，golden 必须不变。

## Risk / rollback

- 风险：注册条目在 TAA 关闭时消失——overlay 对未注册选择本就静默 no-op
  （spec 既有行为），无需特判。
- 回滚：SceneRenderer 单点改动直接 revert。

## Status

Done（2026-07-06）

- `build_editor.bat Debug` / `build_sandbox.bat Debug` PASS。
- 画面验证（`--dump-frame` 抓帧自查，双后端）：`Selected` 依次设三个条目，
  MotionVectors 全黑（静态场景+抓帧禁 jitter → 运动 0、全屏 temporal 有效）、
  HistoryWeight 全屏 ≈0.9 灰白（收敛态混合权重）、Variance 高亮几何/纹理边缘；
  DX12 Variance 与 Vulkan 一致。切回 Off 主画面正常。UI 下拉列出注册项为
  RenderDebugView 既有机制（本改动仅新增注册项），未单独点验。
- `RunRenderGate.bat` PASS（vulkan 0.999997 / dx12 0.999669 / 跨后端 0.999516，
  golden 未变），Engine.ini 已还原基线。
- 结论回写 `docs/specs/features/render-debug-view.md`（计算型条目语义）、
  `docs/specs/features/taa.md`（验证节）。
