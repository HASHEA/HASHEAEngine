# Scene To Render Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first UE-oriented Scene-to-render pipeline in AshEngine for static meshes, with logic-thread scene ownership, worker-thread frustum culling, render-thread-only draw submission, and Vulkan+DX12 validation.

**Architecture:** Keep `Scene` as the logical world, introduce a distinct render-facing `RenderScene` plus proxy/view/visibility/frame-packet layers, and connect them to the existing `Renderer` through a dedicated `SceneRenderer`. The first implementation stays limited to static mesh rendering and leaves lights/material systems/animation as extension points.

**Tech Stack:** C++, `entt`, AshEngine Function-layer render facade, existing render command queue, worker thread pool, Sandbox validation app, Vulkan + DX12 backends.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\SceneComponents.h`
  - extend logical component metadata only where the render bridge needs stable source data
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
  - expose scene-side hooks needed for render extraction without leaking RHI or editor semantics
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`
  - implement any scene-side traversal, world-transform, or extraction support needed by the new render bridge
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
  - add CPU-side metadata needed by static mesh render assets
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
  - compute or expose mesh bounds/section helpers used by render asset creation
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
  - expose APIs needed by the render asset manager for static mesh resolution
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`
  - support the render asset manager’s CPU asset lookup/load path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
  - add the minimum integration points needed by `SceneRenderer`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
  - keep renderer generic while enabling scene-render pass submission
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
  - add scene-render orchestration hooks only if they are required by the high-level engine flow
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`
  - integrate the new scene-render flow into the logic/render-thread lifecycle only where necessary
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
  - wire Sandbox to the new scene-render pipeline
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
  - load a test scene and drive the new logic->render flow
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.cpp`
  - register a new scene-render smoke test
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - update long-term architecture docs after implementation
- `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-16-scene-to-render-flow-design.md`
  - keep the spec aligned if concrete implementation constraints force design adjustments
- `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-16-scene-to-render-flow-design-zh.md`
  - keep the Chinese spec aligned with the English spec

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
  - cache and lifecycle manager for static mesh render asset resolution
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.cpp`
  - CPU-to-render-asset bridge objects and resource state for static meshes
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.cpp`
  - `SceneProxy`, `PrimitiveSceneProxy`, `StaticMeshPrimitiveProxy`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
  - render-facing scene state and proxy ownership
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`
  - `SceneViewDesc`, `SceneView`, `SceneViewFamily`, frustum construction
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Visibility.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Visibility.cpp`
  - visibility query, worker-thread chunking, visible primitive output
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
  - high-level scene renderer that consumes visible frame packets and submits draws

## Task 1: Add Render Asset Foundations For Static Meshes

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`

- [ ] Define `StaticMeshRenderSection`, `StaticMeshRenderAsset`, and `StaticMeshRenderResource` types with clear separation between CPU metadata and GPU resource ownership.
- [ ] Add CPU-side helpers in `AssetData` to expose stable mesh bounds/section information needed by render asset creation without duplicating importer logic.
- [ ] Define `RenderAssetManager` APIs for:
  - requesting a static mesh render asset by logical asset path + mesh index
  - querying asset/resource readiness
  - finalizing GPU resources on the render thread
- [ ] Implement CPU-side asset resolution using `AssetDatabase` and worker-thread dispatch without issuing any render-thread or RHI calls from workers.
- [ ] Implement render-thread resource finalization so vertex/index buffers are created through the existing high-level render resource path, not by leaking backend details upward.
- [ ] Add logging and load-state transitions that make asset readiness debuggable without spamming logs every frame.

## Task 2: Add RenderScene And Proxy Types

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\SceneComponents.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`

- [ ] Define base proxy abstractions:
  - `SceneProxy`
  - `PrimitiveSceneProxy`
  - `StaticMeshPrimitiveProxy`
- [ ] Define `RenderScene` ownership of primitive proxies, stable primitive IDs, and dirty/add/remove tracking.
- [ ] Add scene-side extraction helpers that can iterate mesh-bearing entities and derive the source data required to create or update static mesh primitive proxies.
- [ ] Keep `MeshComponent` logical; do not place GPU handles or backend-specific state in scene components.
- [ ] Add only the minimum scene metadata needed for future extensibility, such as reserved mobility or layer/filter fields if they materially help the proxy contract.
- [ ] Ensure proxy creation can tolerate assets that are still loading by representing unresolved render resources explicitly instead of failing hard.

## Task 3: Add World-Transform And Primitive Bounds Extraction

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`

- [ ] Add scene traversal utilities to derive stable world transforms from parent-child hierarchy for render extraction.
- [ ] Add primitive-local and world-space bounds storage on static mesh proxies.
- [ ] Define dirty propagation rules so transform changes mark only the needed primitive/render-scene state dirty.
- [ ] Ensure the resulting extracted primitive state is independent from direct `entt` reads after extraction finishes.
- [ ] Keep all world-transform and bounds computation on the logic side or worker side, not on the render thread.

## Task 4: Add SceneView And Camera Extraction

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`

- [ ] Define `SceneViewDesc`, `SceneView`, and `SceneViewFamily` for the phase-1 single-camera case.
- [ ] Add logic-side camera extraction rules that choose the primary camera from scene components and derive view/projection matrices plus frustum planes.
- [ ] Define a stable fallback behavior when no valid camera exists, so the scene-render path fails predictably instead of accessing invalid data.
- [ ] Keep the API open for future multiple-view expansion without overbuilding multi-view features now.

## Task 5: Add Worker-Thread Visibility Culling

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Visibility.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Visibility.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Base\hthreading.h`

