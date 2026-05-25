# Skybox 与 IBL 环境光设计

日期：2026-05-25

## 决策

在 Scene 驱动的 deferred renderer 中新增一个 Engine 侧环境光系统。该系统同时负责可见 sky background、PBR diffuse irradiance、PBR specular reflection / IBL，并支持运行时生成和离线生成两条生产路径。

第一版采用专用 `EnvironmentComponent` 作为 Scene authoring 入口，但渲染层只允许每个 Scene 消费一个 active environment。环境资源使用自定义 `.ashibl` 聚合资产格式。`.ashibl` v1 不做压缩，保存 uncompressed HDR payload，优先打通资产、上传、渲染和后续 Editor 接口。

## 目标

- 为当前 deferred scene path 接入 skybox background 和 PBR IBL。
- 支持 `.ashibl` cooked environment asset。
- 保留 HDR / equirectangular source 的运行时 IBL 生成能力。
- 提供 Engine-facing 离线 baker API，后续 Editor 可直接调用该接口生成 `.ashibl`。
- 运行时生成和离线生成必须产出同一种 cooked data contract。
- 渲染消费端只依赖 `EnvironmentMapRuntimeResource`，不关心资源来自 `.ashibl` 还是 runtime bake。
- 只支持一个 active environment，避免第一版引入 blending、priority stack 或 per-view environment policy。
- 保持 Engine / Editor 边界，不修改 `project/src/editor`。
- 通过 Function 层共享 API 支持 Vulkan 和 DX12，不把 backend-specific 类型暴露给 Scene、Editor 或材质 authoring。

## 非目标

- 第一版不支持多个 environment 混合。
- 第一版不支持 environment probe volume、reflection capture 网格或局部反射盒。
- 第一版不做 `.ashibl` payload 压缩，不引入 BC6H、zstd 或 KTX2 embedding。
- 第一版不要求 Editor UI 接入，只提供 Engine 侧接口和资产契约。
- 第一版不把 skybox 当作 `MeshComponent` 或普通 static mesh primitive。
- 第一版不做 dynamic time-of-day sky atmosphere。

## 当前上下文

当前 Scene 到渲染主路径为：

```text
Scene
  -> ScenePresentationSubsystem
  -> RenderScene
  -> VisibleRenderFrame
  -> SceneRenderer
  -> RenderGraph
```

当前 deferred graph 主路径为：

```text
SceneGBufferPass
  -> SceneAmbientOcclusionPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredCompositePass
  -> SceneDeferredToneMapPass
```

现有 `VisibleRenderFrame` 已携带 static mesh draw、light snapshot、view/projection、camera position 和 reverse-Z。环境光应作为同一帧快照的一部分进入渲染，而不是在 render thread 直接读取 `Scene`。

当前 Function 层 texture upload 主要暴露 `create_texture_2d(const TextureUploadDesc&)`。底层 RHI 已有 cubemap texture / view 的概念，但 Function 层还需要新增 cubemap upload 和 mip / face payload 支持，才能让 `.ashibl` 成为正式 runtime asset。

## Scene Authoring

新增 `EnvironmentComponent`：

```cpp
struct EnvironmentComponent
{
    bool active = true;
    std::string ibl_asset_path{};
    std::string source_texture_path{};
    float intensity = 1.0f;
    float rotation_degrees = 0.0f;
    bool visible_background = true;
    bool affect_lighting = true;
};
```

字段语义：

- `active`：参与 active environment 选择。
- `ibl_asset_path`：优先加载 `.ashibl` cooked asset。
- `source_texture_path`：当 `ibl_asset_path` 为空或加载失败且允许 fallback 时，用于 runtime bake。
- `intensity`：统一缩放 background 和 IBL lighting 的线性强度。
- `rotation_degrees`：绕 world up 的环境旋转。第一版不使用 `TransformComponent` 控制 skybox 方向。
- `visible_background`：控制 sky background pass。
- `affect_lighting`：控制 IBL lighting pass。

`EnvironmentComponent` 可以挂在普通 Scene entity 上，便于后续 Editor hierarchy 和 property panel 管理，但它不生成 primitive，不参与 bounds、frustum culling、static mesh material 或 motion vector。

## 单 Active Environment 规则

第一版每个 Scene 只消费一个 active environment。

`Scene::extract_active_environment()` 按 Scene entity order 扫描：

1. 没有 active `EnvironmentComponent` 时返回 empty。
2. 有一个 active environment 时返回该环境描述。
3. 有多个 active environment 时选择第一个有效项，并对该 Scene 记录一次 warning。

