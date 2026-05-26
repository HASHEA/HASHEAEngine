# Render Debug View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an Engine-level render target debug viewer controlled by `[RenderDebugView]`, with an Engine ImGui dropdown and fullscreen main-output visualization of the selected active RT.

**Architecture:** Add a focused `RenderDebugView` module under `Function/Render` that owns runtime config, frame-local debug item registration, UI state, and the fullscreen RenderGraph debug pass. `SceneRenderer` registers active deferred RTs and calls the debug view after tone-map; `Application` loads config and draws the Engine overlay UI.

**Tech Stack:** C++17 Engine Function layer, RenderGraph raster pass, HLSL fullscreen shader, `UIContext`, Engine self-tests, Vulkan/DX12 shared render path.

---

## File Map

- Create: `project/src/engine/Function/Render/RenderDebugView.h`
  - Public config types, debug item descriptors, registry helpers, UI and pass facade.
- Create: `project/src/engine/Function/Render/RenderDebugView.cpp`
  - Config parsing, runtime storage, registry behavior, UI drawing, shader program creation, RenderGraph pass.
- Create: `project/src/engine/Shaders/Debug/RenderDebugView.hlsl`
  - Fullscreen visualization shader for color, HDR, depth, normal, motion vector, AO, scalar.
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
  - Add failing tests first for config parsing, registry duplicate/update behavior, and graph pass contract.
- Modify: `project/src/engine/Function/Application.cpp`
  - Load runtime debug view config; draw debug view UI from Engine overlay.
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
  - Own `RenderDebugView`.
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
  - Initialize/shutdown debug view, register deferred RTs, append debug view pass after tone-map.
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.h`
  - Expose raw/final/temporal AO refs in output struct for registry.
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
  - Populate new output refs without changing AO behavior.
- Modify: `product/config/Engine.ini`
  - Add `[RenderDebugView]` defaults.
- Modify: `README.md`
  - Document systemized Render Debug View.
- Modify: `docs/EngineDeveloperGuide.md`
  - Document config, pass placement, active RT list.
- Modify: `docs/EngineUIContext.md`
  - Document Engine overlay usage without editor panel semantics.

## Task 1: Add Failing Self-Tests

**Files:**
- Modify: `project/src/engine/Base/EngineSelfTests.cpp`
- Reference future header: `project/src/engine/Function/Render/RenderDebugView.h`

- [ ] **Step 1: Include the future header**

Add:

```cpp
#include "Function/Render/RenderDebugView.h"
```

- [ ] **Step 2: Add config parsing test**

Add a self-test that writes:

```ini
[RenderDebugView]
Enabled=true
Selected=SceneGBufferE
```

Expected assertions:

```cpp
const RenderDebugViewConfig config = load_runtime_render_debug_view_config(config_path.string().c_str());
config.enabled == true;
config.selected == "SceneGBufferE";
```

Also write an invalid bool case:

```ini
[RenderDebugView]
Enabled=not-a-bool
Selected=SceneDeferredDepth
```

Expected assertions:

```cpp
invalid_config.enabled == make_default_render_debug_view_config().enabled;
invalid_config.selected == "SceneDeferredDepth";
```

- [ ] **Step 3: Add registry behavior test**

Use a headless `RenderGraphBuilder`, register two textures, and verify:

```cpp
RenderDebugViewFrameRegistry registry{};
registry.begin_frame();
registry.register_item({ "SceneGBufferA", "GBuffer A", tex_a, RenderDebugVisualization::Color, RenderTextureFormat::RGBA8_UNORM, 64, 64 });
registry.register_item({ "SceneGBufferA", "GBuffer A Updated", tex_b, RenderDebugVisualization::Normal, RenderTextureFormat::RGBA16_SFLOAT, 128, 64 });

const RenderDebugViewItem* item = registry.find_item("SceneGBufferA");
item != nullptr;
item->texture == tex_b;
item->visualization == RenderDebugVisualization::Normal;
registry.get_items().size() == 1;
```

- [ ] **Step 4: Add graph pass contract test**

Use `RenderDebugView::add_pass_for_tests(...)` or equivalent static helper to add a debug pass in a headless graph.

Expected compile assertions:

```cpp
result.live_pass_indices.size() == 2u;
result.pass_barriers[1].transitions includes selected RT -> SRVGraphics;
result.pass_barriers[1].transitions includes output -> RTV;
```

Also verify `Selected=Off` and `Selected=SceneOutput` return true without adding a pass.

- [ ] **Step 5: Register tests in `run_engine_base_self_tests()`**

Add calls after AO tests:

```cpp
all_passed = test_render_debug_view_config_parses_runtime_selection() && all_passed;
all_passed = test_render_debug_view_registry_replaces_duplicate_items() && all_passed;
all_passed = test_render_debug_view_graph_pass_contract() && all_passed;
```

- [ ] **Step 6: Run self-test and confirm RED**

Run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build or self-test fails because `RenderDebugView.h` and related symbols do not exist yet.

## Task 2: Implement RenderDebugView Config And Registry

**Files:**
- Create: `project/src/engine/Function/Render/RenderDebugView.h`
- Create: `project/src/engine/Function/Render/RenderDebugView.cpp`

- [ ] **Step 1: Define public config and visualization types**

Expose:

```cpp
enum class RenderDebugVisualization : uint8_t
{
    Color = 0,
    LinearHDR,
    Depth,
    Normal,
    MotionVector,
    AO,
    Scalar
};

