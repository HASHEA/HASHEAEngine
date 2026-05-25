# Directional CSM Shadow Design

Date: 2026-05-25
Status: Approved design for planning

## Context

AshEngine's deferred scene path currently renders static mesh GBuffer, ambient occlusion, deferred lighting accumulation, composite, and tone map. Shadow rendering is not implemented yet, but the engine already has two important foundations for a first shadow phase:

- `Surface.StaticMesh.DepthOnly` exists as a material pass family.
- `SceneRenderer::render_static_meshes_to_pass(...)` can submit static meshes for `PassFamily::DepthOnly`.
- `DeferredLightingPass` already submits each visible light individually inside the lighting accumulation pass.
- `RenderGraphTextureDesc` represents 2D textures, which fits atlas-based traditional shadow maps better than texture arrays or virtual shadow maps for this phase.

The Unreal Engine 5.7 non-VSM reference path uses traditional shadow depths, direction-light CSM, shadow depth atlases, whole-scene shadow caching, movable/static cache modes, and per-light screen-space shadow masks. This design adapts that direction to AshEngine without copying UE's full shadow subsystem.

## Goals

- Implement directional-light shadows first.
- Support any number of directional lights at scene-data level.
- Bound GPU memory through atlas and cache budgets rather than hard-coding a one-light or four-light limit.
- Use CSM for outer directional-light coverage.
- Keep a high-precision near shadow region around the main camera and update it every frame.
- Separate static and dynamic shadow casters so static shadow depth can be cached and dynamic casters can update frequently.
- Integrate shadows with the existing deferred lighting path through a reusable per-light screen shadow mask.
- Keep implementation inside Engine rendering modules and shared RHI abstractions.

## Non-Goals

- No virtual shadow maps in this phase.
- No point-light or spot-light shadows in this phase.
- No per-object shadows, preshadows, capsule shadows, contact shadows, distance-field shadows, or ray-traced shadows.
- No Editor-facing controls or Editor code changes.
- No texture-array dependency in the first implementation.
- No global precomputed all-light shadow mask used as persistent shadow storage.

## UE 5.7 Reference Summary

UE's traditional path does not render one permanent full shadow map per light and keep it forever. It builds shadow depth resources before lighting, then uses a per-light screen-space attenuation/shadow mask during lighting.

The relevant UE ideas are:

- Directional lights create view-dependent whole-scene shadows, one projected shadow per CSM split.
- CSM and other 2D shadows are packed into shadow depth atlases.
- Cached whole-scene shadow maps have a memory budget.
- Static and movable casters can be split into separate cache modes:
  - `StaticPrimitivesOnly`
  - `MovablePrimitivesOnly`
  - `CSMScrolling`
  - `Uncached`
- Movable shadow rendering can copy cached static depth into the current render target, then draw movable casters over it.
- CSM scrolling reuses the overlapped region of a cached cascade when the camera moves slightly, then renders newly exposed static casters plus movable casters.
- Lighting reuses a shared screen shadow mask: clear mask, project one light's shadows into it, then render that light.

AshEngine should copy the architecture shape, not the full feature set.

## First-Phase Pipeline

The scene graph should become:

```text
SceneGBufferPass
-> SceneAmbientOcclusionPass
-> SceneDirectionalShadowDepthPass
-> SceneDeferredLightingAccumPass
   -> base/emissive
   -> for each visible light:
      -> if shadowed directional:
         -> clear shared shadow mask
         -> project that light's CSM into shared shadow mask
         -> accumulate directional lighting using shadow mask
      -> else:
         -> accumulate existing unshadowed light path
-> SceneDeferredCompositePass
-> SceneDeferredToneMapPass
```

The shadow projection step can be implemented as separate render graph passes emitted before each shadowed directional-light accumulation draw, or as a helper owned by the deferred lighting phase. The render graph must still see explicit texture reads and writes for the shadow mask, shadow atlas, and lighting targets.

## Data Model

`VisibleLightData` should gain lightweight shadow intent, not large per-cascade data:

- `casts_shadow`
- `shadow_priority`
- `shadow_settings_id` or compact config values
- optional `shadow_distance`
- optional `shadow_cascade_count`
- optional `near_shadow_distance`

