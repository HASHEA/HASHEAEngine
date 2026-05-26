# SunLight And DirectionalLight Shadow Pass Split Design

Date: 2026-05-26
Status: Approved design

## Context

The current directional shadow implementation treats every shadow-casting directional light as a candidate for the same CSM atlas, static cache, dynamic overlay, screen-space shadow mask, and budget planner. That removed the scene-level hard light-count cap, but shadow work is still bounded by dynamic atlas size, static cache atlas size, and static cache memory budget. In the Sandbox scene, the automatically generated environment sun can consume the available high-quality CSM allocation, causing ordinary directional lights to fall back to unshadowed lighting.

The new design separates the semantic "sun" role from ordinary directional lights. A scene can have many directional lights, but at most one sunlight. The sunlight uses the expensive large-scene shadow path. Ordinary directional lights use a simpler per-light transient cascade path that redraws every frame and reuses the same transient resources across lights.

## Goals

- Add an explicit `sunlight` flag to `LightComponent`.
- Enforce at most one `LightType::Directional && sunlight` light per scene.
- Keep the existing large CSM, static cache, dynamic overlay, and sunlight debug path for the single sunlight.
- Add a separate ordinary directional-light shadow pass that does not use static cache, does not split static and dynamic casters, and does not skip lights because of a shared directional shadow atlas budget.
- Reuse ordinary directional-light shadow resources across per-light passes in the same frame.
- Preserve point and spot lighting behavior.
- Keep the shared render path legal on Vulkan and DX12.

## Non-Goals

- Point and spot shadows remain out of scope.
- VSM / EVSM is not introduced in this design.
- This does not add editor UI for the new `sunlight` field.
- This does not attempt to make ordinary directional lights cheap; their shadow cost scales linearly with light count and cascade count.

## Scene Data Model

`LightComponent` gains:

```cpp
bool sunlight = false;
```

The flag is meaningful only when `type == LightType::Directional`. For point and spot lights it is serialized and stored as `false`.

Scene JSON and asset serialization add a `"sunlight"` field under `"light"`. Missing fields default to `false`, so older scenes keep ordinary directional-light behavior unless the environment importer creates a sunlight.

The environment metadata synchronization path creates or updates the engine-owned `EnvironmentSunLight` entity with:

```cpp
type = LightType::Directional;
sunlight = true;
casts_shadow = true;
```

After scene load and environment sunlight synchronization, the scene validates that no more than one valid directional sunlight exists. A conflict is a load failure rather than an implicit downgrade, because silently changing a light from sunlight to ordinary directional light would make render output depend on load order.

`VisibleLightData` also gains `sunlight`, copied from `LightComponent`.

## Pass Classes

### `SunLightShadowPass`

`SunLightShadowPass` replaces the current high-end role of `DirectionalShadowPass`.

Responsibilities:

- Select only `LightType::Directional && sunlight && casts_shadow`.
- Assert or fail planning if more than one sunlight reaches the renderer.
- Build the sunlight CSM frame plan.
- Maintain static cache atlas and LRU entries.
- Produce sunlight dynamic atlas, static cache atlas, screen-space shadow mask, cascade debug target, and cascade storage buffer.
- Support the existing static-cache refresh and dynamic-overlay logic.

The class keeps the current large-scene resource model and most current shaders. Existing debug view names can be renamed to `SunLightShadowDynamicAtlas`, `SunLightShadowStaticCache`, `SunLightShadowMask`, and `SunLightShadowCascadeIndex` to make the split visible in RenderDoc and the debug overlay.

### `DirectionalLightShadowPass`

`DirectionalLightShadowPass` is a new ordinary directional-light shadow path.

Responsibilities:

- Process one ordinary directional light at a time: `LightType::Directional && !sunlight && casts_shadow`.
- Build cascade splits and light view-projection matrices for that one light.
- Create or reference transient RenderGraph resources for one directional-light shadow atlas and one screen-space mask.
- Clear the transient atlas for each ordinary directional light before drawing its cascades.
- Draw all shadow casters every frame with `ShadowCasterMobilityFilter::All`.
- Generate the screen-space shadow mask for the same light.
- Return the per-light mask to the immediately following deferred lighting pass.

This pass does not own a static cache atlas, does not keep LRU state, does not classify static vs dynamic casters, and does not skip ordinary directional lights due to a shared atlas tile budget. The only hard constraints are per-light cascade count, per-light atlas dimensions, and successful RenderGraph resource creation.

## Resource Model

Sunlight resources are long-lived or frame-wide, matching the current implementation:

- sunlight dynamic atlas
- sunlight static cache atlas
- sunlight shadow mask
- sunlight cascade debug target
- sunlight cascade buffer

Ordinary directional-light resources are transient and per-light in graph order:

- directional-light transient atlas
- directional-light transient mask
- directional-light cascade buffer

Graph resource names should include the frame light index, for example `SceneDirectionalLightShadowAtlas_5` and `SceneDirectionalLightShadowMask_5`, so RenderDoc stays readable. The resources are still transient graph resources, so the compiler can alias physical memory when lifetimes do not overlap. If the current RenderGraph allocator does not yet alias same-desc transient resources, this design remains correct and only loses memory efficiency until aliasing improves.

## Deferred Lighting Flow

The scene renderer builds the deferred graph in this order:

