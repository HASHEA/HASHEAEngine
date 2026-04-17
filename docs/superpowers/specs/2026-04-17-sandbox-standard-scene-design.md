# Sandbox Standard Scene Design

**Status:** Proposed  
**Date:** 2026-04-17  
**Scope:** `project/src/sandbox`, selected Engine-facing scene/render integration points

## 1. Goal

Replace the current mixed Sandbox demo/smoke set with one standard end-to-end scene test that exercises the real Engine path:

- load a production-style asset through `AssetDatabase`
- instantiate it into a logical `Scene`
- update the scene on the logic thread
- bridge logic-side scene state into render-side visible data
- render through `SceneRenderer`
- show the final image on screen through the normal present path

The default standard scene will be **Sponza**. The goal is not fast startup; the goal is meaningful scene complexity and a stable regression baseline for engine/rendering work.

## 2. Why Change

The current Sandbox default registry mixes three different intentions:

- asset pipeline smoke
- scene/render bridge smoke
- Codex logo demo rendering

That was useful while the scene-to-render bridge was incomplete, but it is no longer the right baseline.

Problems with the current setup:

- it is not obvious which test represents the intended long-term Engine flow
- `SceneRenderFlowSmokeTest` still has transitional “hook smoke” semantics
- `CodexLogoRenderTest` validates a demo path, not the engine’s standard scene path
- `AssetPipelineSmokeTest` is valuable as a focused pipeline test, but not as the main Sandbox runtime experience

The Sandbox runtime should now represent the canonical integrated Engine rendering path, not a bag of unrelated demos.

## 3. Recommended Direction

Adopt a **single standard Sandbox scene mode** as the default runtime path.

The recommended baseline is:

- default asset: `product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`
- default behavior: interactive free camera
- thread model: logic thread owns scene and camera updates; render thread owns frame submission
- rendering path: `Scene -> RenderScene -> VisibleRenderFrame -> SceneRenderer -> Renderer -> present`

Old Sandbox demo tests should no longer be part of the default registry.

## 4. Functional Requirements

### 4.1 Standard Scene Loading

At Sandbox startup:

- initialize `AssetDatabase`
- resolve the Sponza asset path
- asynchronously load the model
- instantiate the loaded model into a new logical scene
- create a primary camera entity inside that scene
- persist enough runtime state so the logic thread can keep updating the scene every frame

The scene must be considered failed if:

- the asset cannot be found
- async model load throws
- the resulting model is invalid
- scene instantiation fails
- the camera entity or camera component cannot be created

### 4.2 Interactive Free Camera

The standard scene will expose a Sandbox-only free camera controller that operates on the logical scene camera entity.

Controls:

- `W` / `S`: forward / backward
- `A` / `D`: strafe left / right
- `Q` / `E`: move down / up
- hold right mouse button: enable mouse-look
- mouse delta while mouse-look is active: yaw / pitch
- mouse wheel: adjust move speed
- `Shift`: movement acceleration

The controller should run on the logic thread and consume the already-snapshotted per-thread `InputState`.

### 4.3 CBuffer / View Update Validation

Camera movement must naturally drive:

- logical camera transform changes
- rebuilt `SceneView`
- updated view/projection matrices
- updated object-to-clip shader constants

No special “cbuffer test” hook should be added. The standard scene itself should be the cbuffer update test.

### 4.4 Render Submission

Every frame:

- logic-side scene state is converted into render-side visible data
- the render thread consumes the current visible frame
- the rendered result is shown on screen through the engine’s normal present path

The test is successful only if a non-empty visible frame is produced and submitted over time.

### 4.5 Smoke-Test Compatibility

The new standard scene must remain compatible with the repository’s validation loop.

That means:

- it must run unattended for the configured smoke duration
- it must not require editor interaction to stay alive
- it must exit cleanly when the existing smoke/auto-exit path requests shutdown

Interactive free camera is for normal manual runs. Smoke validation still depends on stable unattended runtime.

## 5. Architecture

## 5.1 High-Level Structure

The Sandbox runtime should shift from “registry of unrelated tests” to “standard scene runtime plus optional focused tests later.”

Recommended structure:

- `SandboxApplication` remains the process entry and lifecycle owner
- a new Sandbox standard-scene runtime object owns:
  - scene loading state
  - current scene
  - free camera runtime state
  - visible-frame handoff state
- logic thread updates:
  - asset/scene readiness
  - camera controller
  - scene-to-visible-frame build
