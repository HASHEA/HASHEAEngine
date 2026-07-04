# Feature Spec: Deferred Lighting（GBuffer + 延迟光照子 pass 族）

## 行为

SceneRenderer 每帧先以 `SceneGBufferPass` 将可见静态网格写入 GBuffer + 深度，随后按顺序执行：
AO → sunlight/方向光 shadow → 光照累积（Base + 逐光源）→ 环境光照（IBL，另见 skybox-ibl spec）→ Composite。
Composite 产出 `SceneDeferredSceneHDRLinear`（RGBA16F），下游依次为 SkyBackground、体积光、Bloom、TAA、ToneMap。

光照累积使用双 MRT：`SceneDeferredLightingDiffuse` / `SceneDeferredLightingSpecular`（均 RGBA16F），
Base pass Clear 后各光源 pass 以 Load + Additive 叠加，Composite 输出 diffuse + specular。

### GBuffer 布局（DeferredHQ）

由 `get_deferred_hq_gbuffer_layout()` 定义（`GBufferLayout.h/.cpp`），layout key 的 feature flags 为
`DeferredLighting | TemporalMotionVector3D | ExtendedMaterialData | HDRMaterialEmission`，quality_tier=1。
5 个颜色附件 + D32 深度，语义通过 `GBufferSemanticMapping`（attachment_index + component_mask）寻址：

| 附件 | 格式 | 语义 |
| --- | --- | --- |
| GBufferA | RGBA8_UNORM | BaseColor.rgb + ShadingModelAndFlags.a |
| GBufferB | RGBA8_UNORM | Metallic / Roughness / AO / Specular（rgba） |
| GBufferC | RGBA8_UNORM | CustomData（rgba） |
| GBufferD | RGBA16_SFLOAT | MotionVector3D.rgb + TemporalFlags.a |
| GBufferE | RGBA16_SFLOAT | NormalOct.rg + EmissiveOrCustom.ba（HDR 自发光） |

深度为 `SceneDeferredDepth`（D32_SFLOAT）。

## 配置

无独立配置节；光源来自场景（`VisibleRenderFrame::lights`），阴影/AO 输入由各自 feature 的
scene_config 节控制（见 shadows / ambient-occlusion spec）。

## 实现

pass 类：`DeferredLightingPass`（`project/src/engine/Function/Render/DeferredLightingPass.h/.cpp`），
公共 shader include `Shaders/Deferred/DeferredCommon.hlsli`。子 pass 族：

| RenderGraph pass | shader（`project/src/engine/Shaders/Deferred/`） | 说明 |
| --- | --- | --- |
| SceneDeferredLightingBasePass | DeferredBaseEmissive.hlsl | 全屏三角形；Clear 累积 MRT，写入自发光基础项（split diffuse/specular） |
| SceneDeferredDirectionalLightingPass_\<i\> | DeferredDirectionalLighting.hlsl | 全屏，无阴影方向光 |
| SceneDeferredDirectionalLightingShadowedPass_\<i\> | DeferredDirectionalLightingShadowed.hlsl | 额外采样 `SceneDirectionalShadowMask` |
| SceneDeferredPointLightingPass_\<i\> | DeferredPointLighting.hlsl | 内建球体积网格逐光源绘制 |
| SceneDeferredSpotLightingPass_\<i\> | DeferredSpotLighting.hlsl | 内建圆锥体积网格逐光源绘制 |
| SceneDeferredCompositePass | DeferredComposite.hlsl | scene_hdr_linear = diffuse + specular |

所有光照 pass 采样 GBufferA–E、SceneDepth 与 `SceneAmbientOcclusion`（AO feature 输出；Off 时为 1x1 中性白）。
资源经 `SceneDeferredGraphResources` 在 pass 间传递。

## 约束与已知限制

- 仅静态网格走 GBuffer 路径（`render_static_meshes_to_pass`，PassFamily::GBuffer）。
- 逐光源独立全屏/体积 pass，无 light culling / clustered 优化。
- GBuffer 布局当前只有 DeferredHQ 一种；`find_gbuffer_semantic_mapping` 供按语义定位。

## 验证

- `RunRenderGate.bat`（双后端 golden SSIM ≥ 0.995，跨后端 diff ≥ 0.99）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView：`GBuffer A/B/C`、`GBuffer D MotionVector`、`GBuffer E Normal`、`Depth`、
  `Deferred Lighting Diffuse`、`Deferred Lighting Specular`、`Scene HDR Linear` 分通道定位。

## 历史

- `docs/superpowers/specs/2026-05-12-deferred-gbuffer-design.md`
- `docs/superpowers/specs/2026-05-12-deferred-lighting-design.md`
