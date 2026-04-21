# ScenePresentationSubsystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce an engine-owned `ScenePresentationSubsystem` so `Sandbox`, `Editor`, and future clients only declare `Scene + Camera + Output` bindings while Engine owns scene sync, output allocation, scene submission, and UI surface presentation.

**Architecture:** Add a new Engine-facing presentation subsystem under `Application`, keep `RenderScene` / `SceneView` / `VisibleRenderFrame` / `SceneRenderer` internal, and migrate `Sandbox` and scene-driven `Editor` viewports onto persistent output/binding handles. The first version keeps internal sync simple: use scene change-version tracking plus per-scene full `RenderScene::rebuild_from_scene(...)` fallback while preserving the longer-term incremental-sync public shape.

**Tech Stack:** C++17, AshEngine `Function/Application`, `Function/Render`, `Function/Scene`, `Function/Gui`, Sandbox standard-scene runtime, Editor viewport service/panels, Vulkan + DX12 runtime validation.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
  - expose `get_scene_presentation()`, own the new subsystem, and add presentation update/submit phase hooks
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`
  - initialize/shutdown the subsystem and run it in the correct logic/render phases
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
  - expose a render-safe scene change version accessor
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`
  - maintain a monotonic change version without reusing `mark_clean()` as render-sync state
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
  - expose explicit camera-entity view construction
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`
  - implement camera-entity view construction and keep `build_primary_scene_view()` as a convenience wrapper
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UICommon.h`
  - add the opaque UI surface handle type used by scene presentation
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UIContext.h`
  - expose surface-handle drawing helpers for scene presentation outputs
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UIContext.cpp`
  - resolve engine-owned scene surfaces to existing render-target UI presentation without leaking `RenderTarget` to upper layers
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
  - replace manual visible-frame/render submission ownership with scene presentation handles
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
  - register window output + scene binding and stop manually touching `Renderer` / `SceneRenderer`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
  - trim render-side packet ownership and expose stable scene access for binding registration
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
  - keep Sandbox focused on scene/runtime state only
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Core\EditorContext.h`
  - replace per-viewport `RenderTarget` exposure with scene presentation surface/output/binding handles
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Services\EditorViewportService.h`
  - move viewport render bookkeeping to persistent scene-presentation declarations
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Services\EditorViewportService.cpp`
  - create/update/destroy scene outputs and view bindings for Editor scene-driven viewports
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Panels\ViewportPanel.cpp`
  - draw engine-owned scene surfaces instead of sampling editor-owned `RenderTarget`s
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Editor.h`
  - remove editor-owned `RenderScene` / viewport RT submission responsibilities
- `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Editor.cpp`
  - stop driving renderer and scene submission directly for scene-driven viewports
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document the new engine-facing scene presentation entry point and lifecycle
- `D:\workspace\AshEngine\HASHEAEngine\docs\EditorDeveloperGuide.md`
  - document the new Editor viewport integration boundary
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineUIContext.md`
  - document surface-handle presentation in `UIContext`

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationHandles.h`
  - define `SceneOutputHandle`, `SceneViewBindingHandle`, and `UISurfaceHandle`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.h`
  - define the public API, descriptors, overrides, and subsystem interface
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`
  - implement descriptor storage, scene sync, packet preparation, output allocation, and render-thread submission

## Task 1: Add Stable Scene Change Tracking And Explicit Camera View Building

**Files:**
- Create: none
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`

- [ ] Add a monotonic `Scene::get_change_version() const` accessor that increments on every structural/component mutation and does **not** depend on `mark_clean()`.
- [ ] Keep `Scene::is_dirty()` / `mark_clean()` semantics intact for save/editor workflows; scene presentation must not clear them.
- [ ] Introduce `build_scene_view_for_camera_entity(const Scene& scene, EntityId camera_entity_id, const SceneViewDesc& desc, SceneView& out_view)`.
- [ ] Refactor `build_primary_scene_view(...)` to reuse the explicit camera-entity path once it resolves the chosen camera.
- [ ] Verify with a focused Engine build that:
  - the new accessor compiles cleanly
  - existing `Sandbox` / `Editor` callers of `build_primary_scene_view()` still compile

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine` builds successfully
- no Editor save-dirty API is removed or repurposed

## Task 2: Introduce The Public Scene Presentation API And Application Integration

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationHandles.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`

- [ ] Define the public API shape approved in the design:
  - `SceneOutputHandle`
  - `SceneViewBindingHandle`
  - `SceneOutputDesc`
  - `SceneCameraSelector`
  - `SceneViewOverrides`
  - `SceneViewBindingDesc`
  - `ScenePresentationSubsystem`
- [ ] Keep `Renderer`, `RenderScene`, `SceneView`, `VisibleRenderFrame`, and `SceneRenderer` as internal details; do not expose them through the new subsystem API.
- [ ] Implement descriptor storage for outputs and bindings plus stable internal ids for UI surfaces.
- [ ] Add `Application::get_scene_presentation()` and own one subsystem instance in `Application`.
- [ ] Initialize the subsystem after `Renderer` / `SceneRenderer` are ready and shut it down before render resources disappear.
- [ ] Add fixed presentation phases:
  - update phase after user scene mutation on the logic thread when enabled, otherwise on the main/update thread
  - submit phase inside the default engine render frame before `_on_gui()`
- [ ] Keep the default `Application::_on_render()` as:
  - `begin_frame()`
  - `_on_render_debug()`
  - presentation submit phase
  - `_on_gui()`
  - `end_frame()`

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine` builds
- `Application` now exposes a non-null `ScenePresentationSubsystem` during runtime initialization

## Task 3: Implement Scene Sync, Output Allocation, And Render-Thread Submission

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`

