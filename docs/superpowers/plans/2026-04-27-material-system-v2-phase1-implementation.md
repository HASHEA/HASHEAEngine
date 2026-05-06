# Material System V2 Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the V2 material framework core for AshEngine, introduce `.AshMat` assets plus EngineShader/MaterialShader compilation, and migrate the `Surface.StaticMesh` opaque/masked path onto the new framework without breaking existing V1 `.material` rendering.

**Architecture:** Extend the existing `MaterialInterface / Material / MaterialInstance` runtime objects so they can describe V2 compile-time/static data while preserving V1 compatibility, then add a new `MaterialSystem + MaterialShaderMap + EngineShaderFamilyRegistry` layer that owns permutation compilation, generated HLSL bindings, and immutable material resources. Phase 1 deliberately keeps `Surface.StaticMesh` as the only fully-onboarded V2 family and leaves `Surface.SkeletalMesh`, `Surface.Transparent`, `Decal.Deferred`, and V1 retirement to follow-on plans after this vertical slice validates cleanly on Vulkan and DX12.

**Tech Stack:** C++17, `nlohmann::json`, existing `AssetDatabase`, `RenderAssetManager`, `Renderer` / `RenderDevice`, DXC preprocessing/include-handler path, HLSL, Vulkan + DX12 smoke validation.

**Execution Notes:**
- Execute this plan in a dedicated worktree before touching code.
- Do not edit `project/src/editor`.
- Keep legacy `.material / .mat` assets rendering throughout Phase 1; the V2 path is additive until later migration phases.
- This repository does not currently have an Engine unit-test harness for this feature area. Validation therefore uses focused builds, runtime smoke coverage, and the existing Vulkan + DX12 validation loop.
- Do not create commits during plan execution; the user performs git commit actions manually.

---

## Scope Decision

The approved spec covers multiple downstream runtime paths that are not yet present in the codebase:

- `Surface.StaticMesh`
- `Surface.SkeletalMesh`
- `Surface.Transparent`
- `Decal.Deferred`
- V1 retirement / asset migration

`Surface.SkeletalMesh` and `Decal` do not currently have a real Engine runtime/render path to wire into, and `Transparent` needs its own renderer queue/orchestration. To keep the first implementation slice testable, this plan intentionally covers only:

- V2 material asset/schema/runtime foundations
- shader composition and generated bindings infrastructure
- `Surface.StaticMesh` + `BasePass / DepthOnly`
- explicit `.AshMat` runtime validation in `Sandbox`

Follow-on plans are required after this phase lands for:

- `Surface.SkeletalMesh`
- `Surface.Transparent`
- `Decal.Deferred`
- V1 compatibility retirement and offline migration

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
  - extend runtime material objects with V2 static metadata, resource declarations, compile hash, and `.AshMat`-relevant getters while keeping V1 APIs usable
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
  - implement `.AshMat` load/save, V1 compatibility branches, compile-hash generation, and built-in V2 fallback material factories
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
  - keep material load APIs stable while broadening accepted asset extensions
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`
  - detect `.AshMat`, preserve caching, and resolve new built-in fallback material virtual paths
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
  - own a `MaterialSystem`, V2 fallback accessors, and proxy/update plumbing
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
  - instantiate/use `MaterialSystem`, keep legacy imported materials alive, and route `.AshMat` proxies through the V2 branch
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
  - split legacy V1 bindings from V2 `MaterialShaderMap / MaterialBindingSnapshot / MaterialResource` state
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
  - implement V2 compile/bind/update flow while preserving the current fixed-PBR legacy path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
  - carry the minimal state needed to consume V2 `Surface.StaticMesh` base/depth resources
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
  - consume V2 material resources for `Surface.StaticMesh`, keep `Transparent` skipped in Phase 1, and preserve legacy V1 draws
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
  - extend `GraphicsProgramDesc` with host/user/generated-binding shader source paths while keeping old single-path creation available
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
  - pass the new shader source tuple into `RHI::ShaderCreation` and keep legacy graphics/compute program creation working
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Shader.h`
  - add generated-bindings path support to `RHI::ShaderCreation` and its hash
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCHelper.h`
  - extend `ShaderItem` to carry the generated-bindings include path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCHelper.cpp`
  - thread the generated-bindings placeholder path through DXC preprocessing
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCIncludeHandler.h`
  - add a second placeholder include hook beside `UserShader.hlsli`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCIncludeHandler.cpp`
  - resolve `GeneratedMaterialBindings.hlsli` to the per-permutation generated file path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Shader.cpp`
  - pass the generated-bindings path into the DX12 include-handler flow
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.cpp`
  - pass the generated-bindings path into Vulkan preprocessing
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
  - inject one explicit `.AshMat` override so Phase 1 runtime validation always exercises the V2 path
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document the Phase 1 V2 material framework, `.AshMat`, host/user/generated shader composition, and validation rules

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.h`
  - public Engine-facing V2 material subsystem API and key usage/permutation types
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.cpp`
  - registry bootstrap, fallback setup, and permutation request orchestration
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.h`
  - immutable compiled-permutation resource model, binding layouts, and snapshot declarations
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.cpp`
  - shader-map caching, material resource creation, and generated-source file bookkeeping
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\EngineShaderFamilyRegistry.h`
  - family/pass/capability registration declarations
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\EngineShaderFamilyRegistry.cpp`
  - Phase 1 registration for `Surface.StaticMesh`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderSourceBuilder.h`
  - generated HLSL binding file builder interface
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderSourceBuilder.cpp`
  - `.generated.hlsli` emission, compile-environment macro emission, and cache-path naming
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Domains\AshSurfaceDomain.hlsli`
  - Phase 1 `Surface` domain ABI and default-node initialization
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Families\SurfaceStaticMeshBasePass.hlsl`
  - base-pass host shader that consumes `Surface` nodes from a material shader
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Families\SurfaceStaticMeshDepthOnly.hlsl`
  - depth-only host shader for masked/opaque `Surface.StaticMesh`
- `D:\workspace\AshEngine\HASHEAEngine\assets\hlsl\include\UserShader.hlsli`
  - placeholder include file name used by the DXC include-handler substitution path
- `D:\workspace\AshEngine\HASHEAEngine\assets\hlsl\include\GeneratedMaterialBindings.hlsli`
  - placeholder include file name used by the generated-bindings substitution path
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Shaders\MaterialV2\M_V2_DebugSurface.hlsl`
  - hand-authored test material shader for Phase 1 runtime validation