struct RenderDebugViewConfig
{
    bool enabled = false;
    std::string selected = "Off";
};
```

- [ ] **Step 2: Define debug item and registry**

Expose:

```cpp
struct RenderDebugViewItem
{
    std::string name{};
    std::string display_name{};
    RenderGraphTextureRef texture{};
    RenderDebugVisualization visualization = RenderDebugVisualization::Color;
    RenderTextureFormat format = RenderTextureFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
};

class RenderDebugViewFrameRegistry
{
public:
    void begin_frame();
    void register_item(const RenderDebugViewItem& item);
    const RenderDebugViewItem* find_item(const std::string& name) const;
    const std::vector<RenderDebugViewItem>& get_items() const;
};
```

Duplicate names replace the existing item in-place so combo order stays stable.

- [ ] **Step 3: Implement config helpers**

Expose and implement:

```cpp
RenderDebugViewConfig make_default_render_debug_view_config();
RenderDebugViewConfig load_runtime_render_debug_view_config(const char* config_path);
void set_runtime_render_debug_view_config(const RenderDebugViewConfig& config);
RenderDebugViewConfig get_runtime_render_debug_view_config();
```

Use `IniConfig::try_get_bool("RenderDebugView", "Enabled", bool_value)` and `get_string("RenderDebugView", "Selected", config.selected)`.

- [ ] **Step 4: Run self-test and confirm partial GREEN**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: config and registry tests pass; graph pass test still fails until pass helper is implemented.

## Task 3: Implement Shader Program And RenderGraph Pass

**Files:**
- Modify: `project/src/engine/Function/Render/RenderDebugView.h`
- Modify: `project/src/engine/Function/Render/RenderDebugView.cpp`
- Create: `project/src/engine/Shaders/Debug/RenderDebugView.hlsl`

- [ ] **Step 1: Add RenderDebugView class facade**

Expose:

```cpp
class RenderDebugView
{
public:
    bool initialize(Renderer* renderer);
    void shutdown();
    void begin_frame();
    void register_item(const RenderDebugViewItem& item);
    bool add_pass(RenderGraphBuilder& graph, RenderGraphTextureRef output_target, const SceneRenderViewContext& view_context);
    void draw_ui(UIContext& ui_context);

