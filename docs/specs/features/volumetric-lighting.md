---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: 体积光（Froxel Volumetric Lighting）

## 行为

- 帧管线位置：deferred lighting / 环境光照 / 天空背景之后、Bloom 之前。输入 `scene_hdr_linear` + 场景深度（`SceneDeferredGraphResources.depth`），输出 `SceneVolumetricCompositeHDR` 并替换下游的 `scene_hdr_linear`。
- `enabled=false` 或 `scattering_intensity<=0` 时直通，不添加任何 pass。
- 主路径（froxel）五个阶段，pass 名依次：
  1. `SceneVolumetricDensityPass`（compute）：按 `density`/`extinction_scale` 填充 froxel 密度。
  2. `SceneVolumetricLightInjectionPass`（compute）：遍历光源 buffer（方向光/点光/聚光，上限 `max_lights`），HG 相函数（`anisotropy`，钳制 ±0.95）注入散射；sunlight CSM 可用时采样其级联阴影。
  3. `SceneVolumetricTemporalPass`（compute）：用上一帧 view-projection 重投影历史散射，按 `history_blend` 混合，输出 history validity。
  4. `SceneVolumetricIntegratePass`（compute）：沿视线累积散射/消光，得到全分辨率 integrated lighting。
  5. `SceneVolumetricCompositePass`（raster 全屏）：与场景 HDR 合成。
- 屏幕空间 fallback：`screen_space_fallback=true` 时**完全替换** froxel 路径，仅跑 `SceneLightShaftScreenSpacePass`——朝屏幕空间光源位置 16 步径向采样，背景深度作可见性，输出 occlusion mask + `SceneLightShaftScreenSpaceCompositeHDR`。

## 配置

`scene_config.volumetric_lighting`（解析见 scene-config spec，`sanitize_volumetric_lighting_config` 兜底）：
`enabled`(false)、`quality`(Medium，Low/Medium/High/Epic)、`froxel_resolution_scale`(0.5)、`froxel_depth_slices`(64)、`max_lights`(64)、`density`(0.02)、`scattering_intensity`(1.0)、`extinction_scale`(1.0)、`anisotropy`(0.35)、`history`(true)、`history_blend`(0.9)、`screen_space_fallback`(false)、`debug_view`(Off，可选 Density/Scattering/IntegratedLighting/HistoryValidity/CompositeHDR/ScreenSpaceMask/ScreenSpaceFinal，选中时替换输出)。

## 实现

- 类：`VolumetricLightingPass` / `VolumetricLightingConfig`（`project/src/engine/Function/Render/VolumetricLightingPass.{h,cpp}`、`VolumetricLightingConfig.{h,cpp}`）。
- Shader（`project/src/engine/Shaders/Deferred/`）：`VolumetricDensity.hlsl`、`VolumetricLightInjection.hlsl`、`VolumetricTemporal.hlsl`、`VolumetricIntegrate.hlsl`、`VolumetricComposite.hlsl`、`LightShaftScreenSpace.hlsl`，公共 `VolumetricLightingCommon.hlsli`（注入阶段另含 `Shaders/Shadow/DirectionalShadowCommon.hlsli`）。
- Froxel 存储：2D atlas（RGBA32_SFLOAT UAV），tile 分辨率 = 输出 × `froxel_resolution_scale`，`slices_per_row = ceil(sqrt(depth_slices))` 平铺；总 froxel 数按 quality 预算钳制（Low 512K / Medium 1M / High 2M / Epic 4M），超预算时等比缩 tile。
- 深度 slice 分布为**幂律**（`kVolumetricSliceDistributionPower = 2.0`，`pow(t, 2)`）：近处密、远处疏，消除等距分布的 depth-slice banding。slice↔depth01 换算在注入/时域/积分三阶段必须一致（`AshVolumetricSliceDepth01` 等函数为唯一来源）。
- 历史：按 view key 双缓冲 scattering RT（`VolumetricHistoryEntry`），记录相机/revision 用于失效判断；`clear_history()` 由 SceneRenderer 场景切换时调用。

## 约束与已知限制

- froxel 中间纹理挂在 RenderGraph 上逐帧创建；仅历史 RT 跨帧持有。
- 时域重投影依赖 `history` 开关与相机连续性，剧烈切换视角时靠 history validity 回退到当前帧。
- 屏幕空间 fallback 只处理"朝光源径向 shaft"，无真实介质参与，仅作低端路径。

## 验证

- `RunRenderGate.bat`（双后端 golden SSIM + 跨后端 diff）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView 定位：`SceneVolumetricCompositeHDR`、`SceneVolumetricIntegratedLighting`、`SceneVolumetricScattering*`、`LightShaftScreenSpaceCompositeHDR`（LinearHDR 可视化）。

## 历史

- docs/superpowers/specs/2026-06-05-volumetric-lighting-design.md
- docs/superpowers/plans/2026-06-05-volumetric-lighting-implementation.md