Per-frame derived data should live in a renderer-side shadow plan:

- shadowed light index
- cascade count
- cascade split near/far distances
- light view-projection per cascade
- atlas tile rectangle per cascade
- cache mode per cascade
- update reason per cascade

Shader-visible cascade data must use a constant/uniform/storage buffer. It should not be packed into existing lighting root constants because the current deferred lighting inline constants are already close to the 256-byte limit.

## CSM Layout

Each shadowed directional light receives a small set of cascades:

- Cascade 0 is the high-precision near cascade.
- Outer cascades cover increasing camera ranges.
- The first implementation should support 3 or 4 cascades per shadowed directional light.
- Cascade count can be configurable per light, but the renderer clamps by budget.

Cascade splits should use a practical lambda blend between linear and logarithmic partitioning:

```text
split = lerp(linear_split, logarithmic_split, split_lambda)
```

Initial defaults:

- `cascade_count = 4`
- `near_cascade_resolution = 2048`
- `outer_cascade_resolution = 1024`
- `split_lambda = 0.65`
- `shadow_distance` comes from light config or a renderer default.

The near cascade is always redrawn every frame. Outer cascades use cache policy.

## Resource Strategy

Use 2D depth atlases for the first implementation.

Resources:

- `DirectionalShadowDynamicAtlas`: transient or frame-owned depth atlas for current-frame cascades.
- `DirectionalShadowStaticCache`: persistent cached depth tiles for static cascades.
- `DirectionalShadowMask`: single screen-size transient texture reused per shadowed directional light.
- `DirectionalShadowCascadeBuffer`: shader-visible structured data for cascade matrices, tile scale/bias, split depths, and filter params.

The dynamic atlas is frame-local. The static cache persists across frames and is managed by a budgeted cache.

The static cache budget should be expressed in MB and tile count. A light can request shadowing without a hard light-count limit, but the shadow system may degrade or skip lower-priority shadow work when it exceeds budget.

## Cache Policy

Each light/cascade owns a cache entry keyed by:

- light entity id
- cascade index
- light direction
- cascade world bounds or snapped shadow projection
- shadow resolution
- static scene revision

Cache modes:

- `Uncached`: draw static and dynamic casters into the dynamic atlas.
- `StaticCached`: use a valid static cache tile and draw only dynamic overlay.
- `StaticRefresh`: render static casters into the persistent cache tile this frame.
- `NearEveryFrame`: draw near cascade every frame, including static and dynamic casters.

The first implementation should support `Uncached`, `StaticCached`, `StaticRefresh`, and `NearEveryFrame`.

`CSMScrolling` is a second-step optimization after the basic cache works. It requires copying the overlapped region from the previous cache tile, computing exposed regions, and culling static casters against those exposed regions.

## Static And Dynamic Classification

The renderer needs a conservative caster classification:

- Static caster: transform and mesh shape are stable, and the scene marks the primitive as not frequently moving.
- Dynamic caster: transform changes, mesh shape changes, skeletal mesh, or primitive is marked as frequently moving.

Current static mesh draw data does not yet carry a stable mobility flag. The implementation plan must add an Engine-side render-scene field for shadow caster mobility without exposing RHI details.

Until a proper mobility source exists, the first implementation may treat all current static mesh draws as static and reserve dynamic overlay for future dynamic primitive streams. That keeps the cache architecture correct while matching current renderer content.

## Rendering Passes

### Static Cache Refresh

For each cascade marked `StaticRefresh`:

- Bind persistent static cache tile as depth attachment.
- Clear tile to depth far value.
- Render static casters with `PassFamily::DepthOnly`.
- Store cache metadata after successful pass submission.

### Dynamic Atlas Build

For each visible cascade needing current-frame depth:

- Bind dynamic atlas tile as depth attachment.
- If cache mode is `StaticCached`, copy static cached depth into the dynamic tile first.
- If cache mode is `NearEveryFrame` or `Uncached`, clear and draw static casters as needed.
- Draw dynamic casters for `StaticCached` and future dynamic support.

