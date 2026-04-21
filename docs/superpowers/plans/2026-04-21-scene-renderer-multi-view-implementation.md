# SceneRenderer Multi-View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add first-version multi-view scene submission support so one frame can submit multiple `SceneView`s with independent output/depth/viewport/scissor, while keeping the current single-view Sandbox path working.

**Architecture:** Keep the current “one `VisibleRenderFrame` per logical `SceneView`” model and add a new render-thread-only `SceneRenderViewContext` that carries per-view output attachments and raster state. `SceneRenderer` stops owning one global depth target and instead consumes either an explicit per-view depth target or an internal scratch-depth cache keyed by attachment size/format.

**Tech Stack:** C++, AshEngine `Function/Render` facade layer, existing `Renderer`/`RenderDevice` pass submission path, Sandbox standard-scene runtime, Vulkan + DX12 smoke validation.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
  - remove `VisibleRenderFrame::output_target` and tighten the frame packet to scene-visible data only
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
  - remove output-target ownership from `build_visible_render_frame()`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
  - switch the public submit API to `render_visible_frame(frame, view_context)` and replace the single `m_depth_target` member
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
  - build scene passes from the new per-view context and implement explicit/scratch depth resolution
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
  - stop passing `output_target` into logical frame construction; keep back-buffer size usage only
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
  - construct the default single-view `SceneRenderViewContext` during render-thread submission
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document per-view scene submission, the new depth ownership rule, and the first-version load/clear limitation

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderView.h`
  - define `SceneRenderViewContext` and any small helper defaults for per-view scene submission

## Task 1: Introduce The Per-View Submission Contract

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderView.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`

- [ ] **Step 1: Write the failing call site first**

Change the Sandbox render submit path so it tries to use the new API shape before the Engine side exists yet:

```cpp
AshEngine::SceneRenderViewContext view_context{};
view_context.debug_name = "SandboxStandardSceneView";
view_context.output_target = output_target;
view_context.color_load_action = AshEngine::RenderLoadAction::Clear;
view_context.color_clear_value = { 0.025f, 0.03f, 0.05f, 1.0f };
view_context.depth_load_action = AshEngine::RenderLoadAction::Clear;
view_context.depth_clear_value = { 1.0f, 0u };

ASH_PROCESS_ERROR(get_scene_renderer().render_visible_frame(*m_activeVisibleFrame, view_context));
```

- [ ] **Step 2: Run a focused build and verify it fails for the expected reason**

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

- compile failure because `SceneRenderViewContext` does not exist yet
- compile failure because `SceneRenderer::render_visible_frame(frame, view_context)` overload does not exist yet

- [ ] **Step 3: Add the new per-view context type**

Create `SceneRenderView.h` with a focused struct that holds only render-thread submission state:

```cpp
#pragma once

#include "Function/Render/RenderDevice.h"
#include <memory>

namespace AshEngine
{
    struct ASH_API SceneRenderViewContext
    {
        const char* debug_name = nullptr;
        std::shared_ptr<RenderTarget> output_target = nullptr;
        std::shared_ptr<RenderTarget> depth_target = nullptr;
        bool has_viewport = false;
        RenderViewport viewport{};
        bool has_scissor = false;
        RenderScissor scissor{};
        RenderLoadAction color_load_action = RenderLoadAction::Clear;
        RenderColorValue color_clear_value{ 0.025f, 0.03f, 0.05f, 1.0f };
        RenderLoadAction depth_load_action = RenderLoadAction::Clear;
        RenderDepthStencilValue depth_clear_value{ 1.0f, 0u };
    };
}
```

- [ ] **Step 4: Update the public `SceneRenderer` contract**

In `SceneRenderer.h`, include the new header and replace the old signature with:

```cpp
bool render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context);
```

Do not keep the old one-argument overload alive. Remove it so old ownership assumptions cannot silently survive.

- [ ] **Step 5: Re-run the focused build**

Run the same `MSBuild.exe ... /t:Sandbox` command again.

Expected:

