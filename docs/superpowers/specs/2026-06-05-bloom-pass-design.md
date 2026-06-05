# Bloom Pass Design

Date: 2026-06-05

## Goal

Add a UE-style high-quality Gaussian bloom pass to the Engine deferred scene path.

The pass should run in linear HDR before tone mapping, be off by default for existing scenes, and be enabled in `product/assets/scenes/Sandbox.scene.json` for validation and visual tuning.

## Current Context

The static mesh scene path already produces `SceneDeferredSceneHDRLinear` as an `RGBA16_SFLOAT` RenderGraph transient after deferred lighting composite and optional sky background. `PostProcessToneMapPass` then reads that HDR texture and writes the final view output.

PostProcess is intentionally outside the material system. New post effects should be independent screen passes with their own shaders, pass classes, config structs, RenderGraph declarations, and debug view entries.

## Chosen Approach

Use a quality-focused Gaussian bloom chain, not FFT or convolution bloom.

The initial implementation will follow the useful parts of UE's standard Gaussian bloom model:

- thresholded bloom setup in HDR linear space
- soft-knee threshold behavior
- multi-resolution bloom levels matching the Bloom1..Bloom6 idea
- per-level `size` and `tint`
- global `intensity`, `quality`, and `size_scale`
- final bloom texture composited back into HDR before tone mapping
- Render Debug View access to setup, mip levels, final bloom, and composited HDR

FFT/convolution bloom is out of scope for the first pass because it requires kernel assets, more compute/UAV infrastructure, and additional authoring/debug workflows that are not needed to establish the baseline bloom feature.

## Configuration

Add a new Engine-side config module:

- `project/src/engine/Function/Render/BloomConfig.h`
- `project/src/engine/Function/Render/BloomConfig.cpp`

Add `BloomConfig bloom{}` to `SceneRenderConfig`.

Default config:

```cpp
struct BloomStageConfig
{
    float size = 1.0f;
    glm::vec3 tint = glm::vec3(1.0f);
};

enum class BloomQuality : uint8_t
{
    Low = 0,
    Medium,
    High,
    Epic
};

enum class BloomDebugView : uint8_t
{
    Off = 0,
    Setup,
    Mip1,
    Mip2,
    Mip3,
    Mip4,
    Mip5,
    Mip6,
    Final,
    CompositeHDR
};

struct BloomConfig
{
    bool enabled = false;
    BloomQuality quality = BloomQuality::High;
    float intensity = 0.6f;
    float threshold = 1.0f;
    float soft_knee = 0.5f;
    float size_scale = 1.0f;
    std::array<BloomStageConfig, 6> stages{};
    BloomDebugView debug_view = BloomDebugView::Off;
};
```

Sanitization rules:

- `intensity` clamps to `[0.0, 10.0]`
- `threshold` clamps to `[-1.0, 64.0]`, where `-1.0` disables thresholding
- `soft_knee` clamps to `[0.0, 1.0]`
- `size_scale` clamps to `[0.1, 8.0]`
- stage `size` clamps to `[0.0, 16.0]`
- stage tint clamps each channel to `[0.0, 8.0]`
- invalid quality/debug strings keep defaults and log a warning

Scene JSON schema:

```json
"bloom": {
  "enabled": true,
  "quality": "High",
  "intensity": 0.6,
  "threshold": 1.0,
  "soft_knee": 0.5,
  "size_scale": 1.0,
  "debug_view": "Off",
  "stages": [
    { "size": 0.3, "tint": [1.0, 1.0, 1.0] },
    { "size": 1.0, "tint": [1.0, 0.95, 0.9] },
    { "size": 2.0, "tint": [0.9, 0.95, 1.0] },
    { "size": 4.0, "tint": [0.8, 0.9, 1.0] },
    { "size": 8.0, "tint": [0.7, 0.8, 1.0] },
    { "size": 16.0, "tint": [0.6, 0.7, 1.0] }
  ]
}
```

`scene_config.bloom` belongs in scene JSON, not `Engine.ini`.

## Runtime Pass Structure

Add:

- `project/src/engine/Function/Render/BloomPass.h`
- `project/src/engine/Function/Render/BloomPass.cpp`
- `project/src/engine/Shaders/Deferred/BloomSetup.hlsl`
- `project/src/engine/Shaders/Deferred/BloomDownsample.hlsl`
- `project/src/engine/Shaders/Deferred/BloomUpsample.hlsl`
- `project/src/engine/Shaders/Deferred/BloomComposite.hlsl`

`BloomPass::add_passes()` accepts:

- `RenderGraphBuilder& graph`
- `const VisibleRenderFrame& frame`
- input HDR texture ref
- `const SceneRenderViewContext& view_context`
- `const BloomConfig& config`

