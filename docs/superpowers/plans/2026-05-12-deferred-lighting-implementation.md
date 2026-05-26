# Deferred Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the first deferred lighting module on top of the existing DeferredHQ GBuffer path.

**Architecture:** Keep user material shaders path-independent: materials write surface attributes through the engine GBuffer host shader, then engine-owned deferred shaders decode GBuffer/depth and accumulate lighting into a `SceneLightingAccum` target. Directional lights use fullscreen draws; point and spot lights use sphere/cone volumes with additive blend, depth test enabled, depth write disabled, and cull none.

**Tech Stack:** C++17 Engine Function/Render and Function/Scene modules, existing RHI pipeline state, HLSL shaders, existing headless `Sandbox.exe --engine-self-test`, Vulkan + DX12 validation loop.

---

### Task 1: Contracts And Headless Self-Tests

**Files:**
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Modify: `project/src/engine/Function/Render/Material.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderScene.h`

- [ ] Add self-tests that require explicit render depth compare/blend state, stable shading-model IDs, and light extraction into `VisibleRenderFrame`.
- [ ] Run `product/bin64/Debug-windows-x86_64/Sandbox.exe --engine-self-test` and confirm the new tests fail or the build fails because the contracts are not implemented yet.

### Task 2: Render State And Material Contract

**Files:**
- Modify: `project/src/engine/Function/Render/RenderDevice.h`
- Modify: `project/src/engine/Function/Render/RenderDevice.cpp`
- Modify: `project/src/engine/Function/Render/Material.h`
- Modify: `project/src/engine/Function/Render/Material.cpp`
- Modify: `project/src/engine/Function/Render/MaterialShaderMap.cpp`
- Modify: `project/src/engine/Function/Render/MaterialShaderSourceBuilder.cpp`
- Modify: `project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl`

- [ ] Add high-level render compare op and blend mode to `GraphicsProgramState`.
- [ ] Map them to RHI depth-stencil and blend state in one shared function.
- [ ] Extend `MaterialShadingModel` to Empty, DefaultLitGGX, Unlit, and BlinnPhong while preserving `"DefaultLit"` JSON compatibility.
- [ ] Have generated material bindings define `ASH_MATERIAL_SHADING_MODEL_ID`.
- [ ] Write `GBufferA.a` as that ID and keep `0` reserved for empty/background.

### Task 3: Scene Light Snapshot And Deferred Resources

**Files:**
- Modify: `project/src/engine/Function/Scene/Scene.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/engine/Function/Render/RenderScene.h`
- Modify: `project/src/engine/Function/Render/RenderScene.cpp`
- Modify: `project/src/engine/Function/Render/SceneDeferredResources.h`
- Modify: `project/src/engine/Function/Render/SceneDeferredResources.cpp`

- [ ] Add `SceneLightExtractionDesc` from `Scene`.
- [ ] Store filtered `VisibleLightData` in `RenderScene` and copy it into each `VisibleRenderFrame`.
- [ ] Allocate `SceneLightingAccum` as `RGBA16_SFLOAT`, shader-resource capable, matching output size.

### Task 4: Deferred Lighting Pass

**Files:**
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.h`
- Modify: `project/src/engine/Function/Render/DeferredLightingPass.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Create/Modify: `project/src/engine/Shaders/Deferred/*.hlsl`

- [ ] Split deferred shader code into shared decode/BRDF/common includes plus pass entry shaders.
- [ ] Implement fullscreen base/emissive, directional lighting, point sphere lighting, spot cone lighting, and final composite.
- [ ] Create shared light-volume vertex/index buffers in `DeferredLightingPass`.
- [ ] Draw point/spot volumes with `depth_test = true`, `depth_compare = GreaterEqual`, `depth_write = false`, `cull_mode = None`, and additive blend.

### Task 5: Documentation And Validation

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`

- [ ] Document the deferred lighting path and render-state additions.
- [ ] Run `git diff --check`.
- [ ] Build Debug Sandbox/Editor.
- [ ] Run `Sandbox.exe --engine-self-test`.
- [ ] Run the validation loop for Sandbox + Editor on Vulkan + DX12.