    static bool should_bypass_debug_pass(const std::string& selected);
    static bool add_pass_for_tests(RenderGraphBuilder& graph, RenderGraphTextureRef selected, RenderGraphTextureRef output);
};
```

- [ ] **Step 2: Create fullscreen HLSL shader**

Shader resources:

```hlsl
Texture2D<float4> RenderDebugInput : register(t0);
SamplerState ScenePointClampSampler : register(s0);
```

Root constants include visualization mode, reverse-Z flag, manual sRGB flag, and optional scale:

```hlsl
cbuffer AshRootConstants : register(b0)
{
    float4 AshDebugViewParams0; // x mode, y reverseZ, z manualSRGB, w scale
    float4 AshDebugViewParams1; // reserved
};
```

Use fullscreen triangle `VSMain(uint vertex_id : SV_VertexID)`.

- [ ] **Step 3: Implement visualization modes**

Mode mapping:

```cpp
Color = 0
LinearHDR = 1
Depth = 2
Normal = 3
MotionVector = 4
AO = 5
Scalar = 6
```

Depth uses reverse-Z aware remap. Normal decodes oct-encoded `.rg`. Motion vector maps `.xy` around neutral grey.

- [ ] **Step 4: Implement `add_pass`**

Behavior:

- Return true and add no pass when disabled.
- Return true and add no pass for `Off` or `SceneOutput`.
- Return true and add no pass if selected item is unavailable.
- Otherwise add `SceneRenderDebugViewPass` that reads selected item as `GraphicsSRV` and writes `SceneOutput` as color attachment.

- [ ] **Step 5: Run self-test and confirm graph contract GREEN**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: new RenderDebugView self-tests pass.

## Task 4: Wire Runtime Config, UI, And SceneRenderer

**Files:**
- Modify: `project/src/engine/Function/Application.cpp`
- Modify: `project/src/engine/Function/Render/SceneRenderer.h`
- Modify: `project/src/engine/Function/Render/SceneRenderer.cpp`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.h`
- Modify: `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
- Modify: `product/config/Engine.ini`

- [ ] **Step 1: Load config during Application initialization**

After AO config load:

```cpp
const RenderDebugViewConfig renderDebugViewConfig = load_runtime_render_debug_view_config(config.backendConfigPath);
set_runtime_render_debug_view_config(renderDebugViewConfig);
```

- [ ] **Step 2: Draw Engine UI**

In `Application::draw_engine_overlay()`, after frame stats overlay:

```cpp
sceneRenderer.draw_render_debug_view_ui(*uiContext);
```

The UI window is interactive; it must not use `NoInputs`.

- [ ] **Step 3: Own and initialize RenderDebugView in SceneRenderer**

Add member:

```cpp
RenderDebugView m_render_debug_view{};
```

Initialize after renderer assignment and before dependent pass use. Shutdown before clearing renderer pointer.

- [ ] **Step 4: Expose AO debug refs**

Extend `AmbientOcclusionPassOutputs`:

```cpp
RenderGraphTextureRef raw_ao{};
RenderGraphTextureRef final_ao{};
RenderGraphTextureRef temporal_ao{};
```

Set them when AO pass creates or imports each texture.

- [ ] **Step 5: Register active RTs**

In `SceneRenderer::render_visible_frame()`:

```cpp
m_render_debug_view.begin_frame();
m_render_debug_view.register_item({ "SceneOutput", "Scene Output", output, RenderDebugVisualization::Color, view_context.output_target->get_format(), output_width, output_height });
m_render_debug_view.register_item({ "SceneDeferredDepth", "Depth", graph_resources.depth, RenderDebugVisualization::Depth, RenderTextureFormat::D32_SFLOAT, output_width, output_height });
```

Register GBuffer A/B/C as `Color`, GBufferD as `MotionVector`, GBufferE as `Normal`, AO as `AO`, lighting and HDR as `LinearHDR`.

- [ ] **Step 6: Insert debug pass after tone-map**

After `SceneDeferredToneMapPass`:

```cpp
ASH_PROCESS_ERROR(m_render_debug_view.add_pass(graph, output, view_context));
```

Keep `SceneDebugDrawOverlayPass` after debug view.

- [ ] **Step 7: Add config defaults**

Add to `product/config/Engine.ini`:

```ini
[RenderDebugView]
Enabled=false
Selected=Off
```

- [ ] **Step 8: Build and run self-test**

Run:

```powershell
.\build_sandbox.bat Debug x64
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: build succeeds, self-test exits 0.

## Task 5: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/EngineDeveloperGuide.md`
- Modify: `docs/EngineUIContext.md`

- [ ] **Step 1: Update README**

Add Render Debug View to the deferred path summary and mention `[RenderDebugView]`.

- [ ] **Step 2: Update EngineDeveloperGuide**

Add `[RenderDebugView]` to config examples and effective config list. Document pass placement:

```text
SceneDeferredToneMapPass -> SceneRenderDebugViewPass -> SceneDebugDrawOverlayPass
```

- [ ] **Step 3: Update EngineUIContext**

Add a short note that Engine overlay can host shared render debug controls through `UIContext`, while Editor panel/dock policy remains above Engine.

- [ ] **Step 4: Run markdown/diff sanity**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors from touched files. Existing unrelated whitespace in `product/config/editor/imgui.ini` may remain if present before this task.

## Task 6: Final Validation

**Files:**
- All touched files.

- [ ] **Step 1: Build Sandbox Debug**

Run:

```powershell
.\build_sandbox.bat Debug x64
```

Expected: exit 0.

- [ ] **Step 2: Run Debug self-test**

Run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --engine-self-test
```

Expected: exit 0.

- [ ] **Step 3: Run Vulkan smoke**

Set `product/config/Engine.ini` to `Backend=Vulkan`, then run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: graceful exit, no validation errors in logs.

- [ ] **Step 4: Run DX12 smoke**

Set `product/config/Engine.ini` to `Backend=DX12`, then run:

```powershell
.\product\bin64\Debug-windows-x86_64\Sandbox.exe --smoke-test-seconds=5
```

Expected: graceful exit, no DX12 debug layer errors in logs.

- [ ] **Step 5: Restore config**

Restore `Engine.ini` backend and debug-view defaults to the intended working state:

```ini
[RenderDebugView]
Enabled=false
Selected=Off
```

- [ ] **Step 6: Final diff review**

Run:

```powershell
git diff -- project/src/engine/Function/Render/RenderDebugView.h project/src/engine/Function/Render/RenderDebugView.cpp project/src/engine/Shaders/Debug/RenderDebugView.hlsl project/src/engine/Base/EngineSelfTests.cpp project/src/engine/Function/Application.cpp project/src/engine/Function/Render/SceneRenderer.h project/src/engine/Function/Render/SceneRenderer.cpp project/src/engine/Function/Render/AmbientOcclusionPass.h project/src/engine/Function/Render/AmbientOcclusionPass.cpp product/config/Engine.ini README.md docs/EngineDeveloperGuide.md docs/EngineUIContext.md
```

Expected: diff is scoped to Render Debug View and AO output refs needed for registration.