- the missing-type / missing-overload errors are gone
- the next compile failures should now point at `VisibleRenderFrame::output_target` / `build_visible_render_frame(..., output_target, ...)` usages that still need to be removed

## Task 2: Remove Render-Target Ownership From `VisibleRenderFrame`

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] **Step 1: Remove `output_target` from the frame packet**

Update `VisibleRenderFrame` to keep only scene-visible data:

```cpp
struct ASH_API VisibleRenderFrame
{
    uint64_t frame_index = 0;
    SceneView* debug_view = nullptr;
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::mat4 view_projection{ 1.0f };
    glm::vec3 camera_position{ 0.0f };
    std::vector<VisibleStaticMeshDraw> static_mesh_draws{};
};
```

- [ ] **Step 2: Change `RenderScene::build_visible_render_frame()` to stop taking an output target**

Use this target signature:

```cpp
bool build_visible_render_frame(
    uint64_t frame_index,
    const SceneView& view,
    VisibleRenderFrame& out_frame) const;
```

The function should still copy `view`, `projection`, `view_projection`, `camera_position`, and visible draw data. It should not know anything about render attachments.

- [ ] **Step 3: Update the Sandbox logical frame builder**

In `SandboxStandardScene.cpp`, keep the back buffer lookup only for viewport sizing:

```cpp
const std::shared_ptr<AshEngine::RenderTarget> back_buffer = m_renderer->get_back_buffer();
view_desc.viewport_width = back_buffer->get_width();
view_desc.viewport_height = back_buffer->get_height();
```

But change the actual frame build call to:

```cpp
if (!io_snapshot.render_scene.build_visible_render_frame(
        frame_index,
        io_snapshot.latest_scene_view,
        visible_frame))
{
    out_error = "Failed to build a VisibleRenderFrame for the Sandbox standard scene.";
    ASH_PROCESS_ERROR(false);
}
```

- [ ] **Step 4: Remove any remaining assignments to `visible_frame.output_target`**

Delete old writes such as:

```cpp
m_activeVisibleFrame->output_target = output_target;
```

That state now belongs only in `SceneRenderViewContext`.

- [ ] **Step 5: Rebuild Sandbox and make sure the contract compiles cleanly**

Run the same focused `MSBuild.exe ... /t:Sandbox` command again.

Expected:

- `VisibleRenderFrame` no longer exposes `output_target`
- the logical frame builder no longer requires a render target parameter
- any remaining compile failures should now be isolated to `SceneRenderer.cpp`

## Task 3: Implement Per-View Scene Submission And Scratch Depth Caching

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`

- [ ] **Step 1: Replace the single depth member with an internal scratch-depth cache**

In `SceneRenderer.h`, replace:

```cpp
std::shared_ptr<RenderTarget> m_depth_target = nullptr;
```

with a keyed cache owned by `SceneRenderer`, for example:

```cpp
struct ScratchDepthKey
{
    uint32_t width = 0;
    uint32_t height = 0;
    RenderTextureFormat format = RenderTextureFormat::Unknown;
};

