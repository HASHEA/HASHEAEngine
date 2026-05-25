# Sandbox 场景化与 SceneConfig 设计

日期：2026-05-25

## 决策

Sandbox 默认运行路径改为加载一个标准场景文件，而不是在 `SandboxStandardScene` 中硬编码模型、相机、灯光和环境配置。标准场景文件使用 `product/assets/scenes/Sandbox.scene.json`，其中同时保存 scene entities 和顶层 `scene_config`。

`scene_config` 承载跟随场景变化的渲染外观和质量设置。第一版迁移以下配置：

- `AmbientOcclusion`
- `DirectionalShadows`
- `RenderDebugView`

环境贴图、强度、旋转、是否显示背景和是否影响 lighting 继续作为 scene 内容，由当前已有的 `EnvironmentComponent` 跟随场景文件保存和加载。

以下配置继续留在 `product/config/Engine.ini`：

- `RHI.Backend`
- Vulkan / DX12 validation
- `Rendering.VSync`
- `EnvironmentLighting` runtime cache 和 `.ashibl` bake defaults

这样划分后，`Engine.ini` 保持机器级、进程级和启动前配置；场景文件负责场景内容、环境和该场景的渲染表现。

## 目标

- Sandbox 只负责启动 runtime、初始化资产数据库、加载 `Sandbox.scene.json` 并绑定 scene presentation。
- 标准 Sandbox 场景包含 Sponza、主相机、directional / point / spot 灯光和 active environment。
- AO、方向光阴影和 Render Debug View 的默认值随场景文件保存和加载。
- Scene renderer 消费 per-scene config snapshot，不再把这些设置当作固定进程全局状态。
- 保持 Engine / Editor 边界，第一版不修改 `project/src/editor`。
- 保持旧场景文件可加载；缺失 `scene_config` 时使用 Engine 默认值。

## 非目标

- 不迁移 `RHI.Backend` 或 validation 配置。
- 不迁移 `Rendering.VSync`。VSync 影响 swapchain present mode，仍属于窗口 / 进程启动前配置。
- 不迁移 `[EnvironmentLighting]` bake/cache defaults。它们描述 `.ashibl` 生成和 cache 策略，不是单个 scene 的最终视觉状态。
- 不在第一版实现 Editor 属性面板或保存 UI。
- 不保留 glTF 模型下拉作为 Sandbox 默认路径。后续如果还需要，可以另做 scene 下拉或显式 debug override。

## SceneConfig Schema

场景文件顶层新增：

```json
"scene_config": {
  "ambient_occlusion": {
    "mode": "HBAO",
    "quality": "Medium",
    "radius": 1.5,
    "intensity": 1.0,
    "power": 1.0,
    "half_resolution": true,
    "blur": true,
    "temporal": true,
    "temporal_blend": 0.85,
    "temporal_depth_threshold": 0.01,
    "temporal_normal_threshold": 0.75
  },
  "directional_shadows": {
    "enabled": true,
    "default_cascade_count": 4,
    "default_shadow_distance": 160.0,
    "near_shadow_distance": 16.0,
    "split_lambda": 0.65,
    "near_cascade_resolution": 2048,
    "outer_cascade_resolution": 1024,
    "dynamic_atlas_size": 4096,
    "static_cache_atlas_size": 4096,
    "static_cache_budget_mb": 64,
    "depth_bias": 0.0015,
    "normal_bias": 0.05,
    "pcf_radius": 1
  },
  "render_debug_view": {
    "enabled": false,
    "selected": "SceneDeferredSceneHDRLinear"
  }
}
```

Field names use snake_case because scene JSON already uses `rotation_euler_degrees`, `material_overrides` and similar JSON-facing names. Runtime C++ structs may keep the existing C++ naming style.

`AmbientOcclusion.DebugView` is intentionally not part of the first `scene_config` contract. The system-level `render_debug_view` replaces it for normal debugging, while the existing AO debug enum can remain an internal compatibility path until removed.

## Engine Types

Add an Engine-facing scene config type under `Function/Scene`:

```cpp
struct SceneRenderConfig
{
    AmbientOcclusionConfig ambient_occlusion{};
    DirectionalShadowConfig directional_shadows{};
    RenderDebugViewConfig render_debug_view{};
};
```

`Scene` owns one `SceneRenderConfig` value in its storage. Public API should expose:

```cpp
const SceneRenderConfig& Scene::get_render_config() const;
bool Scene::set_render_config(const SceneRenderConfig& config);
uint64_t Scene::get_render_config_version() const;
```

Changing render config increments `change_version` and a new `render_config_version`. It must not increment primitive, transform, light or environment versions.

`SceneRenderConfig` lives in Scene/Function space, but it can reuse the existing typed render config structs. If include coupling becomes too wide, create a small `Function/Scene/SceneConfig.h` facade that forward-declares or includes only the needed render config headers.

## Serialization

`Scene::load_from_file()` reads top-level `scene_config` after file version validation and before marking the scene clean.

Rules:

- Missing `scene_config` uses defaults from `make_default_*_config()`.
- Missing nested fields keep defaults.
- Invalid enum strings or out-of-range values keep defaults and log a warning.
- Save always writes a complete `scene_config`, so newly saved scenes become self-contained.
- Existing scene file version increments from 3 to 4.