- [ ] Implement internal state buckets:
  - per-scene `RenderScene` cache keyed by `Scene*`
  - per-output offscreen/window state plus UI surface mapping
  - per-binding persistent declaration and prepared packet state
- [ ] In the update phase:
  - resolve output extents
  - resolve camera selector
  - rebuild per-scene `RenderScene` when the scene change version changed or refresh was requested
  - build one `VisibleRenderFrame` per active binding
  - publish immutable prepared packets for the render thread
- [ ] In the submit phase:
  - finalize pending render assets on the render thread
  - resolve `Window` outputs to the current back buffer
  - lazily create/recreate `Offscreen` outputs as engine-owned `RenderTarget`s
  - derive `SceneRenderViewContext`
  - submit via `SceneRenderer::render_visible_frame(...)`
- [ ] Preserve the current first-version limitation:
  - viewport/scissor limit rasterization only
  - clear is full-attachment clear
  - later bindings on the same output should default to load/preserve, not clear
- [ ] On per-binding failures, log and continue with other bindings rather than failing the whole frame.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine` builds
- no direct render-thread readback of `Scene` / `entt` is introduced

## Task 4: Add UI Surface Presentation Without Leaking Render Targets

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UICommon.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UIContext.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Gui\UIContext.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`

- [ ] Add `UISurfaceHandle` to the engine UI/public handle vocabulary.
- [ ] Expose UI helpers that accept `UISurfaceHandle` instead of `std::shared_ptr<RenderTarget>`, at minimum:
  - `image_surface(...)`
  - `draw_surface_fill_available(...)`
- [ ] Keep `UIContext` backend-agnostic by resolving a `UISurfaceHandle` back to the current offscreen `RenderTarget` inside Engine code only.
- [ ] Do not expose the underlying `RenderTarget` to Editor/Sandbox callers.
- [ ] Return an invalid `UISurfaceHandle` for `Window` outputs because back-buffer presentation is not a UI-sampled surface.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `UIContext` builds cleanly
- existing render-target UI helpers still compile
- new surface helpers compile without adding Editor-specific semantics into `UIContext`

## Task 5: Migrate Sandbox To Scene-Only Ownership

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] Remove Sandbox-side ownership of:
  - `RenderScene`
  - `SceneView`
  - `VisibleRenderFrame`
  - `SceneRenderer` submission
- [ ] Keep Sandbox focused on:
  - asset database initialization
  - standard-scene async load
  - camera/controller logic
  - scene mutation only
- [ ] Register one `Window` output and one persistent scene binding through `Application::get_scene_presentation()`.
- [ ] Bind Sandbox to its standard scene using `PrimaryCamera` in V1, with the subsystem already supporting `EntityId` for future use.
- [ ] Stop overriding render/present just to drive scene submission manually.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Sandbox` builds
- Sandbox no longer calls `Renderer::begin_frame()`, `Renderer::end_frame()`, or `SceneRenderer::render_visible_frame(...)` for its main scene path

## Task 6: Migrate Editor Scene Viewports To Engine-Owned Outputs And Surfaces

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Core\EditorContext.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Services\EditorViewportService.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Services\EditorViewportService.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Panels\ViewportPanel.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Editor.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\editor\Editor.cpp`

- [ ] Replace viewport-owned `RenderTarget` state with:
  - `SceneOutputHandle`
  - `SceneViewBindingHandle`
  - `UISurfaceHandle`
- [ ] Keep `ViewportPanel` responsible only for:
  - requested pixel size
  - panel open/focus/hover state
  - toolbar/UI semantics
  - displaying the engine-owned UI surface
- [ ] Move scene presentation declaration logic into `EditorViewportService`.
- [ ] Use persistent output/binding registration for the current scene and default viewport camera selector.
- [ ] Remove the old Editor render path that rebuilt `RenderScene`, built `SceneView`, built `VisibleRenderFrame`, and called `SceneRenderer` directly.
- [ ] Remove the old unsafe back-buffer sampling warning path because scene viewports should no longer sample the swapchain back buffer directly.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Editor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Editor` builds
- scene-driven Editor viewports no longer touch `RenderScene`, `SceneView`, `VisibleRenderFrame`, `SceneRenderer`, or viewport-owned `RenderTarget`s

## Task 7: Update Docs And Validate The Shared Runtime Path

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EditorDeveloperGuide.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineUIContext.md`
- Modify: any earlier files if validation reveals fixes are required

- [ ] Document the new engine-facing flow:
  - `Application::get_scene_presentation()`
  - persistent output/binding model
  - scene change-version based sync in V1
  - `UIContext` surface-handle display for scene-driven viewports
- [ ] Keep the docs explicit that custom/non-scene renderers still use the old `Renderer`-driven path for now.
- [ ] Run the full AshEngine validation loop after implementation.
- [ ] If validation fails, fix the real issue and rerun until the full matrix is clean.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\huyizhou\.codex\skills\ash-engine-validation-loop\scripts\run-validation-loop.ps1" -Configuration Debug
```

Expected:

- `premake5.exe vs2022` regeneration succeeds if needed
- `AshEngine.sln` builds
- `Sandbox` Vulkan passes
- `Sandbox` DX12 passes
- `Editor` Vulkan passes
- `Editor` DX12 passes
- no validation/debug-layer/leak regressions remain

## Notes

- This plan intentionally does **not** include any `git add`, `git commit`, or `git push` step because the user will handle all submission operations personally.
- If a suggested commit message is needed after implementation, provide it in conversation only.
