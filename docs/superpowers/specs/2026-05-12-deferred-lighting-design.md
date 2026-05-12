# Deferred Lighting 模块设计

## 目标

在现有 DeferredHQ GBuffer 路径之上，建立第一版真正的延迟光照模块。第一版支持现有 `LightComponent` 中的平行光、点光源和聚光灯；面光源只作为后续阶段预留。

这个模块应该把光照计算从材质 host shader 中移到 Engine 自己的 deferred lighting shader 中，同时保持材质边界不变：用户材质 shader 只描述表面数据，不需要知道当前渲染路径是前向还是延迟。

## 非目标

- 第一阶段不实现面光源着色。
- 第一阶段不实现阴影、IES、cookie、体积光、tiled deferred、clustered lighting 或 Lumen 风格 GI。
- 不把 GBuffer MRT 槽位或 lighting pass 内部细节暴露给用户材质 shader。
- 不移除前向路径。前向路径继续作为 fallback，也作为后续透明材质路径。

## 当前上下文

`LightComponent` 已经暴露了本阶段需要的前三类光源：

```cpp
enum class LightType : uint8_t
{
    Directional = 0,
    Point,
    Spot
};

struct LightComponent
{
    LightType type = LightType::Directional;
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
    float inner_cone_angle_degrees = 30.0f;
    float outer_cone_angle_degrees = 45.0f;
};
```

`MaterialShadingModel` 已经存在，但目前只有 `DefaultLit`。延迟光照应该扩展这个 enum，而不是额外引入一套 renderer-only 的 shading model 类型。

当前 DeferredHQ layout 已经把 `GBufferA.a` 预留给 `ShadingModelId/Flags`，但第一版 GBuffer shader 写的是 0。在接入真实光照前必须修正这一点，因为背景像素和有效材质像素需要可区分的 ID。

## 第一阶段架构

渲染流变为：

```text
ScenePresentationSubsystem
  -> VisibleRenderFrame
  -> VisibleRenderLightFrame

SceneRenderer
  -> SceneGBufferPass
  -> SceneLightingAccum clear/background
  -> Directional light pass
  -> Point light volume passes
  -> Spot light volume passes
  -> SceneLightingComposite pass to view output
```

`DeferredLightingPass` 应该从当前一次性的 fullscreen resolve，扩展为 Engine 的光照模块。它负责持有 engine programs、共享 light volume mesh、sampler，以及 lighting pass 需要的逐帧 light buffer。

第一版建议使用 graphics pass，不走 compute。等 light data、GBuffer decode 和 shading model contract 稳定后，再把 tiled 或 clustered lighting 作为后续优化路径。

## 光源数据流

在当前 visible mesh frame 旁边新增一个 render-thread light snapshot。这个 snapshot 从逻辑 `Scene` 构建时只做 CPU 数据整理，然后由 `SceneRenderer` 在 render thread 消费。

建议的渲染侧数据结构：

```cpp
struct SceneLightData
{
    LightType type;
    glm::vec3 position_ws;
    glm::vec3 direction_ws;
    glm::vec3 color;
    float intensity;
    float range;
    float inner_cone_cos;
    float outer_cone_cos;
};
```

规则：

- 平行光使用 entity rotation 得到 `direction_ws`；position 和 range 忽略。
- 点光源使用 entity translation 和 `range`。
- 聚光灯使用 entity translation、entity forward direction、`range` 和 cone angles。
- 无效或禁用的光源应该在 render submit 前过滤掉。
- 第一阶段 light unit 可以先保持 engine-relative；后续引入物理单位时不需要改变 pass 结构。

## 光照累积目标

不要直接把光照写进最终 view output。新增一个可复用的 `SceneLightingAccum` render target：

- 格式：`RGBA16_SFLOAT`。
- 尺寸：当前 view output 尺寸。
- Clear value：scene/view clear color，不是 0。
- Usage：render target + shader resource。

lighting pass 以 additive blend 写入 `SceneLightingAccum`。最后用 composite pass 写到 `SceneRenderViewContext.output_target`。

这样能解决两个直接问题：

- 背景像素保留 scene clear color，不再被当成黑色或非法 GBuffer 材质数据。
- HDR lighting、tone mapping、bloom、exposure、lighting debug view 和后续 post effect 都有干净的插入点。

## GBuffer Contract 调整

延迟光照依赖更严格的 GBuffer contract：