The config parsers should be shared with the current INI loading code where practical. Avoid duplicating enum token normalization and clamp rules between INI parsing and JSON scene parsing. If the current parser helpers are cpp-local, move common parse / clamp helpers to the corresponding config module API or add explicit `load_*_config_from_json(...)` helpers near scene serialization.

## Render Snapshot

Per-scene render config should travel with the same immutable frame data as the rest of scene presentation:

```text
Scene
  -> ScenePresentationSubsystem
  -> RenderScene
  -> VisibleRenderFrame
  -> SceneRenderer
```

`VisibleRenderFrame` gets a `SceneRenderConfig render_config` snapshot. `RenderScene` refreshes it when `Scene::get_render_config_version()` changes. `SceneRenderer` then passes the frame config to AO, directional shadows and Render Debug View.

This avoids using `set_runtime_ambient_occlusion_config()` or `set_runtime_directional_shadow_config()` when loading a scene. Those process-level setters may remain as fallback/default initialization paths for non-scene code, tests and transitional compatibility, but scene rendering should prefer the frame snapshot.

## Render Module Changes

`AmbientOcclusionPass` should not cache one immutable config only at initialize time. It should either:

- accept `const AmbientOcclusionConfig&` in `add_passes(...)`, or
- update its cached config from the frame snapshot at the start of `add_passes(...)`.

The first option is preferred because it makes the dependency explicit.

`DirectionalShadowPass` already refreshes runtime config in `add_depth_passes(...)`; change it to accept `const DirectionalShadowConfig&` from the frame. Static cache resources whose size depends on `static_cache_atlas_size` may need recreation if a scene changes to an incompatible atlas size. First implementation can clear/recreate the static cache atlas when size-related fields change.

`RenderDebugView` should use `frame.render_config.render_debug_view` for pass insertion. UI changes can still update the active process selection for temporary debugging, but the scene default comes from `scene_config`. First implementation may treat UI selection as an in-memory override layered over the scene default; it must not write back to the scene file unless a future Editor workflow explicitly saves it.

## Environment Settings

Environment visual settings remain scene content through `EnvironmentComponent`:

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

`Sandbox.scene.json` should contain an environment entity with the Citrus Orchard `.ashibl` and HDR source paths. The `[EnvironmentLighting]` INI section stays as bake/cache policy. This keeps authored environment appearance in the scene while preserving machine-level cache behavior in `Engine.ini`.

## Sandbox Flow

`SandboxStandardScene` becomes a scene loader / runtime wrapper:

```text
initialize asset database
load product/assets/scenes/Sandbox.scene.json
ensure model and referenced assets are discoverable
tick free camera on the scene primary camera
register window output + primary camera scene binding
```

The class should stop constructing Sponza, lights and environment in code. It should also stop using `ASH_SANDBOX_MODEL` as the primary startup path. If a developer needs ad hoc model validation, add a future explicit mode or scene override instead of keeping it in the standard path.

The free camera controller remains Sandbox-owned runtime behavior. It can bind to the loaded scene's primary camera entity and mutate that camera transform during Sandbox execution. This is runtime control, not authoring.

## Standard Scene Asset

Add `product/assets/scenes/Sandbox.scene.json` with:

- scene name `Sandbox`
- root entity
- Sponza model entities or an asset-instantiated equivalent
- primary camera
- directional, point and spot light entities
- active environment entity
- `scene_config` matching current Sandbox visual defaults

If generating full Sponza node entities directly in the scene file is too large for the first implementation, use an Engine-supported scene reference form only if that form already exists. Otherwise, instantiate from the glTF once and save the resulting scene file through `Scene::save_to_file()`.

## Error Handling

Use the existing process-error style:

- `ASH_PROCESS_GUARD_RETURN`
- `ASH_PROCESS_ERROR`
- `ASH_LOG_PROCESS_ERROR`

Scene config parse failure must not make the whole scene unloadable unless the JSON root itself is invalid. Bad individual fields fall back to defaults with warnings. Sandbox should fail startup if `Sandbox.scene.json` is missing or cannot load, because the standard runtime path depends on that scene.

## Testing

Add or extend Engine self-tests for:

- scene config defaults when the JSON block is missing
- AO mode / quality parsing from scene JSON
- directional shadow numeric clamp behavior from scene JSON
- render debug view selected string preservation
- scene save writes `scene_config`
- scene load/save round-trip preserves config
- loading old version 3 scene files still works

Runtime validation after implementation:

- build Debug x64
- `Sandbox --engine-self-test`
- `Sandbox + Vulkan` smoke
- `Sandbox + DX12` smoke
- `Editor + Vulkan` smoke
- `Editor + DX12` smoke

Because the implementation touches Scene, ScenePresentation, Renderer and shared render passes, Vulkan and DX12 must both pass before completion.

## Documentation

Implementation must update:

- `README.md`
- `docs/EngineDeveloperGuide.md`
- any relevant scene presentation or render config docs if the public data flow changes there

`Engine.ini` documentation should explicitly say that AO, DirectionalShadows and RenderDebugView are no longer authoritative scene defaults once `scene_config` is present.