- `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\M_V2_DebugSurface.AshMat`
  - hand-authored Phase 1 test material asset
- `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_DebugSurface_Tint.AshMat`
  - instance override asset proving runtime-only parameter/resource updates

## Task 1: Extend Runtime Materials To Represent `.AshMat` Without Breaking V1

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`

- [ ] Add V2 compile-time/static metadata types to `Material.h` and keep them orthogonal to runtime overrides.

```cpp
enum class MaterialResourceType : uint8_t
{
    Texture2D = 0
};

enum class MaterialCompareOp : uint8_t
{
    LessEqual = 0,
    Always
};

enum class MaterialResourceColorSpace : uint8_t
{
    Linear = 0,
    SRGB
};

struct MaterialStaticSwitchDesc
{
    std::string name{};
    bool value = false;
};

struct MaterialResourceDesc
{
    std::string name{};
    MaterialResourceType type = MaterialResourceType::Texture2D;
    std::string default_path{};
    std::string sampler{};
    MaterialResourceColorSpace color_space = MaterialResourceColorSpace::Linear;
};

struct MaterialStaticRenderStateDesc
{
    MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
    bool two_sided = false;
    RenderCullMode cull_mode = RenderCullMode::Back;
    bool depth_write = true;
    MaterialCompareOp depth_test = MaterialCompareOp::LessEqual;
    float alpha_cutoff = 0.5f;
};
```

- [ ] Extend `MaterialInterface` so the V2 framework can query static compile data without reaching into concrete subclasses.

```cpp
class ASH_API MaterialInterface
{
public:
    virtual bool is_v2_material() const = 0;
    virtual std::string_view get_material_shader_path() const = 0;
    virtual const MaterialStaticRenderStateDesc& get_static_render_state() const = 0;
    virtual const std::vector<std::string>& get_required_capabilities() const = 0;
    virtual const std::vector<MaterialStaticSwitchDesc>& get_static_switches() const = 0;
    virtual const std::vector<MaterialResourceDesc>& get_resource_descs() const = 0;
    virtual uint64_t get_compile_hash() const = 0;
};
```

- [ ] Implement `.AshMat` parsing/saving in `Material.cpp` and preserve `.material / .mat` compatibility. The V2 parse branch must accept this shape:

```json
{
  "version": 2,
  "class": "Material",
  "name": "M_V2_DebugSurface",
  "domain": "Surface",
  "materialShader": "project/src/sandbox/Shaders/MaterialV2/M_V2_DebugSurface.hlsl",
  "requiredCapabilities": [],
  "staticSwitches": {
    "USE_VERTEX_COLOR": false
  },
  "renderState": {
    "blendMode": "Opaque",
    "twoSided": false,
    "cullMode": "Back",
    "depthWrite": true,
    "depthTest": "LessEqual",
    "alphaCutoff": 0.5
  },
  "samplers": {
    "WrapLinear": {
      "addressU": "Repeat",
      "addressV": "Repeat",
      "addressW": "Repeat",
      "minFilter": "Linear",
      "magFilter": "Linear",
      "mipFilter": "Linear"
    }
  },
  "parameters": {
    "BaseColorTint": {
      "type": "float4",
      "value": [1.0, 1.0, 1.0, 1.0]
    }
  },
  "resources": {
    "BaseColorTex": {
      "type": "Texture2D",
      "path": "models/gltfs/DamagedHelmet/glTF/Default_albedo.jpg",
      "sampler": "WrapLinear",
      "colorSpace": "sRGB"
    }
  }
}
```

- [ ] Add a stable compile-hash helper that only uses compile-affecting fields.

```cpp
static uint64_t build_material_compile_hash(const Material& material)
{
    uint64_t hash_value = 0;
    ASH_HASH::hash_combine(hash_value, material.get_domain());
    ASH_HASH::hash_combine(hash_value, material.get_material_shader_path(), ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hash_value, material.get_static_render_state().blend_mode);
    ASH_HASH::hash_combine(hash_value, material.get_static_render_state().two_sided);
    for (const std::string& capability : material.get_required_capabilities())
    {
        ASH_HASH::hash_combine(hash_value, capability, std::hash<std::string>{});
    }
    for (const MaterialStaticSwitchDesc& sw : material.get_static_switches())
    {
        ASH_HASH::hash_combine(hash_value, sw.name, std::hash<std::string>{});
        ASH_HASH::hash_combine(hash_value, sw.value);
    }
    for (const MaterialParameterDesc& parameter : material.get_parameter_descs())
    {
        ASH_HASH::hash_combine(hash_value, parameter.name, std::hash<std::string>{});
        ASH_HASH::hash_combine(hash_value, parameter.type);
    }
    for (const MaterialResourceDesc& resource : material.get_resource_descs())
    {
        ASH_HASH::hash_combine(hash_value, resource.name, std::hash<std::string>{});
        ASH_HASH::hash_combine(hash_value, resource.type);
        ASH_HASH::hash_combine(hash_value, resource.sampler, std::hash<std::string>{});
        ASH_HASH::hash_combine(hash_value, resource.color_space);
    }
    return hash_value;
}
```

- [ ] Teach `AssetDatabase` to classify `.AshMat` as `AssetType::Material` while keeping the old extensions alive.

```cpp
if (ext == ".ashmat" || ext == ".mat" || ext == ".material")
{
    return AssetType::Material;
}
```

- [ ] Add built-in V2 fallback virtual material resolution in `make_builtin_material(...)` for at least:
  - `Engine/Materials/V2/M_DefaultSurface.AshMat`

Use a helper shaped like:

```cpp
static std::shared_ptr<MaterialInterface> build_default_surface_v2_material()
{
    auto material = std::make_shared<Material>();
    material->set_name("M_DefaultSurfaceV2");
    material->set_asset_path("Engine/Materials/V2/M_DefaultSurface.AshMat");
    material->set_domain(MaterialDomain::Surface);
    material->set_material_shader_path("project/src/sandbox/Shaders/MaterialV2/M_V2_DebugSurface.hlsl");
    material->set_static_render_state(MaterialStaticRenderStateDesc{});
    return material;
}
```

- [ ] Run a focused Engine build after the schema/API changes.

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
- `.AshMat` files are classified as material assets
- old `.material` files still load unchanged
- compile hash does not change when only runtime instance override values change

## Task 2: Add The Phase 1 V2 Framework Core (`MaterialSystem`, `MaterialShaderMap`, `EngineShaderFamilyRegistry`)

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\EngineShaderFamilyRegistry.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\EngineShaderFamilyRegistry.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Define the phase-1 usage axes and make the family/pass scope explicit instead of open-ended.

```cpp
enum class EngineShaderFamily : uint8_t
{
    SurfaceStaticMesh = 0
};

