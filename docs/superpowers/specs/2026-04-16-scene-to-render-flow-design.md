# AshEngine Scene To Render Flow Design

Date: 2026-04-16

## Scope

This document defines the first real Scene-to-render pipeline for AshEngine.

The target is a UE-oriented architecture, but with a deliberately limited first-stage feature scope:

- static mesh scene rendering only
- single main camera view
- logic-thread scene ownership
- worker-thread assisted extraction and frustum culling
- render-thread-only draw submission

The design must preserve the Engine / Editor boundary and must fit the current AshEngine threading and Renderer infrastructure.

## Current State

The current engine already has several prerequisites:

- `Scene` is a CPU-side `entt`-backed logical world facade
- `AssetDatabase` can load `Model` / `Mesh` / `AshAsset` data on worker threads
- the engine has a first-stage thread model with `Render`, `Logic`, and `Worker` roles
- cross-thread render work already has a queue via `enqueue_render_command()`
- `Renderer` and `RenderDevice` already provide pass-based draw submission

What does not exist yet is the bridge between those pieces:

- no render-side scene representation
- no render proxies
- no view extraction layer
- no visibility system
- no frame-stable render packet built on the logic side and consumed on the render side
- no GPU resource bridge from CPU scene assets into reusable render resources

## Goals

The first implementation must:

1. Let a logical `Scene` become renderable without exposing RHI details outside Engine internals.
2. Keep `Scene` as the logic-thread-owned source of truth.
3. Prevent the render thread from directly reading `Scene` / `entt` state.
4. Introduce a UE-like bridge:
   - `SceneProxy`
   - `PrimitiveSceneProxy`
   - `RenderScene`
   - `SceneView`
   - `VisibleRenderFrame`
   - `SceneRenderer`
5. Support multi-threaded CPU frustum culling.
6. Keep Vulkan and DX12 on the same high-level path.
7. Leave extension points for:
   - materials
   - lights
   - instancing
   - animation
   - shadow views
   - GPU-driven rendering

## Non-Goals For Phase 1

The following are explicitly out of scope for the first pass:

- skeletal meshes
- animation evaluation
- full lighting model
- shadow rendering
- dynamic material instances
- instancing / HISM / Nanite-style clustering
- occlusion culling
- pass processor / mesh draw command architecture at full UE depth

Those systems should get extension points, not full implementation.

## Recommended Architecture

The chosen architecture is:

- keep `Scene` as the logical world
- add a distinct render-facing `RenderScene`
- generate `PrimitiveSceneProxy` objects from logical scene entities
- build `SceneView` data from the active camera
- run visibility against render primitives on the logic side with worker assistance
- build an immutable `VisibleRenderFrame`
- hand that frame to the render thread
- have a dedicated `SceneRenderer` translate it into existing `Renderer` / `RenderDevice` draw submission

This is the recommended middle path:

- much cleaner than directly stuffing render caches into `Scene`
- much smaller than immediately building a full UE-style mesh draw command stack

## High-Level Data Flow

### 1. Logical World

`Scene` remains the authoritative gameplay/editor-facing world state.

Entities still hold logical components such as:

- `NameComponent`
- `TransformComponent`
- `CameraComponent`
- `LightComponent`
- `MeshComponent`

Phase 1 extends the scene-side data model only where needed for static mesh rendering.

### 2. Render Asset Resolution

`MeshComponent` currently stores only:

- `asset_path`
- `mesh_index`
- `visible`

That is not sufficient for rendering. A new render-resource bridge resolves those logical references into reusable GPU-ready render assets.

Recommended new types:

- `StaticMeshRenderAsset`
- `StaticMeshRenderResource`
- `RenderAssetManager`

Responsibilities:

- request CPU asset data from `AssetDatabase`
- prepare section/bounds/material-slot metadata
- create and cache GPU vertex/index buffers on the render thread
- expose stable render-resource handles to scene proxies

### 3. Scene Proxy Layer

Add a render extraction layer between logical scene entities and render-thread submission.

Recommended types:

- `SceneProxy`
- `PrimitiveSceneProxy`
- `StaticMeshPrimitiveProxy`

Responsibilities:

- represent only render-relevant state
- detach render-facing data from direct `entt` access
- cache primitive bounds and transform-derived render state
- resolve mesh asset references into render asset references

Phase 1 only needs static mesh primitives, but the base proxy abstraction should leave room for:

- light proxies
- decal proxies
- skeletal mesh proxies
- particle proxies