- render thread updates:
  - visible-frame consumption
  - scene submission via `SceneRenderer`

This keeps Sandbox aligned with the Engine’s thread ownership model instead of encoding more testing-only hooks.

## 5.2 Replace Transitional Hook Semantics

The existing `SandboxSceneRenderFlowHooks` and `SandboxSceneRenderFlowState` were useful when the bridge was still being proven out. Now they should be collapsed toward direct standard-scene runtime ownership.

Preferred direction:

- keep a single shared state object between logic and render threads
- represent it as concrete standard-scene runtime state, not generic “hook smoke” plumbing
- remove status names such as:
  - `AwaitingLogicIntegration`
  - `AwaitingRenderIntegration`
- replace them with runtime-meaningful states such as:
  - loading
  - ready
  - frame-ready
  - rendering
  - failed

This makes logs and failures easier to interpret.

## 5.3 Camera Ownership

The camera should remain a normal `Scene` entity with standard components:

- `TransformComponent`
- `CameraComponent`

The free camera controller should only mutate those components. It should not bypass `Scene` or inject backend-facing state.

That preserves the Engine boundary and ensures the same scene/view code path is exercised as future game/editor usage.

## 5.4 Visible Frame Handoff

The handoff should remain double-buffer or latest-frame style shared state between logic and render threads.

Requirements:

- the logic thread must be able to rebuild a `VisibleRenderFrame`
- the render thread must consume a coherent snapshot
- ownership must stay clear and thread-safe

The design should prefer clear synchronization over clever lock avoidance. The main purpose here is correctness and debuggability.

## 6. Runtime Behavior

## 6.1 Manual Interactive Run

When launched normally:

- Sandbox loads Sponza
- the scene becomes visible on screen
- the camera can be moved with `WASD + QE`
- right mouse button enables mouse-look
- wheel changes camera speed
- the application keeps running until the user closes it or the engine requests exit

## 6.2 Validation Run

When launched under the existing smoke-test environment:

- Sandbox still loads the same Sponza scene
- no user interaction is required
- if the camera is untouched, the scene still renders stably
- shutdown follows the existing timed smoke exit path

This keeps the standard scene compatible with the four-pass validation loop.

## 7. Scope Boundaries

## In Scope

- replacing the default Sandbox runtime path
- introducing a Sandbox free camera controller
- using Sponza as the default standard validation scene
- updating Sandbox lifecycle/state management to support the new flow
- updating docs that describe Sandbox’s validation role

## Out of Scope

- changing Editor code
- creating a generalized scene browser or level-selection UI
- building a full gameplay camera system
- adding lighting, post-process, or material-system overhauls just for Sandbox
- making Sandbox a general-purpose game sample framework

## 8. Testing Strategy

### 8.1 Functional Checks

- Sandbox launches and loads Sponza successfully
- the scene appears on screen
- camera movement changes the view correctly
- right-mouse mouse-look updates yaw/pitch correctly
- render output continues while moving

### 8.2 Regression Checks

Because this affects shared scene/render behavior, final verification should include:

- rebuild through the normal Premake + MSBuild flow
- `Sandbox` on Vulkan
- `Sandbox` on DX12
- `Editor` on Vulkan
- `Editor` on DX12

The shared-risk acceptance standard remains:

- no validation errors
- no debug-layer errors
- no leak reports
- clean timed shutdown

## 9. Risks

### 9.1 Sponza Startup Cost

Sponza is heavier than the current sample models, so startup and first-frame preparation will be slower. This is intentional, but it means:

- error handling around async load must stay explicit
- logs must clearly distinguish loading time from failure

### 9.2 Input Thread Ownership

The camera controller will run on the logic thread, so it must use the per-thread input snapshot already maintained by `Application`. It must not reach back into render-thread event state directly.

### 9.3 Render/Logic Handoff Complexity

If the visible-frame handoff remains too transitional, the standard scene will inherit ambiguity from the current bridge-smoke implementation. That should be addressed during implementation by simplifying the runtime state model instead of preserving the old smoke-test abstraction wholesale.

## 10. Recommendation

Proceed with:

- one new default Sandbox standard-scene runtime
- Sponza as the canonical scene
- interactive free camera on the logic thread
- removal of Codex-logo and old asset-pipeline demos from the default Sandbox registry

This gives the engine a meaningful, repeatable, visually rich standard validation path and aligns Sandbox with the real Scene-to-Render architecture now that the bridge exists.
