# Material System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build AshEngine V1 material system with formal `Material` / `MaterialInstance` runtime assets, section-level `MeshComponent` overrides, minimal texture loading, render-thread material proxies/resources, and fixed-PBR `Surface` rendering on both Vulkan and DX12.

**Architecture:** Keep the new material API in Engine-facing `Function/Render` types, extend `AssetDatabase` so `.material` assets load as runtime `MaterialInterface` objects, and let `RenderAssetManager` resolve imported `MaterialSlot` data into default/generated material instances plus texture GPU resources. `Scene` and `MeshComponent` stay renderer-agnostic; `RenderScene` and `SceneRenderer` consume resolved section material proxies and render with engine-owned surface shaders instead of Sandbox-owned shader files.

**Tech Stack:** C++17, `nlohmann::json`, existing `AssetDatabase`, `Scene`/`MeshComponent`, `RenderAssetManager`, `Renderer`/`RenderDevice`, `stb_image` third-party image decode, Vulkan + DX12 smoke validation.

**Execution Notes:** This repository does not currently have a dedicated Engine unit-test harness for this feature area. Validation in this plan therefore uses focused builds, runtime smoke checks, and documentation updates. Do not create commits during plan execution; the user performs all git commit actions manually.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
  - add `MaterialInterface` load APIs and material cache declarations
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`
  - implement `.material` load/async load/cache behavior and built-in material resolution
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
  - extend imported model/material metadata with stable default-material references
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
  - serialize new mesh/material override data and preserve imported material-slot metadata
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\SceneComponents.h`
  - add section-level mesh material overrides to `MeshComponent`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
  - extend `SceneMeshExtractionDesc` with material override data
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`
  - serialize/deserialize mesh overrides, component descriptors, and extraction logic
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
  - add material/texture request APIs, caches, and fallback-resource ownership
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
  - resolve imported/default materials, load textures, build render proxies/resources, and manage fallbacks
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.h`
  - replace raw color-only section data with resolved material bindings
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.h`
  - expose resolved section material bindings on static mesh primitive proxies
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.cpp`
  - propagate resolved section bindings into primitive proxies
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
  - extend visible draw packets with material proxy-aware section data
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`
  - build visible material draw packets without re-deriving material semantics per view
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
  - remove baked-in `base_color_factor` assumptions and consume per-section material proxies
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
  - render fixed-PBR surface materials through engine-owned shaders and per-material programs
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
  - expose a minimal texture upload creation path usable by Engine material textures
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
  - implement the minimal upload path and any required `RenderTarget` metadata support
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
  - forward the new texture-upload API
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
  - forward the new texture-upload API
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document the new material asset model, `MeshComponent` overrides, generated imported materials, and texture support limits

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
  - define `MaterialInterface`, `Material`, `MaterialInstance`, enums, parameter descriptors, and material file IO entry points
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
  - implement material serialization, built-in material factories, versioning, and parameter resolution
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
  - define `MaterialPassRelevance`, `MaterialResource`, and `MaterialRenderProxy`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
  - implement render-side fixed-PBR binding generation and program creation
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\TextureAsset.h`
  - define a minimal texture runtime object used by material parameters and fallback resources
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\TextureAsset.cpp`
  - implement image decode metadata, texture versioning, and GPU texture wrapper helpers
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`
  - engine-owned fixed-PBR surface shader used by `SceneRenderer`

## Task 1: Add Formal Material Runtime Types And AssetDatabase Material Loading

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`

- [ ] Add the core material enums and parameter vocabulary in `Material.h`.

```cpp
enum class MaterialDomain : uint8_t
{
    Surface = 0,
    Decal,
    PostProcess,
    UI
};

enum class MaterialBlendMode : uint8_t
{
    Opaque = 0,
    Masked,
    Transparent
};

enum class MaterialShadingModel : uint8_t
{
    DefaultLit = 0
};