1. GBuffer and ambient occlusion.
2. `SunLightShadowPass` prepares sunlight shadow outputs if a sunlight exists and shadows are enabled.
3. Deferred base/emissive pass clears and initializes lighting accumulation targets.
4. Iterate `VisibleRenderFrame::lights` in stable frame order:
   - Sunlight: if sunlight shadow output exists for this light, add sunlight shadow mask pass and shadowed directional lighting pass. Otherwise use the unshadowed directional path.
   - Ordinary directional light: if `casts_shadow`, call `DirectionalLightShadowPass` for this frame light, then immediately add a shadowed directional lighting pass using the returned mask. If the pass fails to produce a mask, fail the graph build rather than silently making the light unshadowed.
   - Point and spot lights: keep current volume lighting path.
5. Environment lighting.
6. Composite.
7. Sky background.
8. Tone map and overlays.

The ordinary directional path inserts shadow depth and mask work inside the per-light lighting loop. That is the key change that removes the need for a shared ordinary-directional-light shadow budget.

## API Shape

`SceneRenderer` owns both pass instances:

```cpp
SunLightShadowPass m_sunlight_shadow_pass;
DirectionalLightShadowPass m_directional_light_shadow_pass;
```

`DeferredLightingPass::add_lighting_accumulation_pass()` should not own shadow rendering. It should either:

- expose helper methods for base pass and individual light pass submission, or
- accept a small callback interface that can insert ordinary directional shadow passes before a light pass.

The cleaner long-term shape is to split the deferred lighting API into explicit methods:

- `add_base_pass(...)`
- `add_directional_light_pass(...)`
- `add_point_light_pass(...)`
- `add_spot_light_pass(...)`

Then `SceneRenderer` orchestrates the per-light shadow and lighting sequence directly. This keeps the shadow pass classes independent and avoids giving `DeferredLightingPass` knowledge of scene shadow-caster rendering callbacks.

## Cascade Policy

Sunlight uses the existing cascade policy and config fields:

- `default_cascade_count`
- `default_shadow_distance`
- `near_shadow_distance`
- `split_lambda`
- sunlight atlas/cache resolution and budget fields

Ordinary directional lights also use `shadow_cascade_count`, `shadow_distance`, `near_shadow_distance`, `split_lambda`, `depth_bias`, `normal_bias`, and `pcf_radius`, but use their own transient atlas resolution. The first implementation can reuse `near_cascade_resolution` and `outer_cascade_resolution` as ordinary directional cascade tile sizes to avoid new config surface. If ordinary lights need independent quality later, add explicit config fields after the split is stable.

## Error Handling

- Scene load fails if more than one directional sunlight exists after environment synchronization.
- Renderer graph build fails if a shadowed ordinary directional light cannot create its transient atlas, cascade buffer, mask, or passes.
- Sunlight pass planning fails if multiple sunlight candidates reach the render frame; this is a defensive check in addition to scene validation.
- Ordinary directional lights with invalid direction, non-positive intensity, or non-positive configured shadow distance follow existing light filtering and sanitization rules.

## Debugging

RenderDoc pass names should make the split obvious:

- `SceneSunLightShadowDynamicAtlasClearPass`
- `SceneSunLightShadowDynamicCascadePass_N`
- `SceneSunLightShadowMaskPass`
- `SceneDirectionalLightShadowAtlasClearPass_L`
- `SceneDirectionalLightShadowCascadePass_L_C`
- `SceneDirectionalLightShadowMaskPass_L`
- `SceneDeferredSunLightShadowedPass_L`
- `SceneDeferredDirectionalLightShadowedPass_L`

Debug view should always expose sunlight atlas/cache/mask when available. For ordinary directional lights, expose the last generated ordinary directional atlas and mask in the frame as `DirectionalLightShadowTransientAtlas` and `DirectionalLightShadowTransientMask`. A later debug UI can add per-light selection.

## Migration

The current `DirectionalShadowPass` should be renamed or split so the sunlight path becomes `SunLightShadowPass`. Tests and docs should stop describing the high-end path as applying to every directional light. Existing scene JSON without `"sunlight"` continues to load; environment scenes get one automatic sunlight if IBL metadata provides `dominant_light`.

`product/assets/scenes/Sandbox.scene.json` should keep the explicit `DirectionalLight` as an ordinary directional light (`sunlight=false`) and rely on `EnvironmentSunLight` for the single sunlight unless a task explicitly wants a hand-authored sunlight.

## Tests And Validation

Self-tests should cover:

- `LightComponent.sunlight` JSON load/save round trip.
- Scene validation rejects two directional sunlight entities.
- Environment metadata creates an `EnvironmentSunLight` with `sunlight=true`.
- RenderScene copies the sunlight flag into `VisibleLightData`.
- Sunlight shadow planning ignores ordinary directional lights.
- Ordinary directional shadow pass plans every matching ordinary directional light without a shared light-count budget.
- Ordinary directional pass uses `ShadowCasterMobilityFilter::All`.
- RenderGraph ordering places ordinary directional shadow depth and mask immediately before that light's shadowed lighting pass.

Final implementation validation should rebuild and smoke test Sandbox and Editor on Vulkan and DX12, because the change touches shared RenderGraph, deferred lighting, shadow rendering, and scene serialization paths.

## Open Implementation Notes

- The existing shadow mask shader can likely be shared by both pass classes if the cascade buffer layout remains unchanged.
- Storage buffer ownership should be explicit. If `DirectionalLightShadowPass` updates a shared cascade buffer per light while graph execution happens later, the buffer contents can be overwritten before the pass consumes it. Prefer per-light graph-visible buffers or immutable upload snapshots.
- RenderGraph resource aliasing is an optimization, not a correctness dependency. The first implementation should prioritize pass ordering and resource-state legality.