Depth copy can start as a simple fullscreen/tile copy pass. Backend-specific copy shortcuts can come later only if they fit both Vulkan and DX12.

### Shadow Mask Projection

Before accumulating a shadowed directional light:

- Clear the shared screen shadow mask to lit (`1.0`).
- Read scene depth and GBuffer data needed to reconstruct world position.
- Read current light's CSM atlas tiles.
- Select cascade by view-space depth.
- Sample shadow depth using initial PCF filtering.
- Write a screen-space shadow factor.

The first mask format can be `R8_UNORM` if supported end-to-end. If format support or blending is awkward, use `RGBA8_UNORM` with the shadow factor in `r`.

### Deferred Lighting

Directional lighting gains a shadowed variant or branch:

- Unshadowed directional lights use the current path.
- Shadowed directional lights bind `SceneDirectionalShadowMask`.
- Diffuse and specular lighting are multiplied by the shadow factor.

Point and spot lights remain unchanged.

## Filtering And Bias

First implementation:

- Hardware compare sampler if the high-level sampler API exposes it cleanly for both backends.
- Otherwise use manual depth comparison in shader.
- 3x3 PCF for CSM.
- Per-light or renderer-default constant depth bias.
- Per-light or renderer-default normal/slope bias if surface normal is available from GBuffer.
- Clamp sampling outside atlas tile to lit or border-safe behavior.

The high-level sampler API currently does not expose compare sampler settings even though lower RHI sampler creation has compare fields. The design should not depend on compare samplers until Function-level sampler descriptors are extended.

## Budget And Degradation

There is no hard limit on directional-light count at scene level. There is a bounded shadow budget.

Priority order:

1. Main camera near cascade for the highest-priority shadowed directional lights.
2. Remaining cascades for high-priority lights.
3. Static cache refreshes.
4. Lower-priority or distant lights.

When budget is exceeded:

- Keep lighting but skip shadows for low-priority lights.
- Reduce outer cascade resolution before near cascade resolution.
- Reduce outer cascade update frequency.
- Evict least-recently-used static cache entries.

The renderer should report these decisions through profiling counters and debug logs at a throttled rate.

## Debugging And Instrumentation

New render passes must use `Base/hprofiler.h` instrumentation.

Debug views should eventually include:

- shadow dynamic atlas
- static cache atlas
- per-light screen shadow mask
- cascade index visualization
- cache mode visualization

The first implementation only needs enough debug visibility to verify atlas contents, mask contents, and cascade selection.

## Validation Strategy

Shared rendering changes must validate both Vulkan and DX12.

Design-level acceptance before implementation:

- The spec contains no VSM dependency.
- The spec keeps shadow work in Engine modules.
- The spec does not require Editor changes.
- The spec uses 2D atlas resources compatible with current RenderGraph constraints.

Implementation acceptance later:

- Build succeeds.
- Sandbox Vulkan smoke test succeeds.
- Sandbox DX12 smoke test succeeds.
- Editor Vulkan smoke test succeeds.
- Editor DX12 smoke test succeeds.
- No Vulkan validation or DX12 debug-layer errors are introduced.
- RenderDoc or debug view confirms non-empty shadow depth and screen mask for a test scene.

## Deferred Work

These are intentionally outside the first implementation:

- CSM scrolling cache.
- Point and spot shadows.
- Texture array shadow atlases.
- Per-object and preshadow support.
- Skeletal mesh shadow casters.
- Contact shadows.
- Distance-field or ray-traced shadowing.
- Shadow denoising.
- Editor UI for shadow settings.

## Proposed File Ownership

Likely Engine-side files for later implementation:

- `project/src/engine/Function/Render/DirectionalShadowPass.h`
- `project/src/engine/Function/Render/DirectionalShadowPass.cpp`
- `project/src/engine/Function/Render/DeferredLightingPass.*`
- `project/src/engine/Function/Render/SceneRenderer.*`
- `project/src/engine/Function/Render/RenderScene.*`
- `project/src/engine/Shaders/Shadow/*`
- `project/src/engine/Shaders/Deferred/*`

Do not modify `project/src/editor` for this feature.
