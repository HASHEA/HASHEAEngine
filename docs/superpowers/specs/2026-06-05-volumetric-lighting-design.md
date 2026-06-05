# Volumetric Lighting 子系统设计

## 背景

当前 AshEngine 的 deferred 主链路已经具备 GBuffer、AO、directional / point / spot 延迟光照、环境光、sky/background、Bloom 和独立 tone-map。最初需求是添加 lightshaft；最终范围明确为直接建设完整的 Volumetric Lighting / Participating Media 子系统，同时保留 screen-space lightshaft 作为低成本 fallback 和调试对照路径。

这个设计只涉及 Engine 侧 `Function/Render`、`Function/Scene` 和 shader 资源，不修改 `project/src/editor`，也不向 Editor / Game 暴露 Vulkan、DX12 或其他 RHI 后端类型。所有资源状态转换应继续通过 RenderGraph pass 边界表达，避免在 Vulkan render pass / dynamic rendering 活跃区间内插入非法 barrier。

## 目标

- 增加 scene-owned 的体积光配置，随 `Scene -> RenderScene -> VisibleRenderFrame -> SceneRenderer` 快照传递。
- 新增 Engine 侧 Volumetric Lighting 渲染子系统，使用 frustum-aligned froxel volume 表达全局 participating media。
- 第一版支持 directional、point、spot 三类灯的体积散射贡献。
- directional 体积光接入已有 directional shadow 输出；point / spot 第一版先做未阴影体积光，等待点光 / 聚光阴影系统成熟后接入同一 shadow provider。
- 在 HDR 线性空间中合成体积光，让 Bloom 和 tone-map 自然消费结果。
- 提供 screen-space lightshaft fallback，用于低成本路径、性能对比和调试。
- 提供 RenderDebugView 可视化项和 Tracy instrumentation，方便定位质量、性能和资源问题。
- 保持 Vulkan / DX12 共享实现，不引入 backend-specific 上层接口。

## 非目标

- 第一版不建设完整 weather / fog volume authoring 系统。
- 第一版不添加 Editor 面板、属性编辑器或可视化 gizmo。
- 第一版不新增 point / spot shadow map 或 VSM；它们之后作为独立阴影能力接入体积光。
- 第一版不实现局部体积 primitive、材质驱动体积介质或 asset cooking。
- 第一版不改变现有 surface lighting、Bloom、tone-map 的公共契约。

## 配置契约

新增 `VolumetricLightingConfig.*`，并将其作为 `SceneRenderConfig` 字段序列化到 scene JSON 顶层 `scene_config.volumetric_lighting`。配置不进入 `Engine.ini`；`Engine.ini` 仍只负责 RHI、validation、VSync 和进程级诊断配置。

默认配置关闭，旧场景缺失该字段时保持当前外观不变：

```json
"volumetric_lighting": {
  "enabled": false,
  "quality": "Medium",
  "froxel_resolution_scale": 0.5,
  "froxel_depth_slices": 64,
  "max_lights": 64,
  "density": 0.02,
  "scattering_intensity": 1.0,
  "extinction_scale": 1.0,
  "anisotropy": 0.35,
  "history": true,
  "history_blend": 0.9,
  "screen_space_fallback": false,
  "debug_view": "Off"
}
```

`quality` 提供一组默认预算，例如 `Low`、`Medium`、`High`、`Epic`。显式字段覆盖 quality 默认值。`froxel_resolution_scale` 限制在安全范围内，例如 `0.25` 到 `1.0`；`froxel_depth_slices` 限制在合理整数范围内，例如 `16` 到 `128`；`max_lights` 限制为体积光 shader buffer 的固定上限；`anisotropy` clamp 到 Henyey-Greenstein 相函数稳定范围；`history_blend` clamp 到 `[0, 0.98]`，避免历史残影无法收敛。

`debug_view` 至少包含：

- `Off`
- `Density`
- `Scattering`
- `IntegratedLighting`
- `HistoryValidity`
- `CompositeHDR`
- `ScreenSpaceMask`
- `ScreenSpaceFinal`

## 架构

### C++ 模块

`VolumetricLightingConfig.h/.cpp`

- 定义 `VolumetricLightingQuality`、`VolumetricLightingDebugView` 和 `VolumetricLightingConfig`。
- 提供 `make_default_volumetric_lighting_config()`。
- 提供 `sanitize_volumetric_lighting_config()`。
- 提供 quality / debug view 的 name 和 parse helper。

`SceneConfig.h/.cpp`

- 将 `VolumetricLightingConfig volumetric_lighting` 加入 `SceneRenderConfig`。
- equality 需要比较体积光所有影响渲染输出和 pass 拓扑的字段。
- 默认构造、sanitize 和 `Scene::set_render_config()` 要包含该字段。

`Scene.cpp`

- `deserialize_scene_render_config()` 读取 `scene_config.volumetric_lighting`。
- `serialize_scene_render_config()` 写出完整配置。
- 缺失字段使用默认值；非法字段只记录 warning 并走 sanitize 后结果。

`VolumetricLightingPass.h/.cpp`

