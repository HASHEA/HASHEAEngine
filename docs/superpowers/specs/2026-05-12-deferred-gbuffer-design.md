# Deferred GBuffer Render Path Design

## Goal

Add a deferred GBuffer render path for `Surface.StaticMesh` while keeping material user shaders independent from forward/deferred implementation details. The first implementation will ship a fixed `DeferredHQ` layout, but the API and shader generation model must be layout-key driven so later renderer features can change MRT count, formats, and semantic placement without rewriting material shaders.

## Non-Goals

- Do not expose GBuffer packing, MRT indices, or forward/deferred branch logic to user material shaders.
- Do not remove the forward path. Forward remains the fallback path and the future transparent path.
- Do not implement full production lighting, TSR, Lumen, SSR, or clustered lights in the first pass.
- Do not allow per-material or per-draw GBuffer layout variation inside one view. Layout changes happen at view or renderer feature level.

## Existing Context

The current material V2 path composes engine host shaders with user material shaders and generated material bindings. User material shaders currently provide material behavior through:

```hlsl
CalculateVertexMainNode(params, node);
CalculatePixelMainNode(params, node);
```

The current static mesh scene path submits `Surface.StaticMesh.BasePass` to one color target plus depth. `SceneRenderer` owns static mesh draw submission, `MaterialRenderProxy` prepares pass-specific material resources, and `Renderer` / `RenderDevice` already expose multi-color `PassDesc::color_attachments`.

## Material Boundary

User material shaders describe the surface only. They fill `AshVertexMainNode` and `AshPixelMainNode`; they never write render targets directly and never include GBuffer layout headers.

Engine host shaders decide how a surface node is consumed:

```text
SurfaceStaticMeshForward.hlsl
  AshPixelMainNode -> forward lighting -> SV_Target0

SurfaceStaticMeshGBuffer.hlsl
  AshPixelMainNode -> AshSurfaceMaterialData -> EncodeGBuffer(...) -> SV_Target0..N

SurfaceStaticMeshDepthOnly.hlsl
  AshPixelMainNode -> opacity mask discard -> depth
```

This keeps `.AshMat` and `.AshMatIns` reusable across forward, deferred, depth-only, shadow, and future special passes.

## Render Feature And Layout Model

Introduce a renderer-owned GBuffer layout description:

```cpp
enum class SceneRenderFeature : uint64_t
{
    DeferredLighting = 1ull << 0,
    TemporalMotionVector3D = 1ull << 1,
    ExtendedMaterialData = 1ull << 2,
    HDRMaterialEmission = 1ull << 3,
    MaterialId = 1ull << 4,
    DebugGBuffer = 1ull << 5
};

struct GBufferLayoutKey
{
    uint64_t feature_flags = 0;
    uint32_t quality_tier = 0;
    uint32_t platform_tier = 0;
};

struct GBufferLayoutDesc
{
    uint64_t layout_hash = 0;
    std::vector<GBufferAttachmentDesc> attachments;
    std::vector<GBufferSemanticMapping> semantic_mappings;
    std::vector<std::string> shader_defines;
};
```

The first implementation may expose only one key, `DeferredHQ`, but all shader resources and render resources should be keyed by `layout_hash`. Layout changes are explicit events that rebuild GBuffer render targets, material shader permutations, and lighting programs.

Do not change layout implicitly every frame.

## Default DeferredHQ Layout

`DeferredHQ` uses five color attachments plus depth:

| Buffer | Format | Semantic |
| --- | --- | --- |
| GBufferA | `RGBA8_UNORM` | `BaseColor.rgb` + `ShadingModelId/Flags.a` |
| GBufferB | `RGBA8_UNORM` | `Metallic.r` + `Roughness.g` + `AmbientOcclusion.b` + `Specular/Flags.a` |
| GBufferC | `RGBA8_UNORM` | `CustomData0..3`, interpreted by shading model |
| GBufferD | `RGBA16_SFLOAT` | `MotionVector3D.xyz` + `TemporalFlags.a` |
| GBufferE | `RGBA16_SFLOAT` | `NormalOct.xy` + `EmissiveOrCustom.zw` |
| Depth | `D32_SFLOAT` | Linear depth reconstruction source for lighting |

