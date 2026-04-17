# Sandbox Standard Scene Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current mixed Sandbox demo/test registry with one standard Sponza scene runtime that loads through the logical scene path, renders through the existing scene-to-render bridge, and supports interactive free-camera control on the logic thread.

**Architecture:** Keep Sandbox aligned with the Engine threading model by introducing a focused standard-scene runtime state owned by `SandboxApplication`: logic thread loads and updates the scene plus camera, render thread consumes the latest visible frame and submits it through `SceneRenderer`. The first version intentionally keeps Sandbox-specific logic in Sandbox code and reuses existing Engine scene/view/render abstractions rather than creating a broader sample framework.

**Tech Stack:** C++, AshEngine `Application` threading/input model, `AssetDatabase`, `Scene`, `RenderScene`, `SceneView`, `SceneRenderer`, Sandbox app/test layer, Vulkan + DX12 validation loop.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
  - replace the current mixed test-oriented state with standard scene runtime ownership
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
  - load Sponza, drive the logic/render flow, and integrate free-camera updates
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.h`
  - simplify or retire the old generic smoke registry types if they no longer match the new default runtime
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.cpp`
  - remove old default tests and define the new standard scene test/runtime entry
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Base\input\Input.h`
  - add only the minimal input helpers required for stable free-camera delta handling, if the current API is insufficient
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
  - expose only the minimum engine-facing accessors needed by Sandbox for per-thread input and runtime control if current APIs are insufficient
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`
  - implement any minimal accessor/runtime support required by the Sandbox standard scene path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
  - refine visible-frame/state contracts only if the Sandbox standard scene needs them
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
  - adjust visible-frame build helpers only if required by the new Sandbox flow
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
  - add only the minimum view/camera helpers needed by the free-camera path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`
  - implement any needed camera/view helper updates
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document Sandbox’s new role as the standard scene validation runtime
- `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-17-sandbox-standard-scene-design.md`
  - reconcile spec wording if implementation details require minor design adjustments
- `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-17-sandbox-standard-scene-design-zh.md`
  - keep the Chinese spec aligned with the English spec

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
  - own Sponza scene loading, runtime state, visible-frame handoff, and failure/reporting state
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxFreeCameraController.h`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxFreeCameraController.cpp`
  - implement logic-thread-only free-camera input processing over a normal scene camera entity

## Task 1: Introduce A Focused Sandbox Standard Scene Runtime

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`

- [ ] Define a `SandboxStandardScene` runtime object that owns:
  - load state
  - failure state and detail string
  - logical `Scene`
  - `RenderScene`
  - latest `SceneView`
  - latest `VisibleRenderFrame`
  - the canonical sample asset path
- [ ] Set the canonical sample asset path to `product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`.
- [ ] Move Sandbox scene-render ownership out of the current generic “flow hook” framing and into this concrete runtime object.
- [ ] Keep synchronization explicit with a small, clear shared-state model between logic and render threads.
- [ ] Ensure runtime logs clearly distinguish:
  - asset discovery failure
  - async load failure
  - scene instantiation failure
  - camera setup failure
  - visible-frame build failure
  - render submission failure

## Task 2: Remove Old Default Sandbox Demo/Test Paths

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Tests\SandboxTestRegistry.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`

- [ ] Remove `AssetPipelineSmokeTest` from the default Sandbox runtime path.
- [ ] Remove `CodexLogoRenderTest` from the default Sandbox runtime path.
- [ ] Retire or substantially simplify the current `SceneRenderFlowSmokeTest` so the default Sandbox path no longer presents itself as transitional “bridge smoke.”
- [ ] Keep any reusable helper code only if it directly serves the new standard scene runtime.
- [ ] Make the default Sandbox startup path obviously map to the new standard scene runtime, not a registry of unrelated demos.

## Task 3: Add A Logic-Thread Free Camera Controller

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxFreeCameraController.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxFreeCameraController.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Base\input\Input.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Application.cpp`

- [ ] Define a Sandbox-only free-camera controller that operates on a normal `Scene` camera entity.
- [ ] Support these controls:
  - `W` / `S`: forward / backward
  - `A` / `D`: strafe left / right
  - `Q` / `E`: move down / up
  - hold right mouse button: enable mouse-look
  - mouse delta while active: yaw / pitch
  - mouse wheel: adjust move speed
  - `Shift`: accelerate movement
- [ ] Keep all input consumption on the logic thread using `Application::get_input()` and the existing input snapshot model.
- [ ] Add only the minimum input helpers needed for stable mouse-delta handling, such as previous-mouse tracking or convenience delta accessors, if the current `InputState` is insufficient.
- [ ] Keep the controller independent from renderer or RHI state.

## Task 4: Load Sponza Into A Logical Scene And Create A Primary Camera

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`

- [ ] On Sandbox logic startup, initialize the asset database and resolve the Sponza asset path.
- [ ] Load Sponza asynchronously through the existing `AssetDatabase` path.
- [ ] Instantiate the loaded model into a fresh logical `Scene`.
- [ ] Create a dedicated primary camera entity in that scene.
- [ ] Set a usable default camera transform for initial manual inspection of Sponza.
- [ ] Treat all startup failures as fatal for the Sandbox run and request exit cleanly with a meaningful error log.

## Task 5: Rebuild SceneView And Visible Frames From The Logic Thread

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneView.cpp`

