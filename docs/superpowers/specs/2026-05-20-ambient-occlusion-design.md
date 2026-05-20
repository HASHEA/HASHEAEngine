# Ambient Occlusion Design

Date: 2026-05-20

## Decision

Add a dedicated Engine-side ambient occlusion module for the deferred scene path. The module will expose one RenderGraph AO input for lighting, named `SceneAmbientOcclusion`, while allowing runtime selection between `Off`, `SSAO`, `HBAO`, and `GTAO` through `product/config/Engine.ini`.

The implementation unit should follow the existing pass naming style and live under `project/src/engine/Function/Render` as `AmbientOcclusionPass.*`. If the implementation grows beyond pass orchestration and shader resource ownership, a later refactor may introduce internal helper files, but `SceneRenderer` should only depend on the pass facade.

## Goals

- Support `Off`, `SSAO`, `HBAO`, and `GTAO` modes from runtime config.
- Keep the main deferred graph stable by producing one AO texture regardless of selected algorithm.
- Keep algorithm details out of `SceneRenderer` and `DeferredLightingPass`.
- Use current GBuffer/depth data: D32 scene depth, GBufferE normal octahedral encoding, and GBufferB material AO.
- Preserve Vulkan and DX12 shared-path legality through RenderGraph resource declarations instead of backend-specific barriers.
- Make the first GTAO implementation spatial-only. Temporal history, motion-vector reprojection, and bent normals are explicitly deferred.

## Non-Goals

- No Editor-specific UI or panel changes.
- No new backend-specific RHI API.
- No ray-traced AO.
- No temporal accumulation in the first implementation.
- No changes to material authoring semantics beyond preserving material AO in `GBufferB.b`.

## Current Context

The scene deferred path is currently:

```text
SceneGBufferPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredCompositePass
  -> SceneDeferredToneMapPass
```

DeferredHQ GBuffer already provides:

- `SceneDeferredDepth`: sampled D32 depth.
- `GBufferB.b`: material ambient occlusion.
- `GBufferE.rg`: oct-encoded normal.

`DeferredCommon.hlsli` decodes material AO into `AshDeferredSurface.ao` and currently multiplies it into dynamic direct lighting. AO integration should preserve that behavior by computing:

```text
finalAO = materialAO * screenAO
```

where `screenAO` comes from `SceneAmbientOcclusion`. When AO mode is `Off`, the module returns a neutral 1x1 white AO texture so the lighting path still binds the same shader resource and behaves as if `screenAO == 1`.

## RenderGraph Shape

The target graph is:

```text
SceneGBufferPass
  -> SceneAmbientOcclusionPass
  -> SceneDeferredLightingAccumPass
  -> SceneDeferredCompositePass
  -> SceneDeferredToneMapPass
```

`AmbientOcclusionPass::add_passes()` will:

1. Inspect `AmbientOcclusionConfig`.
2. Return an external neutral AO texture when mode is `Off`, or a graph transient `SceneAmbientOcclusion` texture when enabled.
3. Declare reads from scene depth and GBufferE for enabled modes.
4. Write a single-channel or RGBA fallback AO target, depending on format support already exposed by `RenderTextureFormat`.
5. Optionally register a blur pass if `Blur=true`.

`DeferredLightingPass::add_passes()` will accept a valid AO texture ref. The lighting pass declares it as `GraphicsSRV`, binds it to the lighting shaders, and multiplies it with material AO in `AshDecodeDeferredSurface` or immediately after surface decode. The shader binding surface stays the same across all AO modes.

## Configuration

Add a typed AO config loader in Function/Render, separate from the existing boolean-only `RenderSwitch` table:

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

Supported values:

- `Mode`: `Off`, `SSAO`, `HBAO`, `GTAO`.
- `Quality`: `Low`, `Medium`, `High`.
- `Radius`: world/view-space sampling radius, clamped to a safe positive range.
- `Intensity`: occlusion strength multiplier.
- `Power`: final AO contrast curve.
- `HalfResolution`: first implementation may parse this but can run full resolution until downsample/upsample support is added.
- `Blur`: enables the denoise/blur pass.

Invalid enum or numeric values should log a warning and fall back to defaults. The default should be `Mode=Off` until validation and visual tuning are complete; after the implementation is stable, the default can be changed deliberately.

## Module Boundary

`AmbientOcclusionPass` owns:

- AO config snapshot for render-time use.
- Graphics or compute programs for SSAO/HBAO/GTAO.
- Neutral 1x1 white AO texture for `Off` mode.
- Shared samplers.
- AO output creation.
- AO pass and optional blur pass registration.
- Root constants and shader binding.

`SceneRenderer` owns only:

- Initializing and shutting down `AmbientOcclusionPass`.
- Calling `add_passes()` between GBuffer and deferred lighting.
- Passing the returned AO texture ref to `DeferredLightingPass`.

`DeferredLightingPass` owns only:

- Declaring the AO SRV dependency.
- Binding the AO texture and sampler.
- Applying `materialAO * screenAO`.

It must not know which AO algorithm produced the texture.

## Algorithm Modes

### SSAO

SSAO is the baseline and fallback mode. It reconstructs position from depth, uses GBuffer normals, samples nearby depths with a small kernel, and estimates occlusion from local depth disagreement.

Use it for:

- First visual validation.
- Low-end quality preset.
- Debugging the AO graph and binding path.

Expected issues:

- Noise without blur.
- Haloing around depth discontinuities.
- Strong dependence on radius and bias.

### HBAO

HBAO performs horizon search along several screen-space directions. For each direction, it estimates how much the surrounding depth profile hides the hemisphere.

Use it for:

- Medium/high preset with sharper crevice response.
- Better contact shadows than SSAO without requiring temporal history.

Expected issues:

- More expensive than SSAO.
- Directional banding if direction count is too low.
- Sensitive to depth linearization and edge falloff.

### GTAO

GTAO approximates a more physically grounded hemisphere visibility integral using screen-space slices and horizon terms. The first implementation should be spatial-only and share the same input/output contract as SSAO/HBAO.

Use it for:

- Highest quality mode.
- Long-term base for future bent-normal and indirect-lighting occlusion.

Expected issues:

- Highest implementation complexity.
- Needs careful denoising.
- Full temporal GTAO requires history resources and motion vectors, which are out of scope for this first design.

## Shader Organization

Use dedicated Engine shaders under `project/src/engine/Shaders/Deferred`:

- `AmbientOcclusionCommon.hlsli`
- `AmbientOcclusionSSAO.hlsl`
- `AmbientOcclusionHBAO.hlsl`
- `AmbientOcclusionGTAO.hlsl`
- `AmbientOcclusionBlur.hlsl` if blur is enabled

Shared shader code should contain:

- UV/depth sampling helpers.
- reverse-Z aware depth background handling.
- position reconstruction from `AshInvViewProjection`.
- normal decode from GBufferE.
- AO parameter constants.

The first implementation should prefer raster fullscreen passes for all three algorithms unless compute becomes necessary for blur or half-resolution support. This keeps the pass path close to existing `DeferredLightingPass` and `PostProcessToneMapPass`.

## Resource Format

Preferred AO target format is a single-channel UNORM format if `RenderTextureFormat` exposes one during implementation. If not, use `RGBA8_UNORM` initially and store AO in `.r`, with `.gba` unused. This avoids expanding RHI format support just to land the feature.

`SceneAmbientOcclusion` should be shader-readable. It does not need UAV unless a compute path is selected.

## Error Handling

Use the project process-error style:

- `ASH_PROCESS_GUARD_RETURN`
- `ASH_PROCESS_ERROR`
- `ASH_LOG_PROCESS_ERROR` where logging context is useful

If AO config requests an unavailable mode or resource creation fails, fail the graph pass loudly rather than silently presenting partially invalid output. The only silent neutral path should be explicit `Mode=Off`.

## Profiling And Debuggability

Every AO graph pass must have:

- Stable pass names: `SceneAmbientOcclusionPass`, `SceneAmbientOcclusionBlurPass`.
- `ASH_PROFILE_SCOPE_NC()` in execute lambdas or delegated functions.
- Useful value plots for algorithm mode, quality, sample count, and resolution when practical.

Shader and RHI debug names should include the selected mode, such as `SceneAmbientOcclusionGTAO`.

## Validation

Because this touches shared scene rendering, shader bindings, RenderGraph resources, and deferred lighting, validation must cover:

- `Sandbox` on Vulkan.
- `Sandbox` on DX12.
- `Editor` on Vulkan.
- `Editor` on DX12.

Each backend should be tested with:

- `Mode=Off`.
- `Mode=SSAO`.
- `Mode=HBAO`.
- `Mode=GTAO`.

At minimum, implementation verification must confirm:

- RenderGraph compiler keeps the AO producer when lighting consumes it.
- Vulkan validation and DX12 debug layer report no resource-state or descriptor errors.
- Output is not black/white due to missing AO binding.
- `finalAO = materialAO * screenAO` does not double-darken unlit/emissive-only shading.
- reverse-Z cameras reconstruct position correctly.

## Documentation Updates For Implementation

When this design is implemented, update:

- `README.md`: current deferred path and documentation links.
- `docs/EngineDeveloperGuide.md`: AO config, RenderGraph placement, shader/binding behavior, validation notes.
- `docs/RenderGraphAPISpec.md`: example graph chain if the AO pass becomes part of the documented scene path.