| 字段 | 要求 |
| --- | --- |
| `GBufferA.rgb` | BaseColor |
| `GBufferA.a` | 打包的 `ShadingModelId/Flags`；`0` 表示 empty/background |
| `GBufferB.rgba` | Metallic、Roughness、AO、SpecularScalar |
| `GBufferC.rgb` | SpecularColor/F0 或 shading-model custom data |
| `GBufferC.a` | custom data 或 future material flag |
| `GBufferD.xyz` | MotionVector3D；temporal history 落地前继续写 0 |
| `GBufferD.a` | Temporal flags |
| `GBufferE.rg` | Octahedral normal |
| `GBufferE.ba` | 临时 emissive/custom payload |
| Depth | depth reconstruction source；`depth >= 1.0` 表示 background |

第一版 lighting pass 应该在以下任一条件成立时把像素视为背景：

- depth 是 clear value，或
- `ShadingModelId == Empty`。

两个检查都保留是有意的 defense in depth。Depth 负责处理 clear pixel；shading model ID 负责后续 stencil、sky 或 custom background 路径。

## Shading Model

扩展 `MaterialShadingModel`：

```cpp
enum class MaterialShadingModel : uint8_t
{
    Empty = 0,
    DefaultLitGGX = 1,
    Unlit = 2,
    BlinnPhong = 3
};
```

兼容规则：已有 material JSON 中的 `"DefaultLit"` 映射到 `DefaultLitGGX`。

推荐语义：

- `DefaultLitGGX`：主生产路径，Cook-Torrance BRDF，使用 GGX NDF、Smith geometry term 和 Schlick Fresnel。
- `Unlit`：输出 base color + emissive，忽略动态光。
- `BlinnPhong`：兼容/调试路径，用于验证 light volume，也方便和更简单的光照模型对比。

用户材质 shader 不应该直接选择 BRDF 代码。它只暴露材质属性和静态 `shading_model`；Engine GBuffer host shader 负责写入对应的 shading model ID。

## 平行光

平行光用 fullscreen additive pass。

第一阶段可以选择：

- 每个平行光画一次 fullscreen draw，或
- 一个 fullscreen draw 内 loop 一个小的 directional-light buffer。

推荐一个 fullscreen draw loop 小的 directional-light buffer。平行光通常数量很少，这样能减少 pass churn，也能保持 shader 简洁。

shader 从 GBuffer/depth 重建 position 和 normal，跳过背景像素，根据 shading model 分支求值，然后把结果加到 `SceneLightingAccum`。

## 点光源

点光源使用经典 deferred sphere volume。

实现方案：

- 新增或复用一个由 `DeferredLightingPass` 持有的单位球 mesh。
- 按 light `range` 缩放 sphere radius。
- 第一阶段如果 instancing 不方便，可以每个 light 一次 draw；否则一个 instance 对应一个可见点光源。
- Pixel shader 从 depth 重建 world position，并 reject range 外像素。
- 使用带 range 截断的平滑 inverse-square-inspired attenuation。

推荐第一阶段 attenuation：

```hlsl
float distance_ratio = saturate(distance / range);
float smooth_range = saturate(1.0 - distance_ratio * distance_ratio);
float attenuation = smooth_range * smooth_range / max(distance * distance, 0.01);
```

这套公式稳定、视觉上足够合理，同时还不需要立刻承诺物理单位。

## 聚光灯

聚光灯建议使用 cone volume，不走 fullscreen，也不走 sphere。

实现方案：

- 新增一个由 `DeferredLightingPass` 持有的单位 cone mesh。
- cone length 按 `range` 缩放。
- cone radius 按 `tan(outer_cone_angle) * range` 缩放。
- cone 方向使用 light entity forward direction。
- Pixel shader 从 depth 重建 world position，并 reject range 外和 outer cone 外的像素。
- 使用 inner 到 outer cone 的 smooth falloff。

推荐 cone falloff：

```hlsl
float cos_angle = dot(light_direction_ws, normalize(world_pos - light_pos_ws));
float cone = saturate((cos_angle - outer_cone_cos) / max(inner_cone_cos - outer_cone_cos, 1e-4));
float cone_falloff = cone * cone;
```

聚光灯和点光源一样，第一阶段就应该使用 hardware depth test 来减少无效 light volume 像素，但不依赖 depth test 单独决定光照范围；shader 中仍然必须基于重建的 world position 做 range/cone reject。Stencil、depth bounds、scissor 等进一步优化可以后置。

## 面光源

只有在准备好定义 shape data 后，再添加 `LightType::Area`。真正的 area light component 不只需要当前这些 scalar 字段，还需要：

- shape type，例如 rectangle、disk、tube 或 sphere
- size dimensions
- photometric 或 engine-relative intensity policy
- 可选的 two-sided emission