std::vector<std::pair<ScratchDepthKey, std::shared_ptr<RenderTarget>>> m_scratch_depth_targets{};
```

Use a simple vector or map. The important point is that the cache is keyed per attachment size/format, not globally singleton.

- [ ] **Step 2: Add a private helper that resolves the depth target for one view**

Implement a helper with behavior equivalent to:

```cpp
std::shared_ptr<RenderTarget> resolve_depth_target(const SceneRenderViewContext& view_context);
```

Rules:

- if `view_context.depth_target` is non-null, return it directly
- otherwise, find or lazily create a scratch depth target matching `output_target->get_width()`, `output_target->get_height()`, and `RenderTextureFormat::D32_SFLOAT`
- create scratch targets with optimized depth clear `{ 1.0f, 0u }`

- [ ] **Step 3: Build the pass from `SceneRenderViewContext` instead of hidden members**

Use the context when assembling the pass:

```cpp
PassDesc pass_desc{};
pass_desc.name = view_context.debug_name ? view_context.debug_name : "SceneOpaquePass";
pass_desc.color_attachments.push_back({
    view_context.output_target,
    view_context.color_load_action,
    view_context.color_clear_value
});
pass_desc.depth_attachment = {
    resolved_depth_target,
    view_context.depth_load_action,
    view_context.depth_clear_value
};
```

- [ ] **Step 4: Push viewport/scissor down into every submitted draw when requested**

When `has_viewport` / `has_scissor` are set:

```cpp
draw_desc.has_viewport = view_context.has_viewport;
draw_desc.viewport = view_context.viewport;
draw_desc.has_scissor = view_context.has_scissor;
draw_desc.scissor = view_context.scissor;
```

If they are not set, leave the draw descriptors at their default so `RenderDevice` falls back to full-framebuffer state.

- [ ] **Step 5: Add validation guards at the SceneRenderer boundary**

Before beginning the pass, add local checks for:

- `m_renderer != nullptr`
- `view_context.output_target != nullptr`
- `view_context.output_target->get_width() > 0`
- `view_context.output_target->get_height() > 0`
- if `view_context.depth_target` is provided, its width/height must match `output_target`
- if `has_viewport` is true, the viewport width/height must be non-zero and no larger than the attachment extent
- if `has_scissor` is true, the scissor width/height must be non-zero and no larger than the attachment extent

Prefer local `ASH_PROCESS_ERROR` checks plus one clear error log on failure.

- [ ] **Step 6: Rebuild the Engine + Sandbox render path**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `SceneRenderer` now compiles with the new two-argument submit API
- no remaining references to the old single global depth target

## Task 4: Adapt The Existing Single-View Sandbox Path

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`

- [ ] **Step 1: Build the default full-target view context on the render thread**

In `_submit_standard_scene()`, construct the default context like this:

```cpp
AshEngine::SceneRenderViewContext view_context{};
view_context.debug_name = "SandboxStandardSceneView";
view_context.output_target = output_target;
view_context.color_load_action = AshEngine::RenderLoadAction::Clear;
view_context.color_clear_value = { 0.025f, 0.03f, 0.05f, 1.0f };
view_context.depth_load_action = AshEngine::RenderLoadAction::Clear;
view_context.depth_clear_value = { 1.0f, 0u };
```

Leave `depth_target` null so this path exercises the new scratch-depth behavior.

- [ ] **Step 2: Keep Sandbox single-view semantics intentionally simple**

Do not add a second `SceneView`, a split viewport layout, or any multi-window demo code in Sandbox. The purpose here is regression coverage for the existing standard scene path using the new Engine interface.

- [ ] **Step 3: Run a short Sandbox smoke test on the current backend**

Run:

```powershell
Set-Location 'D:\workspace\AshEngine\HASHEAEngine'
& '.\product\bin64\Debug-windows-x86_64\Sandbox.exe' --smoke-test-seconds=5
```

Expected:

- Sandbox starts
- Sponza standard scene still renders through `SceneRenderer`
- the process exits cleanly after the smoke duration

- [ ] **Step 4: Check the summary log path for obvious regressions**

Inspect the latest log under:

```text
D:\workspace\AshEngine\HASHEAEngine\product\logs\
```

Expected:

- no new SceneRenderer failure logs
- no missing-depth or invalid-pass errors
- standard scene summary still reports visible frames built and submitted

## Task 5: Update The Engine Documentation

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`

- [ ] **Step 1: Update the scene-render pipeline description**

Add wording that `VisibleRenderFrame` is now a scene-visible data packet only, and per-view output attachment state is supplied separately at submit time.

- [ ] **Step 2: Document the new per-view submission contract**

Document that `SceneRenderer` now accepts a per-view context carrying:

- output target
- optional explicit depth target
- optional viewport/scissor
- color/depth load/clear behavior

- [ ] **Step 3: Document the first-version multi-view limitation**

Add one explicit note:

- viewport/scissor restrict draw rasterization only
- `RenderLoadAction::Clear` remains an attachment-wide clear, not a rect clear
- multiple views sharing one output target must choose `Clear`/`Load` accordingly

- [ ] **Step 4: Diff review the docs for consistency with the approved spec**

Verify that the wording in `EngineDeveloperGuide.md` matches:

- `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-21-scene-renderer-multi-view-design-zh.md`

No interface name drift. No stale mention that `VisibleRenderFrame` owns the output target.

## Task 6: Run The Full Shared-Render Validation Gate

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini` during validation only