- [ ] Each logic-thread tick, update the free camera and write the resulting transform back to the scene camera entity.
- [ ] Rebuild the primary `SceneView` from the current scene/camera state.
- [ ] Rebuild `RenderScene` or refresh its scene-derived data using the existing scene-to-render bridge.
- [ ] Build a non-empty `VisibleRenderFrame` from the current view.
- [ ] Publish the newest valid visible frame to the render thread through the standard scene runtime state.
- [ ] Ensure this path naturally exercises view/projection and object transform constant updates rather than adding a special cbuffer test hook.

## Task 6: Submit The Standard Scene On The Render Thread

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] In the Sandbox render path, consume the latest valid visible frame snapshot.
- [ ] Submit it through `SceneRenderer::render_visible_frame`.
- [ ] Keep the output target bound to the standard renderer-present path rather than reintroducing demo-specific rendering.
- [ ] Track whether non-empty visible frames are actually reaching render submission.
- [ ] Make render-side failure states explicit and request clean application exit on fatal errors.

## Task 7: Preserve Smoke-Test Behavior While Enabling Manual Interaction

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxApplication.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] Keep normal interactive runs alive until the user closes the window or the engine requests exit.
- [ ] Keep smoke-test runs compatible with the existing timed auto-exit path.
- [ ] Do not make the new standard scene depend on user input to keep rendering.
- [ ] Log one concise runtime summary on shutdown showing:
  - whether Sponza loaded
  - whether visible frames were built
  - whether frames were submitted
  - whether the run exited cleanly

## Task 8: Update Docs To Reflect Sandbox’s New Role

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-17-sandbox-standard-scene-design.md`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\superpowers\specs\2026-04-17-sandbox-standard-scene-design-zh.md`

- [ ] Update `EngineDeveloperGuide.md` to describe Sandbox as the standard scene/render integration validation runtime.
- [ ] Document that the default Sandbox scene is now Sponza.
- [ ] Document the free-camera controls at a high level.
- [ ] Reconcile any implementation-driven deviations back into both design specs.

## Task 9: Validate Sandbox + Editor On Vulkan And DX12

**Files:**
- Modify: any files from earlier tasks as required by fixes found during validation

- [ ] Regenerate Premake files if project structure changed.
- [ ] Build `AshEngine.sln` in the intended configuration.
- [ ] Run the full AshEngine validation loop:
  - `Sandbox` Vulkan
  - `Sandbox` DX12
  - `Editor` Vulkan
  - `Editor` DX12
- [ ] Treat these as blocking failures:
  - build breaks
  - startup failure
  - abnormal exit
  - validation output
  - debug-layer output
  - resource leak output
- [ ] Fix issues and rerun until the four-pass loop is clean.

## Subagent Execution Split

Recommended AshFlow split for implementation:

- Master agent local critical path:
  - standard-scene runtime architecture
  - logic/render handoff contract
  - final Sandbox integration
  - final validation

- Worker slice 1:
  - free-camera controller and any minimal input helper work

- Worker slice 2:
  - standard-scene loading/runtime state and logic-thread scene/view rebuild

- Worker slice 3:
  - Sandbox test-registry cleanup and default-path simplification

- Worker slice 4:
  - documentation reconciliation and validation-side follow-up if it is independent

Do not run multiple workers against the same Sandbox application files in parallel unless ownership is explicitly separated first.

## Validation Notes

- This task changes shared engine/render behavior at the integration level, so Vulkan and DX12 both matter.
- The final acceptance gate must run both `Sandbox` and `Editor`.
- The standard scene must remain compatible with unattended smoke validation even though it is interactive in manual runs.

## Self-Review

Spec coverage check:

- single standard Sandbox scene: covered by Tasks 1 and 2
- Sponza loading: covered by Task 4
- free camera: covered by Task 3
- logic-thread scene/view updates: covered by Task 5
- render-thread submission: covered by Task 6
- smoke/manual runtime split: covered by Task 7
- docs: covered by Task 8
- four-pass validation: covered by Task 9

Placeholder scan:

- no `TODO`
- no `TBD`
- no “similar to previous task” dependencies

Type consistency:

- the plan consistently uses:
  - `SandboxStandardScene`
  - `SandboxFreeCameraController`
  - `Scene`
  - `RenderScene`
  - `SceneView`
  - `VisibleRenderFrame`
  - `SceneRenderer`

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-17-sandbox-standard-scene-implementation.md`.

Two execution options:

1. Subagent-Driven (recommended) - I dispatch a fresh subagent per task slice, review between slices, and keep the master agent on the architecture/integration critical path.

2. Inline Execution - Execute tasks in this session in sequence, with checkpoints but without per-slice worker delegation.