- 持有 shader program、sampler、history texture、light buffer 和 frame-local constants。
- 对外只暴露 `initialize()`、`shutdown()`、`add_passes()`。
- `add_passes()` 输入 `RenderGraphBuilder`、`VisibleRenderFrame`、`SceneDeferredGraphResources`、输入 HDR texture 和 `SceneRenderViewContext`，输出新的 HDR texture 以及 debug texture refs。
- pass 内部不向外暴露 froxel 资源、RHI resource 或 backend-native 类型。

`SceneRenderer.h/.cpp`

- 持有 `VolumetricLightingPass m_volumetric_lighting_pass`。
- 初始化和 shutdown 顺序与 Bloom / AO / shadow pass 保持一致。
- 在 sky/background 后、Bloom 前调用体积光 pass。
- 将体积光 debug 输出注册到 `RenderDebugView`。

### Shader 模块

`VolumetricLightingCommon.hlsli`

- froxel 坐标转换。
- view-space / world-space reconstruction。
- depth slice mapping。
- Henyey-Greenstein phase function。
- directional / point / spot attenuation。
- screen-space fallback 的 fullscreen helper。

`VolumetricDensity.hlsl`

- 生成或清理 density / extinction volume。
- 第一版使用全局均匀介质，按配置控制 density 和 extinction。

`VolumetricLightInjection.hlsl`

- 将 visible lights 注入 froxel scattering volume。
- 支持 directional、point、spot。
- directional 读取已有 directional shadow mask 或 shadow data provider 时，按当前 view / cascade 信息衰减散射。

`VolumetricTemporal.hlsl`

- 可选历史重投影和 history validity。
- 使用当前 / 上一帧 view-projection、depth 和 froxel 参数判断历史是否可复用。

`VolumetricIntegrate.hlsl`

- 沿 view ray 从相机向远处积分 scattering / transmittance。
- 输出可供 composite 的 integrated lighting texture。

`VolumetricComposite.hlsl`

- 在 HDR 线性空间合成体积光。
- 输出 `SceneVolumetricCompositeHDR`。

`LightShaftScreenSpace.hlsl`

- 作为 fallback，使用 depth occlusion mask + radial blur。
- 第一版主要面向最强 directional / sunlight。

## RenderGraph 数据流

体积光主路径插入当前 deferred 链路：

```text
GBuffer + Depth
Ambient Occlusion
Directional / Point / Spot Deferred Lighting
Environment Lighting
Deferred Composite -> SceneDeferredSceneHDRLinear
SkyBackground -> SceneDeferredSceneHDRWithSky
VolumetricLightingPass -> SceneVolumetricCompositeHDR
Bloom
ToneMap
RenderDebugView / Overlays
```

`VolumetricLightingPass` 内部 pass 顺序：

```text
SceneDepth + View constants
  -> SceneVolumetricDensity

SceneVolumetricDensity + VisibleLightBuffer + optional directional shadow data
  -> SceneVolumetricScattering

SceneVolumetricScattering + previous history + depth
  -> SceneVolumetricScatteringTemporal

SceneVolumetricScatteringTemporal
  -> SceneVolumetricIntegratedLighting

Input HDR + SceneVolumetricIntegratedLighting
  -> SceneVolumetricCompositeHDR
```

如果 `screen_space_fallback=true`，pass 顺序替换为：

```text
SceneDepth + strongest directional light
  -> SceneLightShaftScreenSpaceMask

SceneLightShaftScreenSpaceMask + Input HDR
  -> SceneLightShaftScreenSpaceCompositeHDR
```

所有中间资源为 RenderGraph transient texture 或 buffer。资源名必须稳定，便于 RenderDoc、RenderDebugView 和 self-test 合同检查。

## Light 与 Shadow 数据

第一版直接消费 `VisibleRenderFrame::lights`。为了避免每个 shader 自己解释 CPU 结构，`VolumetricLightingPass` 会构建一个专用 GPU light buffer，字段只包含体积光需要的数据：

- light type
- color / intensity
- world position / range
- world direction
- spot cone cosines
- casts_shadow / sunlight 标志
- frame light index

directional shadowing 复用已有 directional shadow pass 输出。由于当前 sunlight 与普通 directional shadow 已经存在不同 atlas / mask 路径，体积光 pass 不应硬编码具体实现细节，而应在 `SceneDeferredGraphResources` 中通过明确字段接收当前帧可用的 directional shadow resources。缺失 shadow resource 时，该 light 按 unshadowed volumetric contribution 处理并记录 debug counter，而不是让整帧失败。

point / spot 第一版不带 shadow map 衰减。未来 point / spot shadow 或 VSM 接入后，通过同一 volumetric shadow provider 扩展，不改变 scene config 和 `SceneRenderer` 调用点。

## Temporal History

history 开启时，`VolumetricLightingPass` 维护每个 temporal view key 的 history texture。history 资源不进入 RenderGraph transient pool，而是 pass 持有的外部 texture，按 current / previous slot 轮换注册到 graph。

history validity 使用以下条件：

- view size / froxel dimensions 匹配。
- depth slice 参数匹配。
- camera matrices 可用于重投影。
- 当前 depth 与历史 depth 差异在阈值内。