- [ ] **Step 1: Build the full solution in Debug**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine`, `Sandbox`, and `Editor` all build
- post-build copy updates `product/bin64/Debug-windows-x86_64/`

- [ ] **Step 2: Validate Sandbox on Vulkan**

Run:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
  -replace '^Backend=.*', 'Backend=Vulkan' `
  | Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe' --smoke-test-seconds=25
```

Expected:

- Sandbox exits cleanly
- no Vulkan validation errors
- no leak/shutdown regressions in the latest logs

- [ ] **Step 3: Validate Sandbox on DX12**

Run:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
  -replace '^Backend=.*', 'Backend=DX12' `
  | Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe' --smoke-test-seconds=25
```

Expected:

- Sandbox exits cleanly
- no DX12 debug-layer errors or corruption messages

- [ ] **Step 4: Validate Editor on Vulkan and DX12**

Run the same backend swap sequence for `Editor.exe`:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
  -replace '^Backend=.*', 'Backend=Vulkan' `
  | Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Editor.exe' --smoke-test-seconds=25

(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
  -replace '^Backend=.*', 'Backend=DX12' `
  | Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Editor.exe' --smoke-test-seconds=25
```

Expected:

- Editor exits cleanly on both backends
- no viewport scene-render regressions
- no validation/debug-layer failures

- [ ] **Step 5: Restore the default backend after validation**

Restore the default config:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
  -replace '^Backend=.*', 'Backend=DX12' `
  | Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'
```

- [ ] **Step 6: Commit the completed implementation**

Run:

```powershell
git add `
  'project/src/engine/Function/Render/SceneRenderView.h' `
  'project/src/engine/Function/Render/RenderScene.h' `
  'project/src/engine/Function/Render/RenderScene.cpp' `
  'project/src/engine/Function/Render/SceneRenderer.h' `
  'project/src/engine/Function/Render/SceneRenderer.cpp' `
  'project/src/sandbox/App/SandboxStandardScene.cpp' `
  'project/src/sandbox/App/SandboxApplication.cpp' `
  'docs/EngineDeveloperGuide.md'

git commit -m "feat: add scene renderer multi-view submission context"
```

## Suggested AshFlow Ownership Split

- Worker 1 owns `project/src/engine/Function/Render/SceneRenderView.h`, `RenderScene.*`, and `SceneRenderer.*`.
- Worker 2 owns `project/src/sandbox/App/SandboxStandardScene.cpp` and `SandboxApplication.cpp` adaptation only.
- Worker 3 owns `docs/EngineDeveloperGuide.md` plus the final validation run and log triage after the code is integrated.

Do not run multiple workers against `SceneRenderer.cpp` or `RenderScene.cpp` at the same time.

## Self-Review

- Spec coverage:
  - per-view context introduction: covered by Task 1
  - frame/output separation: covered by Task 2
  - explicit vs scratch depth: covered by Task 3
  - single-view Sandbox compatibility: covered by Task 4
  - docs: covered by Task 5
  - Vulkan + DX12 validation: covered by Task 6
- Unresolved-marker scan:
  - no open placeholders remain in the plan body
  - every task has exact files and concrete commands
- Type consistency:
  - `SceneRenderViewContext`
  - `render_visible_frame(const VisibleRenderFrame&, const SceneRenderViewContext&)`
  - `build_visible_render_frame(uint64_t, const SceneView&, VisibleRenderFrame&)`

Plan complete and saved to `docs/superpowers/plans/2026-04-21-scene-renderer-multi-view-implementation.md`.

Two execution options:

1. Subagent-Driven (recommended) - I dispatch focused workers for Engine render contracts, Sandbox adaptation, and final review/validation.
2. Inline Execution - I implement the plan in this session without splitting tasks to subagents.

Which approach?