第一阶段 runtime shading 不应该出现 area light 分支；文档中只说明它是预留项。

## Shader 组织

把当前 `DeferredLighting.hlsl` 拆成 engine-owned include 和 pass entry：

```text
Shaders/Deferred/DeferredGBufferDecode.hlsli
Shaders/Deferred/DeferredBRDF.hlsli
Shaders/Deferred/DeferredLightingCommon.hlsli
Shaders/Deferred/DeferredDirectionalLighting.hlsl
Shaders/Deferred/DeferredPointLighting.hlsl
Shaders/Deferred/DeferredSpotLighting.hlsl
Shaders/Deferred/DeferredComposite.hlsl
```

这样拆分有直接收益：point 和 spot volume shader 共用 GBuffer decode 和 BRDF，directional 则使用 fullscreen vertex shader。

## Blend 与 Depth State

lighting pass 需要 additive blending 到 `SceneLightingAccum`。如果当前高层 `GraphicsProgramState` 还不能表达 additive blend，就应该补一个 renderer-facing blend state，而不是从 Function 层直接触碰 backend-specific RHI 类型。

light volume pass 第一版就要开启 depth test，并且必须关闭 culling，确保 point sphere 和 spot cone 在相机位于 volume 内外时都能稳定覆盖。当前高层 `GraphicsProgramState` 如果只能表达 `depth_test` bool 且默认固定为 LessEqual，需要先扩展出显式 depth compare state；light volume 不能被迫复用 GBuffer/base pass 的 depth compare 语义。

当前 depth 约定是 clear depth = 1.0、几何 pass 使用 LessEqual。基于这个约定，point sphere 和 spot cone 推荐使用 back-volume 语义的 depth compare，也就是 `GreaterEqual`，depth write 关闭，cull mode 为 `None`。Shader 仍然从 GBuffer depth 重建 world position，并做精确的 range/cone reject。若后续改成 reversed-Z，light volume 的 compare 方向也必须随 depth convention 一起翻转。

第一阶段状态：

- Directional：无 depth test，无 depth write，additive blend。
- Point sphere：depth test on，depth write off，cull none，additive blend；shader reject sphere 外像素。
- Spot cone：depth test on，depth write off，cull none，additive blend；shader reject cone 外像素。
- Composite：无 depth test，无 depth write，覆盖写最终 output。

后续优化：

- front-face/back-face volume strategy
- stencil light masks
- depth bounds
- scissor rectangles
- tiled/clustered dispatch

## 背景与 Emissive

`SceneLightingAccum` 的 clear color 就是 view background。

对几何像素：

- `Unlit` 贡献 base color 和 emissive。
- `DefaultLitGGX` 与 `BlinnPhong` 接收动态光照累积。
- Emissive 应该只加一次，不能每个 light 都加一次。第一阶段最干净的做法是在动态光之前加一个小的 base/emissive fullscreen pass。

由于当前 DeferredHQ 只在 `GBufferE.ba` 存了两个 emissive/custom 通道，第一版实现有两个选择：

1. 暂时只使用受限 emissive payload，并把它记录为临时方案；或
2. 在 lighting 落地前先修订 layout，让 emissive 有完整 RGB 表达。

推荐先修订 GBuffer contract 再实现 lighting。lighting 模块不应该一开始就依赖一个已知不完整的 emissive 表达。

## 验证要求

Deferred lighting 实现不算完成，直到满足：

- Sandbox Sponza 在 DX12 下能看到平行光、点光源、聚光灯。
- Sandbox Sponza 在 Vulkan 下能看到平行光、点光源、聚光灯。
- Editor smoke 在 DX12 和 Vulkan 下都能启动并正常退出。
- 背景保持配置的 scene clear color。
- `DefaultLitGGX`、`Unlit`、`BlinnPhong` 材质能明显走到不同 shading-model 分支。
- 修改点光源 range 时，只影响 sphere-volume 覆盖的像素。
- 修改聚光灯 cone angle 时，只影响 cone-volume 覆盖的像素。
- DX12 debug layer 与 Vulkan validation log 干净。

## 实现切片

1. 收紧 GBuffer shading-model/background contract。
2. 从 `LightComponent` 提取 render-thread light snapshot。
3. 增加 `SceneLightingAccum` 资源与 clear/background 处理。
4. 增加共享 GBuffer decode 与 BRDF shader includes。
5. 实现平行光 fullscreen lighting。
6. 实现点光源 sphere lighting。
7. 实现聚光灯 cone lighting。
8. 增加 material shading-model enum 值与 JSON 解析兼容。
9. 增加验证场景或 Sandbox overlay 控件，用于调节 light 参数。