enum class PassFamily : uint8_t
{
    DepthOnly = 0,
    BasePass
};

enum class MaterialCapability : uint64_t
{
    None = 0,
    VertexColor = 1ull << 0,
    UV1 = 1ull << 1
};

struct MaterialUsageDesc
{
    MaterialDomain domain = MaterialDomain::Surface;
    EngineShaderFamily family = EngineShaderFamily::SurfaceStaticMesh;
    PassFamily pass = PassFamily::BasePass;
    uint64_t capability_mask = 0;
};
```

- [ ] Add immutable compiled-resource and runtime-binding snapshot declarations in `MaterialShaderMap.h`.

```cpp
struct MaterialBindingLayoutEntry
{
    std::string name{};
    RHI::ShaderResourceBindingType type = RHI::ShaderResourceBindingType::Unknown;
    uint32_t bind_point = 0;
};

struct MaterialBindingSnapshot
{
    uint64_t version = 0;
    std::vector<uint8_t> packed_parameter_data{};
    std::unordered_map<std::string, std::shared_ptr<RenderTarget>> textures{};
    std::unordered_map<std::string, std::shared_ptr<RenderSampler>> samplers{};
};

struct MaterialResource
{
    MaterialUsageDesc usage{};
    uint64_t combined_source_hash = 0;
    std::vector<MaterialBindingLayoutEntry> binding_layout{};
    std::unique_ptr<GraphicsProgram> program = nullptr;
    MaterialPassRelevance pass_relevance{};
};
```

- [ ] Register only the Phase 1 family/pass pairs in `EngineShaderFamilyRegistry.cpp`.

```cpp
static const EngineShaderFamilyDesc k_phase1_families[] = {
    {
        EngineShaderFamily::SurfaceStaticMesh,
        MaterialDomain::Surface,
        "Surface.StaticMesh",
        "project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshBasePass.hlsl",
        "project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshDepthOnly.hlsl",
        static_cast<uint64_t>(MaterialCapability::VertexColor) |
            static_cast<uint64_t>(MaterialCapability::UV1)
    }
};
```

- [ ] Implement `MaterialSystem` as the single place that validates `domain + family + pass`, chooses fallback materials, and asks a `MaterialShaderMap` for a `MaterialResource`.

```cpp
class ASH_API MaterialSystem
{
public:
    bool initialize(Renderer* renderer);
    void shutdown();