It returns:

```cpp
struct BloomPassOutputs
{
    RenderGraphTextureRef scene_hdr_linear{};
    RenderGraphTextureRef setup{};
    std::array<RenderGraphTextureRef, 6> mips{};
    RenderGraphTextureRef final_bloom{};
    RenderGraphTextureRef composite_hdr{};
};
```

If bloom is disabled or `intensity <= 0`, `scene_hdr_linear` returns the original input and no bloom passes are added.

Pass chain:

```text
SceneDeferredSceneHDRLinear
 -> SceneBloomSetupPass
 -> SceneBloomDownsample1..N
 -> SceneBloomUpsample(N-1)..1
 -> SceneBloomCompositePass
 -> SceneDeferredToneMapPass
```

Quality maps to active mip count:

- Low: 3 mips
- Medium: 4 mips
- High: 5 mips
- Epic: 6 mips

All bloom intermediate textures use `RGBA16_SFLOAT`, `shader_resource=true`, `unordered_access=false`, and optimized black clear.

## Shader Behavior

`BloomSetup` samples HDR linear color and applies thresholding:

- threshold `-1.0`: pass all positive HDR color
- threshold `>= 0.0`: use luminance-based threshold
- soft knee smooths the threshold transition to avoid harsh cutoff

`BloomDownsample` performs filtered 2x downsample. The first downsample reads `SceneBloomSetup`; later levels read the previous mip.

`BloomUpsample` reads a lower-resolution bloom texture and the current higher-resolution bloom texture, upsamples with linear sampling, applies the configured stage tint, and uses stage `size * size_scale` as the sampling radius in source texels. Stage `size` does not change texture resolution; a stage with `size <= 0` contributes no bloom at that level.

`BloomComposite` adds final bloom into HDR:

```text
SceneBloomCompositeHDR = SceneHDRLinear + SceneBloomFinal * intensity
```

Tone mapping remains responsible for exposure, ACES, and optional manual sRGB encoding.

## Render Debug View

Register debug entries from `SceneRenderer` after bloom passes are added:

- `SceneBloomSetup`
- `SceneBloomMip1`
- `SceneBloomMip2`
- `SceneBloomMip3`
- `SceneBloomMip4`
- `SceneBloomMip5`
- `SceneBloomMip6`
- `SceneBloomFinal`
- `SceneBloomCompositeHDR`

Use `RenderDebugVisualization::LinearHDR` for all bloom HDR textures.

If `BloomConfig::debug_view != Off`, `BloomPass` will return the selected debug texture as `scene_hdr_linear` so it naturally flows through the existing tone-map/debug overlay chain. Process-level `RenderDebugView.Selected` remains available for explicit runtime selection.

## Scene Integration

`SceneRenderer` will own `BloomPass m_bloom_pass`.

Initialization order:

```text
DeferredLightingPass
SkyBackgroundPass
BloomPass
PostProcessToneMapPass
RenderDebugView
```

Rendering order:

```text
DeferredComposite
SkyBackground
Bloom
ToneMap
RenderDebugView override
Scene overlays
DebugDraw overlays
```

The exact insertion point is immediately after `SkyBackgroundPass::add_pass()` and before `PostProcessToneMapPass::add_pass()`.

## Tests

Add or update Engine self-tests for:

- `BloomConfig` default values and sanitization
- `SceneRenderConfig` equality including bloom
- scene JSON load/save round-trip for `scene_config.bloom`
- `RenderScene` copies bloom config into `VisibleRenderFrame`
- RenderGraph contract containing the expected bloom pass order between sky/background HDR and tone map
- debug view registry entries for bloom textures
- `product/config/Engine.ini` does not contain `[Bloom]`

Shader source contract tests should check that bloom shaders contain the expected entry points and root constant/sampler names.

## Documentation

Update after implementation:

- root `README.md`
- `docs/EngineDeveloperGuide.md`
- `docs/RenderGraphAPISpec.md`

These documents should describe the new deferred path as:

```text
SceneGBufferPass -> AO -> Lighting -> Composite -> SkyBackground -> Bloom -> ToneMap
```

and document `scene_config.bloom` as the scene-owned authority for bloom settings.

## Validation

Because bloom touches the shared RenderGraph/post-process path, implementation validation must cover:

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

Each run should use normal startup and graceful smoke-test shutdown. Debug-layer/validation errors, RenderGraph compiler errors, shutdown leaks, or backend mismatches are failures.

## Out Of Scope

- FFT bloom
- convolution/kernel bloom assets
- lens dirt mask
- anamorphic streaks
- local exposure integration
- editor UI for editing bloom settings
- TAA-aware or temporal bloom

These can be added after the Gaussian bloom baseline is stable.