Editor 后续可以在 authoring UI 中阻止多个 active environment，但 Engine runtime 仍需要 deterministic fallback。选择结果写入 `RenderScene` 的 environment snapshot。

Scene 版本应新增 `render_environment_version`。`EnvironmentComponent` 增删改只更新 environment version，不触发 primitive rebuild、transform update 或 light rebuild。

## Render Snapshot

新增渲染侧快照：

```cpp
struct VisibleEnvironmentData
{
    EntityId entity_id = 0;
    std::string ibl_asset_path{};
    std::string source_texture_path{};
    float intensity = 1.0f;
    float rotation_degrees = 0.0f;
    bool visible_background = true;
    bool affect_lighting = true;
};
```

`VisibleRenderFrame` 增加：

```cpp
std::optional<VisibleEnvironmentData> environment;
```

`RenderScene` 负责缓存当前 active environment。`ScenePresentationSubsystem` 在检测到 `render_environment_version` 变化时，只刷新 environment snapshot，并在 `build_visible_render_frame()` 时把它复制到 `VisibleRenderFrame`。

## .ashibl v1 资产格式

`.ashibl` 是聚合环境光资产，不是单一贴图格式。第一版 payload 不压缩。

文件包含：

```text
AshIBLHeader
AshIBLMetadataJson
AshIBLPayloadTable
RadianceCubemapPayload
IrradianceCubemapPayload
PrefilteredSpecularCubemapPayload
BRDFLut2DPayload
OptionalPreviewThumbnailPayload
```

Header 建议结构：

```cpp
struct AshIBLHeader
{
    char magic[8];                 // "ASHIBL\0"
    uint32_t version;              // 1
    uint32_t flags;
    uint32_t payload_count;
    uint32_t reserved0;
    uint64_t metadata_offset;
    uint64_t metadata_size;
    uint64_t payload_table_offset;
    uint64_t payload_table_size;
};
```

Payload desc 建议结构：

```cpp
enum class AshIBLPayloadKind : uint32_t
{
    RadianceCubemap = 1,
    IrradianceCubemap = 2,
    PrefilteredSpecularCubemap = 3,
    BRDFLut2D = 4,
    PreviewThumbnail2D = 5
};

enum class AshIBLCompression : uint32_t
{
    None = 0
};

struct AshIBLPayloadDesc
{
    AshIBLPayloadKind kind;
    AshIBLCompression compression;
    RenderTextureFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t face_count;
    uint32_t mip_count;
    uint64_t byte_offset;
    uint64_t byte_size;
    uint64_t uncompressed_size;
};
```

v1 固定规则：

- `compression` 必须为 `None`。
- cubemap `face_count` 必须为 6。
- cubemap face order 固定为 `+X, -X, +Y, -Y, +Z, -Z`。
- mip payload 顺序固定为 `mip -> face`，每个 subresource 是 tightly packed rows。
- HDR cubemap payload 使用 `RenderTextureFormat::RGBA16_SFLOAT`。
- BRDF LUT 优先使用 `RenderTextureFormat::RG16_SFLOAT`；如果当前 shared format table 不支持，则第一版退到 `RGBA16_SFLOAT`。
- 所有 payload offset 需要满足至少 16-byte alignment。

Metadata JSON 保存：

- asset version。
- source texture path。
- source content hash。
- build settings。
- generated timestamp。
- baker version。
- radiance / irradiance / prefilter / BRDF LUT resolution 和 mip count。

## EnvironmentMapCookedData

运行时生成和离线生成共用同一份 cooked data contract：

```cpp
struct EnvironmentMapBuildDesc
{
    std::string source_texture_path{};
    uint32_t radiance_size = 1024;
    uint32_t irradiance_size = 64;
    uint32_t prefilter_size = 256;
    uint32_t prefilter_mip_count = 8;
    uint32_t brdf_lut_size = 256;
    RenderTextureFormat hdr_format = RenderTextureFormat::RGBA16_SFLOAT;
    uint32_t sample_count = 1024;
};

struct TextureCubePayload
{
    RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 0;
    std::vector<TextureSubresourcePayload> subresources{};
};

struct EnvironmentMapCookedData
{
    EnvironmentMapBuildDesc build_desc{};
    uint64_t source_content_hash = 0;
    TextureCubePayload radiance{};
    TextureCubePayload irradiance{};
    TextureCubePayload prefiltered_specular{};
    Texture2DPayload brdf_lut{};
};
```

`EnvironmentMapBaker` 产出 `EnvironmentMapCookedData`。`.ashibl` writer 只负责序列化该数据，`.ashibl` reader 只负责解析并恢复该数据。