`MotionVector3D` is defined as screen-space velocity `xy` plus a `z` component for temporal reconstruction. The `xy` channels are directly consumable by TAA, motion blur, and TSR-style passes. The `z` channel is reserved for depth/reprojection history use and future Lumen-style history systems.

`NormalOct.xy` stores world-space or view-space normal using octahedral encoding. The first implementation should keep it in `RGBA16_SFLOAT.rg`; later format work can move it to `RG16F` or `RG16_SNORM`.

## Future Layout Presets

The dynamic layout system should allow later presets:

```text
DeferredBase
  A: BaseColor + Flags
  B: M/R/AO/Specular
  C: NormalOct + compact custom data

DeferredTemporal
  DeferredBase + MotionVector3D attachment

DeferredHQ
  A/B/C + MotionVector3D + NormalOct + emissive/custom

DeferredExtended
  DeferredHQ + independent material/object id or additional custom data
```

Only `DeferredHQ` is required for the first code pass.

## Shader Generation

`MaterialShaderSourceBuilder` should add GBuffer pass macros only for the engine host shader permutation:

```hlsl
#define ASH_PASS_GBUFFER 1
#define ASH_GBUFFER_LAYOUT_HASH ...
#define ASH_GBUFFER_OUTPUT_COUNT 5
#define ASH_GBUFFER_HAS_MOTION_VECTOR_3D 1
```

The engine host GBuffer shader includes an engine-generated GBuffer layout header that declares output structures and encode/load helpers:

```hlsl
struct AshGBufferOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
};
```

The user material shader does not see this structure.

## Scene Render Flow

Deferred rendering for an opaque or masked static mesh view:

```text
ScenePresentationSubsystem
  -> builds VisibleRenderFrame

SceneRenderer
  -> resolves SceneDeferredFrameResources for the view
  -> GBuffer pass writes DeferredHQ MRTs + depth
  -> DeferredLighting pass samples GBuffer + depth
  -> writes view_context.output_target
```

`SceneDeferredFrameResources` should own or reference:

- GBuffer layout hash.
- Per-layout color render targets.
- Shared depth target.
- View dimensions.
- Clear values and load/store policy.

The GBuffer pass prepares `Surface.StaticMesh.GBuffer` material resources. The lighting pass is an engine shader, not a material shader.

## Forward Compatibility

Forward rendering remains available:

- As a debug/fallback mode.
- For transparent materials until the transparent queue is implemented.
- For comparing deferred and forward output during migration.

The renderer can expose a runtime switch later, but first implementation can choose deferred for opaque/masked static meshes by default in Sandbox.

## Required Engine Changes

- Add `PassFamily::GBuffer` and a host shader path for `Surface.StaticMesh.GBuffer`.
- Add `GBufferLayoutDesc`, `GBufferLayoutKey`, and a registry/helper that returns `DeferredHQ`.
- Add GBuffer layout generated HLSL or a fixed first-pass include that is still keyed by layout hash.
- Add `SurfaceStaticMeshGBuffer.hlsl`.
- Add a deferred lighting fullscreen shader.
- Extend `MaterialRenderProxy` and `MaterialShaderMap` resource preparation to request and cache GBuffer resources.
- Extend `SceneRenderer` to create GBuffer resources, submit the GBuffer pass, and run the lighting pass.
- Keep render pass and graphics program variants keyed by actual MRT formats/counts.
- Update `README.md` and `docs/EngineDeveloperGuide.md`.

## Validation Requirements

First implementation is not complete until:

- Sandbox runs Sponza, DamagedHelmet, and Avocado on DX12.
- Sandbox runs at least Sponza and DamagedHelmet on Vulkan.
- Editor smoke still starts on DX12 and Vulkan.
- Forward fallback remains functional.
- GBuffer resize and Sandbox model switch do not reuse stale render targets.
- Vulkan shutdown reports no live VMA allocations.
- DX12 debug layer has no MRT/PSO/render-target format errors.

## Implementation Slices

1. Add layout descriptions and fixed `DeferredHQ` registry.
2. Add GBuffer pass family and host shader.
3. Add deferred lighting fullscreen pass.
4. Route `SceneRenderer` opaque/masked static meshes through GBuffer plus lighting.
5. Add previous-frame transform/view data and write real `MotionVector3D`.