enum class MaterialParameterType : uint8_t
{
    Scalar = 0,
    Vector4,
    Texture
};
```

- [ ] Define the runtime-facing material classes and keep them renderer-agnostic.

```cpp
struct MaterialParameterDesc
{
    std::string name{};
    MaterialParameterType type = MaterialParameterType::Scalar;
    std::string group{};
    glm::vec4 default_vector{ 0.0f };
    float default_scalar = 0.0f;
    std::string default_texture_path{};
};

class ASH_API MaterialInterface
{
public:
    virtual ~MaterialInterface() = default;
    virtual bool is_material_instance() const = 0;
    virtual MaterialDomain get_domain() const = 0;
    virtual MaterialBlendMode get_blend_mode() const = 0;
    virtual MaterialShadingModel get_shading_model() const = 0;
    virtual uint64_t get_change_version() const = 0;
    virtual const class Material* resolve_base_material() const = 0;
};
```

- [ ] Implement JSON file IO in `Material.cpp` for both `Material` and `MaterialInstance` and support these file-level entry points:

```cpp
ASH_API bool load_material_from_file(
    const std::filesystem::path& path,
    std::shared_ptr<MaterialInterface>& out_material,
    std::string* out_error = nullptr);

ASH_API bool save_material_to_file(
    const MaterialInterface& material,
    const std::filesystem::path& path,
    std::string* out_error = nullptr);
```

- [ ] Add built-in virtual material factories in `Material.cpp` for:
  - `Engine/Materials/M_SurfacePBR.material`
  - `Engine/Materials/M_DefaultSurface.material`

Use a helper shaped like:

```cpp
ASH_API std::shared_ptr<MaterialInterface> make_builtin_material(std::string_view virtual_path);
```

- [ ] Extend `AssetDatabase` with material APIs and cache entries:

```cpp
bool load_material_by_id(AssetId id, std::shared_ptr<const MaterialInterface>& out_material);
bool load_material_by_path(const std::filesystem::path& path, std::shared_ptr<const MaterialInterface>& out_material);
std::shared_future<std::shared_ptr<const MaterialInterface>> load_material_by_id_async(AssetId id);
std::shared_future<std::shared_ptr<const MaterialInterface>> load_material_by_path_async(const std::filesystem::path& path);
```

Cache materials by normalized path exactly like meshes/models/ashassets. Resolve built-in virtual material paths before trying disk IO.

- [ ] Run a focused Engine build.

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
- `.material` files are now a first-class runtime asset type
- built-in material virtual paths resolve without touching Sandbox code

## Task 2: Add Minimal Texture Runtime Objects And GPU Texture Upload

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\TextureAsset.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\TextureAsset.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Define a minimal runtime texture object that can back material texture parameters.

```cpp
enum class TextureColorSpace : uint8_t
{
    Linear = 0,
    SRGB
};

class ASH_API TextureAsset
{
public:
    std::string asset_path{};
    uint32_t width = 0;
    uint32_t height = 0;
    TextureColorSpace color_space = TextureColorSpace::Linear;
    std::shared_ptr<RenderTarget> resource = nullptr;
    uint64_t change_version = 0;
};
```

- [ ] Add a minimal upload API to `RenderDevice`/`Renderer`. Do not overload `create_render_target()` with ad-hoc texture semantics; add an explicit helper.

```cpp
struct TextureUploadDesc
{
    uint16_t width = 1;
    uint16_t height = 1;
    RenderTextureFormat format = RenderTextureFormat::RGBA8_UNORM;
    const void* initial_data = nullptr;
    uint32_t row_pitch = 0;
    bool srgb = false;
    const char* name = nullptr;
};

std::shared_ptr<RenderTarget> create_texture_2d(const TextureUploadDesc& desc);
```

Implement the upload path once in `RenderDevice.cpp`, then forward it through `Renderer`.

- [ ] In `RenderAssetManager`, add texture caches and request helpers:

```cpp
std::shared_ptr<TextureAsset> request_texture_asset(
    const std::string& asset_path,
    TextureColorSpace color_space);
