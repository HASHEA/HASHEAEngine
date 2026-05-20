# 环境光遮蔽（Ambient Occlusion）设计

日期：2026-05-20

## 决策

在 deferred scene path 中新增一个 Engine 侧环境光遮蔽模块。该模块向 deferred lighting 暴露一个统一的 RenderGraph AO 输入，命名为 `SceneAmbientOcclusion`，并允许通过 `product/config/Engine.ini` 在运行时选择 `Off`、`SSAO`、`HBAO` 或 `GTAO`。

实现单元应遵循当前 pass 命名风格，放在 `project/src/engine/Function/Render` 下，文件名使用 `AmbientOcclusionPass.*`。如果后续实现规模超过 pass 编排和 shader/resource 持有职责，可以再拆内部 helper 文件，但 `SceneRenderer` 只依赖这个 pass facade。

## 目标

- 通过运行时配置支持 `Off`、`SSAO`、`HBAO` 和 `GTAO`。
- 无论选择哪种算法，都让主 deferred graph 只消费一张统一 AO texture。
- 不让算法细节泄漏到 `SceneRenderer` 和 `DeferredLightingPass`。
- 复用当前 GBuffer/depth 数据：D32 scene depth、GBufferE oct normal 和 GBufferB material AO。
- 通过 RenderGraph 资源声明维持 Vulkan / DX12 共享路径合法性，不引入 backend-specific barrier。
- 第一版 GTAO 只做 spatial-only；temporal history、motion-vector reprojection 和 bent normal 明确留到后续阶段。

## 非目标

- 不改 Editor-specific UI 或面板。
- 不新增 backend-specific RHI API。
- 不做 ray-traced AO。
- 第一版不做 temporal accumulation。
- 不改变材质 authoring 语义，只保留 `GBufferB.b` 中已有的 material AO。

## 当前上下文

当前 scene deferred path 为：

```text
SceneGBufferPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredCompositePass
  -> SceneDeferredToneMapPass
```

DeferredHQ GBuffer 已经提供：

- `SceneDeferredDepth`：可采样 D32 depth。
- `GBufferB.b`：材质环境光遮蔽。
- `GBufferE.rg`：oct-encoded normal。

`DeferredCommon.hlsli` 会把 material AO 解码到 `AshDeferredSurface.ao`，并且当前已经把它乘进 dynamic direct lighting。AO 接入后保留这一行为，最终遮蔽按下面公式计算：

```text
finalAO = materialAO * screenAO
```

其中 `screenAO` 来自 `SceneAmbientOcclusion`。当 AO mode 为 `Off` 时，模块返回一张 neutral 1x1 white AO texture，让 lighting path 仍然绑定同一个 shader resource，并表现为 `screenAO == 1`。

## RenderGraph 形态

目标 graph 为：

```text
SceneGBufferPass
  -> SceneAmbientOcclusionPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredCompositePass
  -> SceneDeferredToneMapPass
```

`AmbientOcclusionPass::add_passes()` 负责：

1. 读取 `AmbientOcclusionConfig`。
2. 当 mode 为 `Off` 时返回 external neutral AO texture；启用时创建 graph transient `SceneAmbientOcclusion` texture。
3. 在启用模式下声明读取 scene depth 和 GBufferE。
4. 写一张单通道 AO target；如果当前 `RenderTextureFormat` 尚未暴露合适单通道格式，则先用 RGBA fallback target。
5. 如果 `Blur=true`，额外注册 blur pass。

`DeferredLightingPass::add_passes()` 接收一个有效 AO texture ref。lighting pass 将其声明为 `GraphicsSRV`，绑定给 lighting shaders，并在 `AshDecodeDeferredSurface` 中或 surface decode 之后立即与 material AO 相乘。所有 AO mode 使用同一套 shader binding surface。

## 配置

在 Function/Render 下新增 typed AO config loader，独立于当前 boolean-only 的 `RenderSwitch` 表：

```ini
[AmbientOcclusion]
Mode=GTAO
Quality=Medium
Radius=1.5
Intensity=1.0
Power=1.0
HalfResolution=true
Blur=true
```

支持值：

- `Mode`：`Off`、`SSAO`、`HBAO`、`GTAO`。
- `Quality`：`Low`、`Medium`、`High`。
- `Radius`：world/view-space 采样半径，加载时 clamp 到安全正数范围。
- `Intensity`：遮蔽强度倍率。
- `Power`：最终 AO contrast curve。
- `HalfResolution`：第一版可以先解析该字段，但在 downsample/upsample 支持完成前仍以 full resolution 执行。
- `Blur`：启用 denoise/blur pass。

非法 enum 或数值应记录 warning，并回退到默认值。默认值先使用 `Mode=Off`，等验证和视觉调参稳定后再有意识地切换默认启用策略。

## 模块边界

`AmbientOcclusionPass` 持有：

- render-time 使用的 AO config snapshot。
- SSAO / HBAO / GTAO 的 graphics 或 compute programs。
- `Off` mode 使用的 neutral 1x1 white AO texture。
- 共享 samplers。
- AO output 创建逻辑。
- AO pass 和可选 blur pass 注册逻辑。
- root constants 和 shader binding。

`SceneRenderer` 只负责：

- 初始化和 shutdown `AmbientOcclusionPass`。
- 在 GBuffer 与 deferred lighting 之间调用 `add_passes()`。
- 把返回的 AO texture ref 传给 `DeferredLightingPass`。