## Runtime Resource

渲染消费端使用：

```cpp
struct EnvironmentMapRuntimeResource
{
    std::shared_ptr<RenderTarget> radiance_cubemap;
    std::shared_ptr<RenderTarget> irradiance_cubemap;
    std::shared_ptr<RenderTarget> prefiltered_specular_cubemap;
    std::shared_ptr<RenderTarget> brdf_lut;
    uint64_t change_version = 1;
};
```

该对象由 `RenderAssetManager` 或专用 `EnvironmentMapAssetManager` 创建和缓存。`SceneRenderer` 只绑定这些 GPU resources，不读取 `.ashibl` 文件，也不调 baker。

如果 environment 缺失，渲染行为为：

- background 使用 `SceneRenderViewContext.color_clear_value`。
- IBL lighting pass 不注册，或绑定 black/neutral fallback resource 并输出 0。

## 运行时生成路径

运行时生成用于快速预览、Sandbox 验证和 `.ashibl` 缺失时的 fallback。

流程：

```text
HDR/equirect source
  -> decode source texture
  -> equirect to radiance cubemap
  -> diffuse irradiance convolution
  -> specular prefilter mip chain
  -> BRDF LUT generation or shared LUT reuse
  -> EnvironmentMapRuntimeResource
  -> optional .ashibl cache
```

运行时生成应通过 Engine baker facade 进入，不应把 bake pass 逻辑散落到 `SceneRenderer`。可选 cache 输出放在 `product/caches/EnvironmentCaches/`，不能写到 repository root。

## 离线生成路径

离线生成同样使用 Engine-facing baker API：

```cpp
class EnvironmentMapBaker
{
public:
    bool bake_to_cooked_data(
        const EnvironmentMapBuildDesc& desc,
        EnvironmentMapCookedData& out_data,
        EnvironmentBakeReport* out_report);

    bool write_ashibl(
        const EnvironmentMapCookedData& data,
        const std::filesystem::path& output_path,
        EnvironmentBakeReport* out_report);

    bool read_ashibl(
        const std::filesystem::path& input_path,
        EnvironmentMapCookedData& out_data,
        EnvironmentBakeReport* out_report);
};
```

后续 Editor 只调用该 Engine API 生成 `.ashibl` asset，不直接访问 `SceneRenderer`、RenderGraph internals 或 backend-specific RHI 类型。

第一版 baker 可以复用 GPU path。后续如果需要 command-line cooker 或 CPU baker，仍保持 `EnvironmentMapCookedData` 和 `.ashibl` 文件契约不变。

## Function 层 Texture API

需要补齐共享 Function 层 cubemap upload API：

```cpp
struct TextureSubresourceUploadDesc
{
    uint32_t mip_level = 0;
    uint32_t array_layer = 0;
    const void* data = nullptr;
    uint32_t row_pitch = 0;
    uint32_t slice_pitch = 0;
};

struct TextureCubeUploadDesc
{
    uint16_t width = 1;
    uint16_t height = 1;
    RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
    uint8_t mip_level_count = 1;
    const TextureSubresourceUploadDesc* subresources = nullptr;
    uint32_t subresource_count = 0;
    const char* name = nullptr;
};
```

`Renderer` / `RenderDevice` 暴露 `create_texture_cube(const TextureCubeUploadDesc&)`。实现内部映射到底层 `RHI::TextureCreation` 的 cubemap type、6 array layers 和 mip count。DX12 / Vulkan 都应通过同一个 Function API 创建。

如果 BRDF LUT 使用 2D texture，则继续走 `create_texture_2d()`。

## RenderGraph 形态

目标 graph 为：

```text
SceneGBufferPass
  -> SceneAmbientOcclusionPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredEnvironmentLightingPass
  -> SceneDeferredCompositePass
  -> SceneSkyBackgroundPass
  -> SceneDeferredToneMapPass
```

`SceneDeferredEnvironmentLightingPass` 读取：

- GBuffer targets。
- `SceneDeferredDepth`。
- `SceneAmbientOcclusion`。
- environment irradiance cubemap。
- environment prefiltered specular cubemap。
- BRDF LUT。

该 pass 写：

- `SceneDeferredLightingDiffuse`，load existing lighting，additive blend 或 shader 显式加。
- `SceneDeferredLightingSpecular`，load existing lighting，additive blend 或 shader 显式加。

`SceneSkyBackgroundPass` 读取：

- `SceneDeferredDepth`。
- environment radiance cubemap。
- `SceneDeferredSceneHDRLinear`。