```

Decode image files with `stbi_load` / `stbi_loadf` from the repo's `stb_image` third-party library in a new `TextureAsset.cpp`. Support `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, and `.hdr` in V1. If decode fails or the file is `.dds`, log once and fall back to an engine-owned default texture.

- [ ] Add three engine-owned fallback textures in `RenderAssetManager`:
  - white albedo
  - black emissive/packed data
  - flat normal `(0.5, 0.5, 1.0, 1.0)`

Represent them as tiny uploaded `TextureAsset`s created during `RenderAssetManager::initialize(...)`.

- [ ] Rebuild `Engine` after the upload API and texture cache land.

Run the same `MSBuild.exe ... /t:Engine` command from Task 1.

Expected:

- `Engine` builds
- there is now one explicit Engine path for “image asset -> GPU texture”
- missing or unsupported textures resolve to stable fallback resources instead of crashing

## Task 3: Extend Imported Model Data And `MeshComponent` With Material References

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\SceneComponents.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Scene\Scene.cpp`

- [ ] Add stable default-material references to imported model data without deleting the existing `MaterialSlot` import metadata.

Use a compact shape like:

```cpp
struct ModelMaterialReference
{
    uint32_t material_slot = k_invalid_material_slot;
    std::string material_path{};
};

struct Model
{
    // existing fields...
    std::vector<ModelMaterialReference> default_materials{};
};
```

- [ ] Extend `MeshComponent` with section-level overrides keyed by `material_slot`.

```cpp
struct MeshMaterialOverride
{
    uint32_t material_slot = k_invalid_material_slot;
    std::string material_path{};
};

struct MeshComponent
{
    std::string asset_path{};
    uint32_t mesh_index = 0;
    std::vector<MeshMaterialOverride> material_overrides{};
    bool visible = true;
    SceneMobility mobility = SceneMobility::Static;
    uint32_t layer_mask = k_default_scene_layer_mask;
};
```

- [ ] Update scene component descriptors and serialization in `Scene.cpp` so `material_overrides` round-trips through `.scene` files and contributes to scene `change_version`.

Use a JSON layout shaped like:

```json
"material_overrides": [
  { "material_slot": 0, "material_path": "materials/MI_Helmet.material" }
]
```

- [ ] Update `load_ashasset_from_file()` / `save_ashasset_to_file()` in `AssetData.cpp` so prefab-style `.ashasset` files preserve `material_overrides`, and keep backward compatibility when the field is missing.

- [ ] Extend `SceneMeshExtractionDesc` to carry resolved component-side overrides:

```cpp
struct SceneMeshExtractionDesc
{
    EntityId entity_id = 0;
    std::string asset_path{};
    uint32_t mesh_index = 0;
    std::vector<MeshMaterialOverride> material_overrides{};
    bool visible = true;
    SceneMobility mobility = SceneMobility::Static;
    uint32_t layer_mask = k_default_scene_layer_mask;
    glm::mat4 world_transform{ 1.0f };
};
```

- [ ] Rebuild `Engine` and verify old scene/prefab files still load.

Run the same `MSBuild.exe ... /t:Engine` command from Task 1.

Expected:

- `Engine` builds
- `MeshComponent` now has formal section override data
- missing `material_overrides` arrays in old files default to empty without load failures

## Task 4: Resolve Imported/Default Materials Inside `RenderAssetManager`

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\StaticMeshRenderAsset.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Replace the color-only section payload with resolved material-facing fields.

Update `StaticMeshRenderSection` toward this shape:

```cpp
struct ASH_API StaticMeshRenderSection
{
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t material_slot = k_invalid_material_slot;
    MeshPrimitiveTopology topology = MeshPrimitiveTopology::Triangles;
    std::shared_ptr<MaterialInterface> material = nullptr;
    std::shared_ptr<class MaterialRenderProxy> material_proxy = nullptr;
};
```

- [ ] Add material caches to `RenderAssetManager`:

```cpp
std::unordered_map<std::string, std::shared_ptr<const MaterialInterface>> m_material_assets{};
std::unordered_map<std::string, std::shared_ptr<MaterialRenderProxy>> m_material_proxies{};
std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_texture_assets{};
```

- [ ] Implement default material resolution when populating a static mesh render asset.

Use the following resolution order for every section:

```cpp
component override by material_slot
-> explicit model default material path
-> generated imported material instance from MaterialSlot
-> Engine/Materials/M_DefaultSurface.material
```

The generated imported material instance must be cached under a deterministic key such as:

```cpp
std::string make_generated_material_key(const std::string& asset_path, uint32_t material_slot);
```

Its parent should be the built-in `Engine/Materials/M_SurfacePBR.material`, and its overrides should come from imported `MaterialSlot` factors and texture paths.

- [ ] Build `MaterialRenderProxy` objects lazily and cache them by final resolved material identity plus relevant static state. Do **not** derive them ad hoc in `SceneRenderer`.

- [ ] Keep this task renderer-agnostic at the Scene boundary: `RenderAssetManager` may depend on material and texture render objects, but `MeshComponent` and `Scene` must still only store paths and override lists.

- [ ] Rebuild `Engine`.

Run the same `MSBuild.exe ... /t:Engine` command from Task 1.

Expected:

- `RenderAssetManager` now owns the mesh + material + texture resolution bridge
- imported models without explicit `.material` files still resolve to generated material instances
- fallback material resolution is deterministic and centralized

## Task 5: Add Render-Side Material Resources And Propagate Them Through `RenderScene`

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderScene.cpp`