- [ ] Define `VisibilityQuery`, `VisiblePrimitiveSet`, and `VisibilityResult`.
- [ ] Implement CPU frustum culling over static mesh primitive bounds.
- [ ] Use the existing worker-thread pool to process primitive ranges in parallel without touching renderer state.
- [ ] Aggregate worker results deterministically into a stable visible primitive list.
- [ ] Make the visibility stage callable from the logic thread as a self-contained phase.
- [ ] Leave clear extension points for future occlusion, pass masks, or visibility flags without implementing them now.

## Task 6: Add VisibleRenderFrame And Cross-Thread Handoff

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Base\hthreading.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Base\hthreading.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`

- [ ] Define an immutable `VisibleRenderFrame` packet that contains all render-thread-consumable scene draw inputs for one frame.
- [ ] Ensure the packet stores resolved transforms, primitive visibility results, and resolved render-asset references without requiring render-thread access back into `Scene`.
- [ ] Implement a phase-1 handoff path using the existing render command queue and immutable shared frame packets.
- [ ] Add the minimum application-level orchestration needed so logic thread scene extraction can publish completed render frames and render thread can consume them safely.
- [ ] Keep the handoff model explicit and debuggable; do not introduce speculative multi-buffer scheduling beyond what phase 1 needs.

## Task 7: Add SceneRenderer And Render Submission Integration

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`

- [ ] Define `SceneRenderer` as the high-level scene submission system, not as a world manager replacement for `Renderer`.
- [ ] Add a phase-1 static mesh graphics path that turns `VisibleRenderFrame` entries into `GraphicsDrawDesc` instances and submits them through `Renderer::GraphicsPassContext`.
- [ ] Reuse the existing render facade and resource binding paths rather than bypassing them.
- [ ] Keep material handling minimal for phase 1:
  - use a stable placeholder material/program contract
  - keep interfaces open for future material render proxy integration
- [ ] Ensure scene submission uses only render-thread-owned objects and no direct logical scene references.
- [ ] Keep Vulkan and DX12 behavior unified at the high-level path.

## Task 8: Integrate A Sandbox Scene-Render Smoke Path

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.cpp`

- [ ] Add a Sandbox smoke path that loads one or more static mesh scenes using the new logic-thread scene pipeline.
- [ ] Make the smoke path exercise:
  - scene instantiation
  - render asset resolution
  - visibility build
  - visible frame handoff
  - render-thread draw submission
- [ ] Keep the test runtime short and deterministic enough to fit later automated validation.
- [ ] Prefer a classic sample scene or existing glTF assets already present in `product/assets/models/gltfs/`.

## Task 9: Update Docs And Run Full Validation

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-16-scene-to-render-flow-design.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-16-scene-to-render-flow-design-zh.md`

- [ ] Update `EngineDeveloperGuide.md` so the implemented architecture is reflected as current reality rather than future direction.
- [ ] Reconcile any implementation-driven changes back into both spec documents.
- [ ] Regenerate Premake files if project structure changed.
- [ ] Build the intended configuration.
- [ ] Run Sandbox and/or Editor scene-render smoke coverage as needed.
- [ ] Run the AshEngine Vulkan + DX12 validation loop and treat build failures, validation output, debug-layer output, or leak signals as blocking issues.

## Subagent Execution Split

Recommended AshFlow split for implementation:

- Master agent local critical path:
  - architecture decisions
  - `VisibleRenderFrame` contract
  - `SceneRenderer` integration
  - final merge/refinement
  - final validation

- Worker slice 1:
  - `AssetData` / `AssetDatabase` / `RenderAssetManager` / `StaticMeshRenderAsset`

- Worker slice 2:
  - `Scene` / `SceneProxy` / `RenderScene` / transform and bounds extraction

- Worker slice 3:
  - `SceneView` / `Visibility`

- Worker slice 4:
  - Sandbox smoke integration and test coverage

Do not run multiple workers against the same files in parallel.

## Validation Notes

- Shared-risk rendering changes must be validated on both Vulkan and DX12.
- Prefer Vulkan legality as the stricter shared-path constraint.
- Do not allow render-thread submission to depend on live `Scene` reads.
- Watch shutdown logs for leak regressions, especially after introducing render asset ownership.

## Self-Review

Spec coverage check:

- render asset bridge: covered by Task 1
- render proxy and render scene: covered by Task 2
- transform/bounds extraction: covered by Task 3
- scene view and camera extraction: covered by Task 4
- multi-threaded visibility: covered by Task 5
- immutable render-frame handoff: covered by Task 6
- dedicated scene renderer integration: covered by Task 7
- sandbox validation: covered by Task 8
- docs + dual-backend validation: covered by Task 9

Placeholder scan:

- no `TODO`
- no `TBD`
- no task depends on “similar to previous task” wording

Type consistency:

- the plan consistently uses:
  - `RenderAssetManager`
  - `StaticMeshRenderAsset`
  - `StaticMeshRenderResource`
  - `SceneProxy`
  - `PrimitiveSceneProxy`
  - `StaticMeshPrimitiveProxy`
  - `RenderScene`
  - `SceneView`
  - `VisibleRenderFrame`
  - `SceneRenderer`

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-16-scene-to-render-flow-implementation.md`.

Two execution options:

1. Subagent-Driven (recommended) - I dispatch a fresh subagent per task slice, review between slices, and keep the master agent on the architecture/integration critical path.

2. Inline Execution - Execute tasks in this session in sequence, with checkpoints but without per-slice worker delegation.