    MaterialResource* get_or_create_resource(
        const MaterialInterface& material,
        const MaterialUsageDesc& usage,
        std::string* out_error = nullptr);

    const MaterialInterface* get_domain_fallback(MaterialDomain domain) const;

private:
    Renderer* m_renderer = nullptr;
    EngineShaderFamilyRegistry m_family_registry{};
    std::unordered_map<std::string, std::shared_ptr<MaterialShaderMap>> m_shader_maps{};
};
```

- [ ] Let `RenderAssetManager` own exactly one `MaterialSystem` so existing render-thread consumers have one access point.

```cpp
class ASH_API RenderAssetManager
{
public:
    MaterialSystem* get_material_system();

private:
    MaterialSystem m_material_system{};
};

void RenderAssetManager::initialize(AssetDatabase* asset_database, Renderer* renderer)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_asset_database = asset_database;
    m_renderer = renderer;
    m_material_system.initialize(renderer);
}
```

- [ ] Run a focused Engine build after the new framework types land.

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
- the V2 framework has exactly one registered Phase 1 family (`Surface.StaticMesh`)
- unsupported usages fail early with clear logs instead of silently compiling nonsense

## Task 3: Add Host/User/Generated-Bindings Shader Composition

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Shader.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCHelper.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCHelper.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCIncludeHandler.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DXC\DXCIncludeHandler.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Shader.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\assets\hlsl\include\UserShader.hlsli`
- Create: `D:\workspace\AshEngine\HASHEAEngine\assets\hlsl\include\GeneratedMaterialBindings.hlsli`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Domains\AshSurfaceDomain.hlsli`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Families\SurfaceStaticMeshBasePass.hlsl`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Families\SurfaceStaticMeshDepthOnly.hlsl`

- [ ] Extend `RHI::ShaderCreation` and its hash so generated bindings participate in shader identity.

```cpp
struct ShaderCreation
{
    const char* pBaseShaderPath = nullptr;
    const char* pUserShaderPath = nullptr;
    const char* pGeneratedBindingsPath = nullptr;
    const char* pShaderDef = nullptr;
    const char* pShaderMacro = nullptr;
    const char* pEntryPoint = nullptr;
    AshShaderStageFlagBits type = AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

inline uint64_t get_shader_hash(const ShaderCreation& ci)
{
    uint64_t hashCode = 0;
    ASH_HASH::hash_combine(hashCode, ci.pBaseShaderPath, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.pUserShaderPath, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.pGeneratedBindingsPath, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.pShaderDef, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.pShaderMacro, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.pEntryPoint, ASH_HASH::CStringHash{});
    ASH_HASH::hash_combine(hashCode, ci.type);
    return hashCode;
}
```

- [ ] Extend `GraphicsProgramDesc` so Phase 1 can pass a host shader path plus a user shader path and generated-bindings path, while preserving old one-file call sites.

```cpp
struct GraphicsProgramDesc
{
    const char* shader_path = nullptr; // legacy
    const char* base_shader_path = nullptr;
    const char* user_shader_path = nullptr;
    const char* generated_bindings_path = nullptr;
    const char* vertex_entry = "VSMain";
    const char* fragment_entry = "PSMain";
    const char* shader_macro = nullptr;
    GraphicsProgramState state{};
    const char* name = nullptr;
    std::shared_ptr<const VertexDecl> vertex_decl = nullptr;
    RHI::VertexInputCreation vertex_input{};
};
```

- [ ] Update `RenderDevice::create_graphics_program(...)` to use the new tuple and keep old callers working.

```cpp
const char* base_shader_path =
    desc.base_shader_path ? desc.base_shader_path : desc.shader_path;

RHI::ShaderCreation vertex_shader_creation{};
vertex_shader_creation.pBaseShaderPath = base_shader_path;
vertex_shader_creation.pUserShaderPath = desc.user_shader_path;
vertex_shader_creation.pGeneratedBindingsPath = desc.generated_bindings_path;
vertex_shader_creation.pShaderMacro = desc.shader_macro;
vertex_shader_creation.pEntryPoint = desc.vertex_entry;
vertex_shader_creation.type = RHI::ASH_SHADER_STAGE_VERTEX_BIT;
```

- [ ] Add a second placeholder include hook in the DXC include handler beside `UserShader.hlsli`.

```cpp
const std::filesystem::path defaultUserShaderName = L"UserShader.hlsli";
const std::filesystem::path defaultGeneratedBindingsName = L"GeneratedMaterialBindings.hlsli";

void set_current_generated_bindings_path(const std::filesystem::path& path);

if (defaultGeneratedBindingsName == filePathInclude.filename())
{
    findFilePathInclude = m_currentGeneratedBindingsPath;
    fileExist = AshEngine::file_exists(findFilePathInclude.string().c_str());
}
```

- [ ] Thread that path through both DX12 and Vulkan preprocessing.

```cpp
ShaderItem shader_item{};
shader_item.sourceShaderPath = ci.pBaseShaderPath;
shader_item.userShaderPath = ci.pUserShaderPath;
shader_item.generatedBindingsPath = ci.pGeneratedBindingsPath;

if (item.generatedBindingsPath && *item.generatedBindingsPath != '\0')
{
    m_pDefaultIncluder.p->set_current_generated_bindings_path(item.generatedBindingsPath);
}
else
{
    m_pDefaultIncluder.p->set_current_generated_bindings_path(std::filesystem::path{});
}
```

- [ ] Create the Phase 1 `Surface` domain ABI file.

```hlsl
struct SurfaceVertexMainNode
{
    float3 world_position_offset;
};

struct SurfacePixelMainNode
{
    float3 base_color;
    float opacity;
    float opacity_mask;
    float3 normal_ts;
    float metallic;
    float roughness;
    float3 emissive;
    float ambient_occlusion;
    float pixel_depth_offset;
};

inline SurfacePixelMainNode AshInitializeSurfacePixelMainNode()
{
    SurfacePixelMainNode node;
    node.base_color = float3(1.0, 1.0, 1.0);
    node.opacity = 1.0;
    node.opacity_mask = 1.0;
    node.normal_ts = float3(0.0, 0.0, 1.0);
    node.metallic = 0.0;
    node.roughness = 0.5;
    node.emissive = float3(0.0, 0.0, 0.0);
    node.ambient_occlusion = 1.0;
    node.pixel_depth_offset = 0.0;
    return node;
}
```

- [ ] Create host shaders that include both placeholders and call the material-owned `Calculate*` functions.

```hlsl
#include "../Domains/AshSurfaceDomain.hlsli"
#include "GeneratedMaterialBindings.hlsli"
#include "UserShader.hlsli"

VSOutput VSMain(VSInput input)
{
    AshVertexParameters params = BuildSurfaceStaticMeshVertexParameters(input);
    AshVertexMainNode node = AshInitializeSurfaceVertexMainNode();
    CalculateVertexMainNode(params, node);
    return BuildSurfaceStaticMeshVertexOutput(input, params, node);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    AshPixelParameters params = BuildSurfaceStaticMeshPixelParameters(input);
    AshPixelMainNode node = AshInitializeSurfacePixelMainNode();
    CalculatePixelMainNode(params, node);
    return EvaluateSurfaceStaticMeshBasePass(params, node);
}
```

- [ ] Run a focused Engine build after the shader-composition plumbing lands.

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
- host shaders can reference `UserShader.hlsli` and `GeneratedMaterialBindings.hlsli` through the existing DXC include-handler path
- legacy one-file graphics programs still compile unchanged

## Task 4: Implement Phase 1 Generated Bindings, Shader Maps, And V2 Proxy Binding

**Files:**
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderSourceBuilder.h`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderSourceBuilder.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialShaderMap.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Emit generated bindings/includes per material permutation under `product/caches/ShaderGenerated/Materials/`.

```cpp
struct MaterialGeneratedSourcePaths
{
    std::filesystem::path bindings_include_path{};
    uint64_t combined_source_hash = 0;
};

MaterialGeneratedSourcePaths MaterialShaderSourceBuilder::build_surface_staticmesh_source(
    const MaterialInterface& material,
    const MaterialUsageDesc& usage,
    const std::filesystem::path& output_root)
{
    const std::string usage_name = "SurfaceStaticMesh_" +
        std::to_string(static_cast<uint32_t>(usage.pass));
    const std::filesystem::path output_dir =
        output_root / std::to_string(material.get_compile_hash()) / usage_name;
    const std::filesystem::path bindings_path = output_dir / "Bindings.generated.hlsli";

    std::ostringstream bindings{};
    bindings << "#define ASH_DOMAIN_SURFACE 1\n";
    bindings << "#define ASH_ENGINE_FAMILY_SURFACE_STATIC_MESH 1\n";
    bindings << "#define ASH_PASS_BASE_PASS " << (usage.pass == PassFamily::BasePass ? 1 : 0) << "\n";
    bindings << "cbuffer AshMaterialParameters : register(b1)\n{\n";
    bindings << "    float4 BaseColorTint;\n";
    bindings << "    float RoughnessScale;\n";
    bindings << "};\n";
    bindings << "Texture2D<float4> BaseColorTex : register(t0);\n";
    bindings << "SamplerState WrapLinear : register(s0);\n";

    write_text_file_if_changed(bindings_path, bindings.str());
    return { bindings_path, hash_text(bindings.str()) };
}
```

- [ ] Let `MaterialShaderMap` cache immutable `MaterialResource`s by `compile_hash + usage + backend`.

```cpp
struct MaterialPermutationKey
{
    uint64_t compile_hash = 0;
    MaterialUsageDesc usage{};
    RHIBackend backend = RHIBackend::Unknown;
};

MaterialResource* MaterialShaderMap::find_or_create_resource(
    const MaterialInterface& material,
    const MaterialUsageDesc& usage,
    Renderer& renderer,
    std::string* out_error)
{
    const MaterialPermutationKey key{
        material.get_compile_hash(),
        usage,
        resolve_material_backend(renderer)
    };

    if (const auto found = m_resources.find(key); found != m_resources.end())
    {
        return found->second.get();
    }

    const MaterialGeneratedSourcePaths generated =
        m_source_builder.build_surface_staticmesh_source(material, usage, m_generated_source_root);
    const EngineShaderFamilyDesc* family_desc = m_family_registry.find(usage.family);
    if (!family_desc)
    {
        if (out_error) *out_error = "EngineShaderFamilyRegistry lookup failed.";
        return nullptr;
    }

    const std::string user_shader_path(material.get_material_shader_path());
    const std::string generated_bindings_path = generated.bindings_include_path.string();
    const std::string usage_macros = build_usage_macro_string(usage);

    GraphicsProgramDesc program_desc{};
    program_desc.base_shader_path = family_desc->resolve_host_shader_path(usage.pass);
    program_desc.user_shader_path = user_shader_path.c_str();
    program_desc.generated_bindings_path = generated_bindings_path.c_str();
    program_desc.vertex_entry = "VSMain";
    program_desc.fragment_entry = "PSMain";
    program_desc.shader_macro = usage_macros.c_str();
    program_desc.state = build_phase1_program_state(material.get_static_render_state());
    program_desc.name = "MaterialV2_SurfaceStaticMesh";
    program_desc.vertex_decl = get_mesh_vertex_decl();

    auto resource = std::make_unique<MaterialResource>();
    resource->usage = usage;
    resource->combined_source_hash = generated.combined_source_hash;
    resource->program = renderer.create_graphics_program(program_desc);
    if (!resource->program)
    {
        if (out_error) *out_error = "create_graphics_program failed.";
        return nullptr;
    }

    resource->binding_layout = reflect_material_binding_layout(*resource->program);
    resource->pass_relevance = build_surface_staticmesh_pass_relevance(material, usage);
    return m_resources.emplace(key, std::move(resource)).first->second.get();
}
```

- [ ] Split `MaterialRenderProxy` into a V1 compatibility branch and a V2 branch instead of trying to big-bang replace the existing PBR path.

```cpp
class ASH_API MaterialRenderProxy
{
public:
    const MaterialResource* get_surface_staticmesh_basepass_resource() const
    {
        return m_surface_staticmesh_basepass;
    }

    const MaterialResource* get_legacy_resource() const
    {
        return m_legacy_program ? &m_legacy_resource : nullptr;
    }

private:
    std::shared_ptr<const MaterialInterface> m_material = nullptr;

    // legacy V1 state
    MaterialResource m_legacy_resource{};
    std::unique_ptr<GraphicsProgram> m_legacy_program = nullptr;

    // V2 state
    MaterialSystem* m_material_system = nullptr;
    MaterialShaderMap* m_shader_map = nullptr;
    MaterialBindingSnapshot m_binding_snapshot{};
    MaterialResource* m_surface_staticmesh_basepass = nullptr;
    MaterialResource* m_surface_staticmesh_depthonly = nullptr;
    uint64_t m_runtime_binding_version = 0;
};
```

- [ ] Build runtime binding snapshots from resolved parameter/resource overrides only, and do not rebuild shader maps when only instance values change.

```cpp
bool MaterialRenderProxy::refresh_v2_bindings(RenderAssetManager& asset_manager)
{
    MaterialBindingSnapshot snapshot{};
    snapshot.version = ++m_runtime_binding_version;
    snapshot.packed_parameter_data = pack_material_parameter_block(*m_material);
    snapshot.textures["BaseColorTex"] = resolve_material_texture(asset_manager, *m_material, "BaseColorTex");
    snapshot.samplers["WrapLinear"] = resolve_material_sampler(asset_manager, *m_material, "WrapLinear");
    m_binding_snapshot = std::move(snapshot);
    return bind_v2_program_resources();
}
```

- [ ] Keep the V1 path untouched for generated imported materials and existing built-ins. V2 should activate only when `material.is_v2_material()` is true.

```cpp
bool MaterialRenderProxy::ensure_program(Renderer& renderer)
{
    if (!m_material || !m_material->is_v2_material())
    {
        return ensure_legacy_program(renderer);
    }
    return ensure_v2_resources(renderer);
}
```

- [ ] Run an `Engine + Sandbox` build after the V2 proxy/shader-map path lands.

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

- `Engine` and `Sandbox` build successfully
- V2 materials compile through `MaterialShaderMap`
- changing only instance parameter/resource overrides refreshes bindings but does not force shader recompilation
- legacy generated/imported materials still use the current fixed-PBR compatibility path

## Task 5: Route `Surface.StaticMesh` Through V2 And Add Explicit Runtime Smoke Assets

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\Shaders\MaterialV2\M_V2_DebugSurface.hlsl`
- Create: `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\M_V2_DebugSurface.AshMat`
- Create: `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_DebugSurface_Tint.AshMat`

- [ ] Add one explicit test material shader that exercises the Phase 1 contract without requiring the future material editor.

```hlsl
void CalculateVertexMainNode(in AshVertexParameters params, inout AshVertexMainNode node)
{
    node.world_position_offset = float3(0.0, 0.0, 0.0);
}

void CalculatePixelMainNode(in AshPixelParameters params, inout AshPixelMainNode node)
{
    float4 albedo = BaseColorTex.Sample(WrapLinear, params.uv0);
    node.base_color = albedo.rgb * BaseColorTint.rgb;
    node.opacity = albedo.a * BaseColorTint.a;
    node.opacity_mask = node.opacity;
    node.normal_ts = float3(0.0, 0.0, 1.0);
    node.metallic = 0.0;
    node.roughness = RoughnessScale;
    node.emissive = 0.0.xxx;
    node.ambient_occlusion = 1.0;
}
```

- [ ] Add hand-authored `.AshMat` assets for a base material and a runtime-only instance override.

```json
{
  "version": 2,
  "class": "Material",
  "name": "M_V2_DebugSurface",
  "domain": "Surface",
  "materialShader": "project/src/sandbox/Shaders/MaterialV2/M_V2_DebugSurface.hlsl",
  "requiredCapabilities": [],
  "staticSwitches": {},
  "renderState": {
    "blendMode": "Opaque",
    "twoSided": false,
    "cullMode": "Back",
    "depthWrite": true,
    "depthTest": "LessEqual",
    "alphaCutoff": 0.5
  },
  "samplers": {
    "WrapLinear": {
      "addressU": "Repeat",
      "addressV": "Repeat",
      "addressW": "Repeat",
      "minFilter": "Linear",
      "magFilter": "Linear",
      "mipFilter": "Linear"
    }
  },
  "parameters": {
    "BaseColorTint": { "type": "float4", "value": [1.0, 1.0, 1.0, 1.0] },
    "RoughnessScale": { "type": "float", "value": 0.65 }
  },
  "resources": {
    "BaseColorTex": {
      "type": "Texture2D",
      "path": "models/gltfs/DamagedHelmet/glTF/Default_albedo.jpg",
      "sampler": "WrapLinear",
      "colorSpace": "sRGB"
    }
  }
}
```

- [ ] Teach `SceneRenderer` to consume the V2 `Surface.StaticMesh` base-pass program when present, while continuing to skip `Transparent` in Phase 1.

```cpp
const MaterialResource* material_resource = section.material_proxy->get_surface_staticmesh_basepass_resource();
if (!material_resource)
{
    material_resource = section.material_proxy->get_legacy_resource();
}

if (!material_resource ||
    material_resource->pass_relevance.domain != MaterialDomain::Surface ||
    material_resource->pass_relevance.is_transparent)
{
    continue;
}

draw_desc.program = material_resource->program.get();
```

- [ ] Inject one explicit V2 material override into the standard `Sandbox` scene so validation definitely covers the new path.

```cpp
for (AshEngine::Entity entity : out_snapshot.scene.get_entities_with_component(AshEngine::SceneComponentType::Mesh))
{
    AshEngine::MeshComponent mesh = entity.get_mesh_component();
    if (mesh.asset_path.empty())
    {
        continue;
    }

    if (mesh.material_overrides.empty())
    {
        mesh.material_overrides.push_back({
            0u,
            "materials/v2/MI_V2_DebugSurface_Tint.AshMat"
        });
        entity.set_mesh_component(mesh);
        HLogInfo("Sandbox standard scene injected V2 material override on entity {}.", entity.get_id());
        break;
    }
}
```

- [ ] Keep the default Sponza scene otherwise unchanged so this step validates both:
  - legacy V1 imported/generated materials
  - one explicit V2 `.AshMat` override

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

- `Engine`, `Sandbox`, and `Editor` all build successfully
- the standard `Sandbox` scene still loads
- at least one static-mesh section in `Sandbox` now resolves through a hand-authored `.AshMat`
- old imported/generated materials still render through the compatibility path

## Task 6: Update Documentation And Run Full Phase 1 Validation

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`

- [ ] Update `EngineDeveloperGuide.md` to document the new Phase 1 material framework and the deliberate non-goals still left for later phases.

```md
- `.AshMat` is now the Phase 1 V2 material asset format
- Phase 1 officially supports only `MaterialDomain=Surface` + `EngineShaderFamily=Surface.StaticMesh`
- V2 shader compilation now uses:
  - host shader (`base_shader_path`)
  - material shader (`user_shader_path`)
  - generated bindings include (`generated_bindings_path`)
- `MaterialSystem` owns permutation validation/fallback and `MaterialShaderMap` owns immutable compiled resources
- `MaterialRenderProxy` now has two branches:
  - legacy V1 fixed-PBR compatibility
  - V2 shader-map + binding-snapshot path
- `Surface.Transparent`, `Surface.SkeletalMesh`, and `Decal` remain follow-on phases and are not considered implemented by this plan
```

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

- the full solution builds successfully

- [ ] Run the AshEngine validation loop for shared rendering changes.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\huyizhou\.codex\skills\ash-engine-validation-loop\scripts\run-validation-loop.ps1" `
  -Configuration Debug `
  -RunSeconds 25 `
  -CloseTimeoutSeconds 20
```

Expected:

- Premake regeneration succeeds
- rebuild succeeds
- `Sandbox` passes on Vulkan and DX12
- `Editor` passes on Vulkan and DX12
- no Vulkan validation errors, DX12 debug-layer failures, abnormal exits, or VMA leak output

- [ ] During validation, explicitly verify the Phase 1 runtime outcomes instead of only checking clean logs.

Checklist:

```md
- the standard Sandbox Sponza scene still renders primarily through legacy imported/generated materials
- the injected `MI_V2_DebugSurface_Tint.AshMat` override compiles and renders on at least one static-mesh section
- changing only instance tint/roughness values in `MI_V2_DebugSurface_Tint.AshMat` updates runtime bindings without forcing shader recompilation
- no `Transparent` or `Decal` path is accidentally treated as implemented
- no Editor code changes were required to keep scene-driven viewport rendering alive
```

## Coverage Check

- Spec sections 1-4: covered by Tasks 1 and 2 through `.AshMat` schema evolution, the three-axis Phase 1 usage model, and the new framework core.
- Spec sections 5-8: covered by Tasks 3 and 4 through the `Surface` domain contract, host/user/generated shader composition, compile hash, and shader-map/resource split.
- Spec sections 9-12: covered by Tasks 4 and 5 through `MaterialResource`, `MaterialBindingSnapshot`, `MaterialRenderProxy`, fallback material routing, and `SceneRenderer` integration.
- Spec section 13.1: covered directly by Tasks 1-6; this plan is the explicit “introduce V2 framework and get `Surface.StaticMesh` running” slice.
- Spec sections 13.2-13.3, 14.3 (`Surface.SkeletalMesh`, `Surface.Transparent`, `Decal`), and 15 follow-on scope: intentionally deferred to later plans because the current codebase does not yet have those runtime paths.

## Deferred Follow-On Plans

- **Phase 2A:** `Surface.SkeletalMesh`
  - add primitive family/runtime asset path
  - register `Surface.SkeletalMesh`
  - provide skeletal-specific parameter adapters while keeping the `Surface` ABI fixed
- **Phase 2B:** `Surface.Transparent`
  - add transparent queue/orchestration
  - register `TransparentForward`
  - validate sorting/depth behavior independently of opaque base pass
- **Phase 2C:** `Decal.Deferred`
  - introduce a real `DecalRenderer` / projector runtime path
  - add `Decal` domain ABI and deferred decal host shaders
  - add per-domain decal fallback handling
- **Phase 3:** V1 retirement and migration
  - map or upgrade legacy `.material / .mat`
  - remove the fixed `SceneSurfacePBR` compatibility branch only after Phase 2 is proven stable