- [ ] Define `MaterialPassRelevance`, `MaterialResource`, and `MaterialRenderProxy`.

Use a shape like:

```cpp
struct MaterialPassRelevance
{
    bool supports_surface = false;
    bool supports_depth_prepass = false;
    bool supports_base_pass = false;
    bool is_masked = false;
    bool is_transparent = false;
    MaterialDomain domain = MaterialDomain::Surface;
};

struct MaterialResource
{
    MaterialPassRelevance pass_relevance{};
    MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
    MaterialShadingModel shading_model = MaterialShadingModel::DefaultLit;
    std::shared_ptr<UniformBuffer> material_uniforms = nullptr;
};
```

- [ ] In `MaterialRenderProxy.cpp`, cache one `GraphicsProgram` per proxy instead of sharing one mutable global program across multiple materials.

This is required because `Renderer::GraphicsPassContext` defers draw execution until `end_pass()`. If multiple draws mutate the same `GraphicsProgram` with different textures before the pass executes, earlier draws would incorrectly see later bindings.

Cache the program inside the proxy:

```cpp
class ASH_API MaterialRenderProxy
{
public:
    const MaterialResource& get_resource() const;
    GraphicsProgram* get_program() const;
    bool ensure_program(Renderer& renderer);
    bool update_bindings(RenderAssetManager& asset_manager);
};
```

- [ ] Use a material uniform buffer for fixed-PBR factors and keep per-draw root constants limited to object/view data.

- [ ] Propagate resolved section material proxies into `StaticMeshPrimitiveProxy`, `VisibleStaticMeshDraw`, and `VisibleRenderFrame`. Once this task is done, `RenderScene` should no longer rely on `section.base_color_factor` at all.

- [ ] Rebuild `Engine` and `Sandbox`.

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

- `Engine` and `Sandbox` build
- visible draw packets now carry resolved section material proxies
- no draw path still depends on raw imported color-only section data

## Task 6: Move Surface Rendering To An Engine-Owned Fixed-PBR Shader

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`

- [ ] Add an engine-owned surface shader and stop depending on `project/src/sandbox/Shaders/SceneStaticMesh.hlsl`.

Start from this minimal contract:

```hlsl
cbuffer AshRootConstants
{
    float4x4 ObjectToClip;
};

cbuffer MaterialConstants
{
    float4 BaseColorFactor;
    float4 EmissiveFactorAndAlphaCutoff;
    float2 MetallicRoughness;
    float2 Padding;
};