### 4. RenderScene

`RenderScene` is the render-facing world representation associated with one logical scene.

Responsibilities:

- own primitive proxies
- maintain stable primitive identifiers
- track dirty/add/remove state from logic-side scene changes
- provide compact arrays for extraction and culling

Important boundary:

- `RenderScene` is not the RHI scene
- it is still an Engine Function-layer render-world abstraction
- it must not expose Vulkan/DX12 types

### 5. SceneView

Add a view abstraction so the visibility and scene-render path stops relying on ad hoc camera usage.

Recommended types:

- `SceneViewDesc`
- `SceneView`
- `SceneViewFamily`

Phase 1 requirements:

- single main camera
- view matrix
- projection matrix
- view-projection matrix
- camera position
- frustum planes
- viewport size

Future extension points:

- multiple editor/game viewports
- shadow views
- reflection views
- stereo views

### 6. Visibility System

Add a visibility stage that operates on render primitives, not on raw scene entities.

Recommended types:

- `VisibilityQuery`
- `VisiblePrimitiveSet`
- `VisibilityResult`

Phase 1 behavior:

- CPU frustum culling only
- worker-thread parallel chunking across primitive ranges
- stable per-view visible primitive list output

This stage should consume:

- `RenderScene`
- `SceneView`

And produce:

- a compact visible primitive list for that frame

### 7. VisibleRenderFrame

This is the most important cross-thread handoff object.

Recommended type:

- `VisibleRenderFrame`

Design rule:

- immutable after construction
- safe to hand from logic thread to render thread
- contains only render-thread-consumable data
- does not depend on `Scene` or `entt` remaining readable

Phase 1 contents should include:

- frame index or generation
- target scene/view identifiers
- visible primitive entries
- resolved world transforms
- resolved mesh sections
- resolved render resources
- future material binding placeholders
- pass classification metadata for opaque static meshes

The render thread should consume this packet directly, without peeking back into the logical scene.

### 8. SceneRenderer

Do not turn `Renderer` into a world manager.

Recommended entry point:

- add a dedicated `SceneRenderer`

Responsibilities:

- accept `VisibleRenderFrame`
- build render passes for scene rendering
- translate visible primitives into existing `GraphicsDrawDesc`
- choose the phase-1 graphics program(s)
- issue draw submission through the existing `Renderer` facade

Why this is preferred over adding everything directly to `Renderer`:

- keeps `Renderer` as a general render facade
- keeps scene rendering as a higher-level system
- leaves room for non-scene rendering users of `Renderer`
- matches the requested UE-like direction more closely

## Thread Ownership Model

### Logic Thread

Owns:

- `Scene`
- scene mutation
- scene instantiation
- proxy dirty tracking
- view construction
- visibility orchestration
- `VisibleRenderFrame` construction

Must not:

- directly issue RHI calls
- directly mutate render-thread-owned GPU resources

### Worker Threads

Own:

- async CPU asset loading
- section/bounds preparation
- visibility task chunks
- CPU-side extraction helpers that do not mutate render state directly

Must not:

- directly touch `Renderer`
- directly submit draw work

### Render Thread

Owns:

- `Renderer`
- `RenderDevice`
- `GraphicsProgram` / `ComputeProgram` binding and draw submission
- GPU resource creation/finalization for render assets
- consumption of completed `VisibleRenderFrame`

Must not:

- directly read `Scene` / `entt`
- depend on live logical entity state while submitting a frame

## Synchronization Model

Phase 1 should use a simple, explicit model.

Recommended approach:

- logic thread builds a complete `VisibleRenderFrame`
- that frame is transferred to the render thread through the existing render command queue
- render thread consumes the latest completed frame packet

Two acceptable concrete implementations exist:

1. render-command pushed shared frame packet
2. double-buffered latest-frame exchange with generation numbers

Recommendation for Phase 1:

- use a render-command handoff with immutable shared frame packets first

Reason:

- integrates naturally with the current `enqueue_render_command()` system
- easy to debug
- easy to fence
- simpler than prematurely introducing a more elaborate multi-buffer frame scheduler

If later needed, the frame packet transport can evolve without redesigning the proxy/view/culling architecture.

## Scene-Side Data Extensions Needed

Phase 1 should extend scene data only where architecturally necessary.

Recommended additions:

- explicit static-mesh-oriented render metadata component or proxy source data
- optional mobility/static flags placeholder
- optional layer/filter placeholder