`DeferredLightingPass` 只负责：

- 声明 AO SRV 依赖。
- 绑定 AO texture 和 sampler。
- 应用 `materialAO * screenAO`。

`DeferredLightingPass` 不应知道 AO texture 是由哪种算法生成的。

## 算法模式

### SSAO

SSAO 是 baseline 和 fallback mode。它从 depth 重建 position，使用 GBuffer normal，在附近深度上做小 kernel 采样，并根据局部 depth 差异估计遮蔽。

适合用途：

- 第一版视觉验证。
- 低端质量档。
- 调试 AO graph 和 binding path。

预期问题：

- 没有 blur 时噪声明显。
- depth discontinuity 周围容易出现 halo。
- 对 radius 和 bias 参数高度敏感。

### HBAO

HBAO 会沿多个 screen-space 方向做 horizon search。每个方向根据周围 depth profile 抬高到什么程度，估算该方向对半球可见性的遮挡。

适合用途：

- 需要更清晰 crevice response 的 medium/high 质量档。
- 在不引入 temporal history 的情况下获得比 SSAO 更稳定的 contact shadow。

预期问题：

- 成本高于 SSAO。
- 方向数不足时会出现 directional banding。
- 对 depth linearization 和边缘 falloff 比较敏感。

### GTAO

GTAO 用 screen-space slices 和 horizon terms 近似更接近物理意义的半球 visibility integral。第一版应保持 spatial-only，并与 SSAO/HBAO 共享相同输入/输出契约。

适合用途：

- 最高质量模式。
- 作为未来 bent normal 和 indirect-lighting occlusion 的长期基础。

预期问题：

- 实现复杂度最高。
- 需要谨慎 denoise。
- 完整 temporal GTAO 需要 history resources 和 motion vectors，不在本设计第一版范围内。

## Shader 组织

在 `project/src/engine/Shaders/Deferred` 下新增专用 Engine shaders：

- `AmbientOcclusionCommon.hlsli`
- `AmbientOcclusionSSAO.hlsl`
- `AmbientOcclusionHBAO.hlsl`
- `AmbientOcclusionGTAO.hlsl`
- `AmbientOcclusionBlur.hlsl`（当 blur 启用时）

共享 shader 代码应包含：

- UV/depth sampling helpers。
- reverse-Z aware depth background 判断。
- 基于 `AshInvViewProjection` 的 position reconstruction。
- 从 GBufferE 解码 normal。
- AO parameter constants。

第一版三种算法优先使用 raster fullscreen pass，除非 blur 或 half-resolution 支持确实需要 compute。这样可以让 AO pass 路径贴近现有 `DeferredLightingPass` 和 `PostProcessToneMapPass`。

## 资源格式

如果实现时 `RenderTextureFormat` 已暴露合适的单通道 UNORM 格式，则 AO target 优先使用单通道格式。否则第一版使用 `RGBA8_UNORM`，把 AO 存在 `.r`，`.gba` 暂不使用。这样可以避免为了落地 AO 先扩大 RHI format 支持范围。

`SceneAmbientOcclusion` 必须可作为 shader resource 读取。除非选择 compute path，否则不需要 UAV。

## 错误处理

使用项目内的 process-error 风格：

- `ASH_PROCESS_GUARD_RETURN`
- `ASH_PROCESS_ERROR`
- `ASH_LOG_PROCESS_ERROR`，用于需要额外日志上下文的位置

如果 AO config 请求了不可用 mode，或资源创建失败，应让 graph pass 明确失败，而不是静默展示部分错误输出。唯一的静默 neutral path 是显式 `Mode=Off`。

## Profiling 与可调试性

每个 AO graph pass 必须具备：

- 稳定 pass 名：`SceneAmbientOcclusionPass`、`SceneAmbientOcclusionBlurPass`。
- execute lambda 或被委托函数中的 `ASH_PROFILE_SCOPE_NC()`。
- 在可行时记录 algorithm mode、quality、sample count 和 resolution 等有用数值。

Shader 和 RHI debug name 应包含当前 mode，例如 `SceneAmbientOcclusionGTAO`。

## 验证

该功能触及 shared scene rendering、shader binding、RenderGraph resources 和 deferred lighting，因此验证必须覆盖：

- `Sandbox` on Vulkan。
- `Sandbox` on DX12。
- `Editor` on Vulkan。
- `Editor` on DX12。

每个 backend 至少测试：

- `Mode=Off`。
- `Mode=SSAO`。
- `Mode=HBAO`。
- `Mode=GTAO`。

实现验收至少确认：

- RenderGraph compiler 在 lighting 消费 AO 时保留 AO producer。
- Vulkan validation 和 DX12 debug layer 没有 resource-state 或 descriptor 错误。
- 输出不会因为 AO binding 缺失而全黑或全白。
- `finalAO = materialAO * screenAO` 不会错误压暗 unlit / emissive-only shading。
- reverse-Z camera 下 position reconstruction 正确。

## 实现时需要同步更新的文档

当该设计进入实现后，需要更新：

- `README.md`：当前 deferred path 和文档入口。
- `docs/EngineDeveloperGuide.md`：AO config、RenderGraph 位置、shader/binding 行为和验证说明。
- `docs/RenderGraphAPISpec.md`：如果 AO pass 成为文档化 scene path 的一部分，则同步更新示例 graph chain。