history 失效时直接使用当前 scattering volume，并刷新 history。配置关闭 history 时，不创建 temporal pass。

## Debug 与 Profiling

新增或修改的 render pass 都使用 `Base/hprofiler.h` facade 打点，不在 public header include Tracy。建议 profiling 名称：

- `VolumetricLightingPass::initialize`
- `VolumetricLightingPass::add_passes`
- `SceneVolumetricDensityPass`
- `SceneVolumetricLightInjectionPass`
- `SceneVolumetricTemporalPass`
- `SceneVolumetricIntegratePass`
- `SceneVolumetricCompositePass`
- `SceneLightShaftScreenSpacePass`

RenderDebugView 注册项：

- `SceneVolumetricDensity`
- `SceneVolumetricScattering`
- `SceneVolumetricIntegratedLighting`
- `SceneVolumetricCompositeHDR`
- `SceneVolumetricHistoryValidity`
- `SceneLightShaftScreenSpaceMask`
- `SceneLightShaftScreenSpaceFinal`

可视化格式按资源语义选择 `Scalar`、`Color` 或 `LinearHDR`。

## 错误处理

- 配置关闭时，`add_passes()` 返回输入 HDR texture，不创建任何 pass。
- 配置 sanitize 后仍无法满足最小资源尺寸时，记录 warning 并返回输入 HDR。
- 初始化期 shader program / sampler / persistent history 资源创建失败时，`initialize()` 返回 false，遵循现有 renderer 初始化失败路径。
- 某帧缺失可选 shadow resource 时，不让 pass 失败；对应 light 走 unshadowed 体积贡献。
- 必需资源缺失，例如 depth 或输入 HDR 缺失，使用 `ASH_PROCESS_ERROR` 让该 frame path 失败并进入现有错误日志。
- 函数保持项目现有 process-error 风格，避免散落直接早退。

## 测试策略

Headless self-test 增加合同测试：

- `VolumetricLightingConfig` 默认关闭。
- quality / debug view 字符串解析正确。
- sanitize clamp `froxel_resolution_scale`、`froxel_depth_slices`、`max_lights`、`density`、`anisotropy`、`history_blend`。
- `SceneRenderConfig` equality 包含 volumetric lighting。
- `Scene::set_render_config()` 只递增 render config version，不影响 primitive / transform / light / environment version。
- scene JSON 缺失 `volumetric_lighting` 时使用默认值。
- scene JSON load/save round-trip 保留 sanitized volumetric config。
- `RenderScene` 会把 volumetric config 复制到 light-only frame 和 full visible frame。
- `Engine.ini` 不包含 `[VolumetricLighting]`。
- shader source contract 包含预期 entry point、binding 名和 root constants。
- `VolumetricLightingPass` source contract 包含稳定 pass 名和 RenderGraph resource 名。
- `SceneRenderer` 集成顺序为 sky/background 后、Bloom 前、tone-map 前。

运行验证按共享渲染路径执行：

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

标准 `Sandbox.scene.json` 可显式开启 `volumetric_lighting` 的 Medium 配置，覆盖真实 runtime path。若性能成本过高，可在标准场景中使用 Low 配置，但必须保持默认旧场景关闭。

## 文档更新

实现完成后同步更新：

- 根目录 `README.md`：当前状态、渲染能力、标准 Sandbox scene 配置。
- `docs/EngineDeveloperGuide.md`：Scene render config、deferred render path、debug/validation/self-test 覆盖。
- 如 RenderGraph API 或 debug view 注册约定变化，再更新对应专题文档。

## 分阶段实现建议

第一阶段：配置与合同

- 新增 config 类型、scene JSON round-trip、RenderScene snapshot。
- 添加 self-test，先看失败，再实现。

第二阶段：RenderGraph 骨架

- 新增 `VolumetricLightingPass`，资源名、pass 名、debug 注册和 no-op/disabled path 先跑通。
- 不改变默认画面。

第三阶段：froxel density 与 light injection

- 实现全局均匀介质。
- 支持 directional / point / spot 未阴影体积贡献。

第四阶段：integrate / composite / debug view

- 合成到 HDR，放在 Bloom 前。
- 注册 RenderDebugView 项。

第五阶段：directional shadow 与 temporal history

- 接入已有 directional shadow resources。
- 增加 history texture、reprojection 和 validity。

第六阶段：screen-space fallback

- 实现 depth mask + radial blur fallback。
- 通过配置切换，默认不启用。

第七阶段：文档和完整验证

- 更新 README 与 EngineDeveloperGuide。
- 执行 Sandbox / Editor 的 Vulkan / DX12 验证矩阵。

## 已确认决策

- 主方向是完整 Volumetric Lighting 子系统，不是单独 lightshaft pass。
- 第一版支持 directional、point、spot 三类灯。
- screen-space lightshaft 保留为 fallback/debug path。
- scene JSON 是体积光配置权威来源，`Engine.ini` 不承载该配置。
- 渲染集成点为 sky/background 后、Bloom 前、tone-map 前。
- 不修改 Editor 代码。