该 pass 写：

- `SceneDeferredSceneHDRLinear`，load existing HDR，且只在 depth 为 background 的像素写 sky radiance。

Sky background 放在 composite 后、tone-map 前，保证 sky 与 scene lighting 共用 HDR tone-map。IBL lighting 放在 composite 前，保证 diffuse/specular lighting debug view 能观察 IBL 贡献。

## PBR IBL Shader Contract

Deferred IBL 使用当前 GBuffer surface decode contract。

Diffuse IBL：

```text
diffuseIBL = irradiance(normal_ws) * baseColor * kd * ao * intensity
```

Specular IBL：

```text
reflection = reflect(-view_dir_ws, normal_ws)
prefiltered = sample_prefiltered_specular(reflection, roughness)
brdf = sample_brdf_lut(n_dot_v, roughness)
specularIBL = prefiltered * (F0 * brdf.x + brdf.y) * ao * intensity
```

`rotation_degrees` 应统一作用于 background sampling、irradiance sampling 和 specular reflection sampling。第一版只支持 yaw rotation。

`affect_lighting=false` 时不注册 environment lighting pass。`visible_background=false` 时不注册 sky background pass。

## Error Handling

错误处理遵循项目 process-error 风格：

- `ASH_PROCESS_GUARD_RETURN`
- `ASH_PROCESS_ERROR`
- `ASH_LOG_PROCESS_ERROR`

`.ashibl` loader 必须校验：

- magic。
- version。
- payload table bounds。
- payload offset / size 是否在文件范围内。
- v1 compression 是否为 `None`。
- 必需 payload 是否存在。
- cubemap face count 是否为 6。
- mip count、width、height 是否合法。

加载失败时，`RenderAssetManager` 记录 warning，environment resource 标记失败，并按 fallback 策略处理。不能让失败的 environment 破坏整帧渲染。

## Profiling 与调试

新增或修改的 render pass 必须带 Tracy instrumentation：

- `SceneDeferredEnvironmentLightingPass`
- `SceneSkyBackgroundPass`
- `EnvironmentMapBaker::EquirectToCube`
- `EnvironmentMapBaker::IrradianceConvolution`
- `EnvironmentMapBaker::SpecularPrefilter`
- `EnvironmentMapBaker::BRDFLUT`

RenderGraph pass name、RHI resource debug name 和 shader program name 应能在 RenderDoc 中直接定位。例如：

- `SceneEnvironmentIrradianceCube`
- `SceneEnvironmentPrefilteredSpecularCube`
- `SceneEnvironmentBRDFLUT`
- `SceneSkyBackground`

Render Debug View 后续可增加 `SceneEnvironmentLightingDiffuse`、`SceneEnvironmentLightingSpecular` 或 sky-only debug 项，但第一版不强制。

## Validation

该设计触碰共享 renderer、RenderDevice、shader binding、texture upload 和 Scene snapshot。实现后必须视为 Vulkan + DX12 共享路径变更。

最低验证矩阵：

- Build Debug x64。
- Sandbox Vulkan 25s normal shutdown。
- Sandbox DX12 25s normal shutdown。
- Editor Vulkan 25s normal shutdown。
- Editor DX12 25s normal shutdown。

额外检查：

- 无 `.ashibl` 且无 source texture 时画面仍正常。
- `.ashibl` 加载失败时画面 fallback 正常且只记录受控 warning。
- `visible_background=false` 时背景不写 sky。
- `affect_lighting=false` 时 IBL 不影响材质光照。
- 多个 active environment 时只选第一个并 warning once。
- Vulkan shutdown 无 VMA leak。

## 分阶段落地建议

第一阶段：

- 添加 `EnvironmentComponent`、Scene metadata、版本和 extraction。
- 添加 `.ashibl` reader / writer 数据结构和无压缩 payload parser。
- 添加 Function 层 cubemap upload API。
- 添加 fallback runtime resource 和 `.ashibl` runtime loading。

第二阶段：

- 添加 `EnvironmentMapBaker` GPU runtime bake。
- 支持 HDR equirect source 到 `.ashibl` cooked data。
- 生成或复用 BRDF LUT。

第三阶段：

- 接入 `SceneDeferredEnvironmentLightingPass` 和 `SceneSkyBackgroundPass`。
- 更新 deferred shader binding。
- 完成 Vulkan / DX12 验证。

第四阶段：

- 为 Editor 接入离线 bake API 预留调用面。
- 后续按需要扩展 `.ashibl` compression、preview thumbnail、cache invalidation 和 debug view。