Do not store raw GPU resource handles inside `MeshComponent`.

Keep `MeshComponent` as a logical reference component.

Any GPU-facing resolution belongs in the render asset / proxy layer.

## Asset And Render Resource Model

Phase 1 static mesh rendering needs a reusable render asset system.

Recommended asset split:

- CPU import data remains in `AssetData`
- render-ready mesh resources live in a new render asset layer

Suggested objects:

- `StaticMeshRenderAsset`
  - engine-facing static mesh asset record
  - owns CPU mesh section metadata and logical asset identity

- `StaticMeshRenderResource`
  - resolved GPU resource bundle
  - owns vertex buffer, index buffer, section draw ranges, bounds

- `RenderAssetManager`
  - cache and lifecycle manager
  - accepts logical mesh references
  - tracks load state
  - enqueues render-thread resource finalization

This layer is the bridge between:

- `AssetDatabase`
- `SceneProxy`
- `SceneRenderer`

## Submission Path

Phase 1 should continue reusing the current pass and draw path instead of replacing it.

Recommended flow:

1. `SceneRenderer` creates or reuses the phase-1 static-mesh graphics program.
2. `SceneRenderer` builds one opaque scene pass targeting the engine back buffer.
3. For each visible primitive section:
   - resolve vertex/index buffers
   - set transform/object constants
   - set placeholder material state
   - append a `GraphicsDrawDesc`
4. Submit through `Renderer::GraphicsPassContext`

This keeps the existing renderer path valid while introducing the missing scene extraction and submission layers above it.

## Extension Points Reserved For Later

The first implementation should explicitly preserve future hooks for:

- `LightSceneProxy`
- `MaterialRenderProxy`
- `MeshPassType`
- `RenderLayerMask`
- `PrimitiveVisibilityFlags`
- instancing groups
- shadow view families
- pass processors / draw command compaction

These can exist as empty or minimal placeholders if that helps keep type boundaries stable.

## File And Module Impact

The following existing files are likely to need changes:

- `project/src/engine/Function/Scene/SceneComponents.h`
- `project/src/engine/Function/Scene/Scene.h`
- `project/src/engine/Function/Scene/Scene.cpp`
- `project/src/engine/Function/Asset/AssetData.h`
- `project/src/engine/Function/Asset/AssetData.cpp`
- `project/src/engine/Function/Asset/AssetDatabase.h`
- `project/src/engine/Function/Asset/AssetDatabase.cpp`
- `project/src/engine/Function/Render/Renderer.h`
- `project/src/engine/Function/Render/Renderer.cpp`

Recommended new Engine Function-layer modules:

- `project/src/engine/Function/Render/SceneRenderer.*`
- `project/src/engine/Function/Render/RenderScene.*`
- `project/src/engine/Function/Render/SceneProxy.*`
- `project/src/engine/Function/Render/SceneView.*`
- `project/src/engine/Function/Render/Visibility.*`
- `project/src/engine/Function/Render/RenderAssetManager.*`
- `project/src/engine/Function/Render/StaticMeshRenderAsset.*`

The exact file split can still be adjusted during planning, but these responsibilities should remain distinct.

## Recommended Implementation Order

1. Introduce the render asset bridge for static meshes.
2. Introduce `RenderScene` and static mesh primitive proxies.
3. Add transform and bounds extraction for render primitives.
4. Add `SceneView` and single-camera extraction.
5. Add multi-threaded frustum culling.
6. Add immutable `VisibleRenderFrame` handoff.
7. Add `SceneRenderer` and connect it to the existing `Renderer`.
8. Add Sandbox coverage for scene rendering.
9. Run Vulkan + DX12 validation.

## Validation Strategy

Phase 1 validation should include:

- CPU asset load and scene instantiation still work
- static mesh scene can render from the logic-thread-driven path
- visibility removes out-of-frustum primitives correctly
- render thread no longer depends on direct `Scene` reads
- Vulkan and DX12 both render the same scene path
- shutdown is clean with no leak / validation regressions

Recommended runtime validation path:

- add a Sandbox scene-render smoke test
- then run the normal AshEngine validation loop on both backends

## Chosen Direction

The approved direction is:

- UE-like architecture
- dedicated `SceneRenderer`
- dedicated `RenderScene`
- dedicated proxy/view/visibility/frame-packet pipeline
- phase-1 support limited to static mesh rendering
- complex systems left as extension points, not implemented immediately

This is the baseline for the implementation plan.