Texture2D BaseColorTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MetallicRoughnessTexture : register(t2);
Texture2D EmissiveTexture : register(t3);
SamplerState LinearWrapSampler : register(s0);
```

The first shader version may stay visually simple, but it must consume material textures/factors and support `Opaque` plus `Masked`.

- [ ] Shrink `SceneRenderer::SceneObjectConstants` so it no longer contains `base_color_factor`.

```cpp
struct SceneObjectConstants
{
    glm::mat4 object_to_clip{ 1.0f };
};
```

- [ ] In `SceneRenderer::render_visible_frame(...)`, use the section’s `MaterialRenderProxy`:
  - skip sections whose `pass_relevance.domain != MaterialDomain::Surface`
  - skip `Transparent` materials in V1 with a single warning log
  - use `section.material_proxy->get_program()` as `draw_desc.program`
  - only write per-draw object constants into `draw_desc.const_data`

- [ ] Remove the old hidden assumption that one `SceneRenderer`-owned graphics program covers every static mesh section. The durable cached shader program should now live on the material proxy, not on `SceneRenderer`.

- [ ] Rebuild `Engine`, `Sandbox`, and `Editor`.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox;Editor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- no Engine rendering path references Sandbox shader files anymore
- `SceneRenderer` consumes material proxies instead of ad hoc color factors
- the build succeeds for all three targets

## Task 7: Update Engine Documentation And Run Full Vulkan + DX12 Validation

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`

- [ ] Update `EngineDeveloperGuide.md` to document:
  - the new `MaterialInterface / Material / MaterialInstance` object model
  - `.material` asset loading
  - `MeshComponent.material_overrides`
  - imported `MaterialSlot` -> generated material instance fallback behavior
  - fixed-PBR `Surface` V1 limits
  - texture support limits and fallback textures
  - the fact that Engine-owned scene rendering now uses an engine shader path, not Sandbox shader files

- [ ] Build the full solution once before runtime validation.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Build `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- full solution builds successfully

- [ ] Validate `Sandbox` on Vulkan.

Run:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
-replace '^Backend=.*', 'Backend=Vulkan' `
| Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe' --smoke-test-seconds=25
```

Expected:

- normal startup and shutdown
- no Vulkan validation errors
- no texture/material-related crash when loading the standard scene

- [ ] Validate `Sandbox` on DX12.

Run:

```powershell
(Get-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini') `
-replace '^Backend=.*', 'Backend=DX12' `
| Set-Content 'D:\workspace\AshEngine\HASHEAEngine\product\config\Engine.ini'

& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe' --smoke-test-seconds=25
```

Expected:

- normal startup and shutdown
- no DX12 debug-layer errors
- no texture/material-related crash when loading the standard scene

- [ ] Validate `Editor` on Vulkan and DX12 without modifying Editor code.

Run:

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

- normal startup and shutdown in both backends
- no validation errors or resource leak signals
- Editor scene-driven viewport path remains compatible with the new engine material system

## Coverage Check

- Spec section 1-6: covered by Tasks 1, 4, 5, and 6 through the new material object model and render proxy/resource split.
- Spec section 7: covered by Task 1 for `.material` assets and Task 2 for formal texture runtime objects.
- Spec section 8: covered by Task 3 for `MeshComponent` overrides and Task 4 for section material resolution.
- Spec section 9: covered by Tasks 5 and 6 through render-side proxies, pass relevance, and engine-owned shader programs.
- Spec section 10: covered by Task 4 through imported material generation and fallback resolution order.
- Spec section 11: covered by Tasks 1, 4, and 5 through change-versioned material objects, cached proxies, and immutable render-side state.
- Spec section 12-14: covered by Task 7 validation plus the explicit task boundaries above.

## Notes

- Do not add Editor-specific material APIs in this plan. Editor validation is required, but Editor code remains untouched unless a later user instruction explicitly expands scope.
- Do not create git commits during execution. After the implementation and validation complete, prepare commit-log text for the user instead.
