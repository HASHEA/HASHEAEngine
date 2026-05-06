# Material System V2 V1 Retirement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the V1 material system from AshEngine, hard-split V2 base-vs-instance assets into `.AshMat` and `.AshMatIns`, and migrate all static-mesh default, imported, generated, and fallback materials onto a V2-only `SurfacePBR` path.

**Architecture:** Build on top of the existing Phase 1 V2 `MaterialSystem / MaterialShaderMap / MaterialRenderProxy` path that already drives `Surface.StaticMesh`. First harden asset loading and naming rules, then introduce a built-in V2 `SurfacePBR` base material plus default instance, then migrate generated/imported materials and object-binding rules, and finally delete the legacy fixed-PBR runtime/render path entirely.

**Tech Stack:** C++17, `nlohmann::json`, existing `AssetDatabase`, `RenderAssetManager`, `MaterialSystem`, `MaterialShaderMap`, HLSL, Premake/MSBuild, Vulkan + DX12 validation loop.

---

## Execution Notes

- Work directly on `main`; the user explicitly rejected worktrees for this effort.
- The current workspace is already dirty with in-flight Phase 1 V2 changes. Treat this plan as an incremental migration on top of the current workspace and do not revert unrelated edits.
- Do not modify `project/src/editor`.
- This repo does not currently expose a focused unit-test harness for the material system. Use red/green smoke loops with hand-authored assets, focused builds, log assertions, and the full Vulkan/DX12 validation loop.
- V1 removal is intentionally breaking. Do not add compatibility shims for `.material`, `.mat`, `version = 1`, or `.AshMat` instances.

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
  - replace V1 built-in material path constants with V2-only constants
  - remove `MaterialFixedPBRSurfaceInputs`
  - add V2 suffix helpers and, if needed, a small enum for material file kind
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
  - delete `version = 1` parse/save branches
  - enforce `.AshMat` vs `.AshMatIns` hard validation on load/save
  - synthesize built-in `M_SurfacePBR.AshMat` and `MI_DefaultSurface.AshMatIns`
  - merge sampler definitions across parent + instance
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`
  - classify only `.ashmat` and `.ashmatins` as material assets
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
  - add helpers for “bindable instance material” validation
  - keep generated material key helpers aligned with `.AshMatIns`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
  - migrate all fallback paths to the V2 default instance
  - build generated/imported materials as V2 `MaterialInstance` objects parented to `M_SurfacePBR.AshMat`
  - reject base materials at object-binding entry points
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
  - delete legacy fixed-PBR state, textures, uniform structs, and program slots
  - keep only V2 resource-template, uniform-buffer, and binding-snapshot state
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
  - delete `SceneSurfacePBR`/legacy render path
  - make `ensure_program()` and related flow V2-only
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.cpp`
  - point the domain fallback to `MI_DefaultSurface.AshMatIns`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
  - remove the legacy fallback branch and consume only V2 material resources
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`
  - replace any old default material fallback path with the V2 default instance
- `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
  - rename the injected hand-authored instance asset to `.AshMatIns`
  - use this file for focused negative/positive smoke swaps during execution
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document V2-only asset rules, V1 retirement, `SurfacePBR`, and instance-only binding

### Existing files to delete

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`
  - legacy fixed-PBR host shader; remove once `MaterialRenderProxy` no longer references it

### New files to create

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Materials\M_SurfacePBR.hlsl`
  - engine-owned V2 material shader for imported/default surface PBR semantics
- `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_DebugSurface_Tint.AshMatIns`
  - renamed hand-authored instance asset for the existing V2 sample
- `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_SurfacePBR_SamplerOverride.AshMatIns`
  - focused regression asset to prove parent+instance sampler-definition merge

## Task 1: Hard-Split V2 Material Files Into `.AshMat` And `.AshMatIns`

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetDatabase.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_DebugSurface_Tint.AshMatIns`
- Delete: `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_DebugSurface_Tint.AshMat`

- [ ] **Step 1: Write the failing smoke setup by renaming the sample instance asset before loader support exists**

Rename the current sample instance and point Sandbox at the new suffix immediately:

```json
{
  "version": 2,
  "class": "MaterialInstance",
  "name": "MI_V2_DebugSurface_Tint",
  "parent": "materials/v2/M_V2_DebugSurface.AshMat",
  "overrides": {
    "parameters": {
      "BaseColorTint": [1.0, 0.45, 0.45, 1.0],
      "RoughnessScale": 0.25
    }
  }
}
```

Update the Sandbox override constant to the new file name:

```cpp
static constexpr const char* k_v2_debug_material_override_path =
    "materials/v2/MI_V2_DebugSurface_Tint.AshMatIns";
```

- [ ] **Step 2: Run the focused build and smoke loop to prove the current loader still fails**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m

$env:ASH_ENGINE_SMOKE_TEST_SECONDS='8'
& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe'

$log = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\logs\AshEngineLogFile_*.logfile' |
  Sort-Object LastWriteTime |
  Select-Object -Last 1
Select-String -Path $log.FullName -Pattern 'failed to load mesh material override|Unsupported material file|failed to open material'
```

Expected:

- `Engine` and `Sandbox` build
- the smoke run does **not** bind the renamed instance asset
- the log contains a material-load failure because `.AshMatIns` is not yet classified/validated

- [ ] **Step 3: Implement V2 suffix recognition and hard suffix/class validation**

Add a small helper in `Material.cpp`:

```cpp
enum class MaterialFileKind : uint8_t
{
    Unknown = 0,
    Material,
    MaterialInstance
};

static auto classify_material_file_path(const std::filesystem::path& path) -> MaterialFileKind
{
    const std::string ext = to_lower_copy(path.extension().string());
    if (ext == ".ashmat")
    {
        return MaterialFileKind::Material;
    }
    if (ext == ".ashmatins")
    {
        return MaterialFileKind::MaterialInstance;
    }
    return MaterialFileKind::Unknown;
}

static auto validate_material_file_kind(
    const std::filesystem::path& path,
    std::string_view class_name,
    std::string* out_error) -> bool
{
    const MaterialFileKind file_kind = classify_material_file_path(path);
    if (file_kind == MaterialFileKind::Material && class_name == "Material")
    {
        return true;
    }
    if (file_kind == MaterialFileKind::MaterialInstance && class_name == "MaterialInstance")
    {
        return true;
    }
    return make_error(out_error, "Material file suffix does not match JSON class.");
}
```

Call it on load immediately after reading `class_name`:

```cpp
const std::string class_name = root.value("class", std::string{});
ASH_PROCESS_ERROR(validate_material_file_kind(path, class_name, out_error));
```

Use the runtime object type for save-time validation:

```cpp
const MaterialFileKind file_kind = classify_material_file_path(path);
if (!material.is_material_instance() && file_kind != MaterialFileKind::Material)
{
    bResult = make_error(out_error, "Base material files must use .AshMat.");
    break;
}
if (material.is_material_instance() && file_kind != MaterialFileKind::MaterialInstance)
{
    bResult = make_error(out_error, "MaterialInstance files must use .AshMatIns.");
    break;
}
```

Update `AssetDatabase.cpp` classification:

```cpp
if (ext == ".ashmat" || ext == ".ashmatins")
{
    return AssetType::Material;
}
```

- [ ] **Step 4: Re-run the same smoke loop and verify the renamed instance asset now loads**

Run the same commands from Step 2.

Expected:

- the smoke run succeeds
- the latest engine log contains:

```text
MaterialRenderProxy: refreshed V2 bindings for material 'materials/v2/MI_V2_DebugSurface_Tint.AshMatIns'
SceneRenderer: drawing Surface.StaticMesh.BasePass with V2 material 'materials/v2/MI_V2_DebugSurface_Tint.AshMatIns'
```

## Task 2: Replace V1 Built-Ins With V2 `SurfacePBR` And Default Instance

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialSystem.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\ScenePresentationSubsystem.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\MaterialV2\Materials\M_SurfacePBR.hlsl`

- [ ] **Step 1: Write the failing smoke by flipping fallback constants to the new V2 built-in paths first**

Replace the old built-in constants in `Material.h` before adding their factories:

```cpp
inline constexpr const char* k_builtin_surface_pbr_material_path =
    "Engine/Materials/V2/M_SurfacePBR.AshMat";
inline constexpr const char* k_builtin_default_surface_material_path =
    "Engine/Materials/V2/MI_DefaultSurface.AshMatIns";
```

Also point the domain fallback and default material request sites at `k_builtin_default_surface_material_path`.

- [ ] **Step 2: Run a focused smoke and prove the current built-in factory cannot satisfy the new paths yet**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m

$env:ASH_ENGINE_SMOKE_TEST_SECONDS='8'
& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe'

$log = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\logs\AshEngineLogFile_*.logfile' |
  Sort-Object LastWriteTime |
  Select-Object -Last 1
Select-String -Path $log.FullName -Pattern 'falling back to|failed to load material|M_SurfacePBR|MI_DefaultSurface'
```

Expected:

- build succeeds
- the smoke fails to resolve the new V2 built-ins because `make_builtin_material(...)` still synthesizes the old paths

- [ ] **Step 3: Implement the built-in V2 base material, default instance, and `SurfacePBR` shader**

Create the engine-owned V2 material shader:

```hlsl
void CalculateVertexMainNode(in AshVertexParameters params, inout AshVertexMainNode node)
{
    node.world_position_offset = float3(0.0, 0.0, 0.0);
}

void CalculatePixelMainNode(in AshPixelParameters params, inout AshPixelMainNode node)
{
    const float4 base_sample = BaseColorTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 normal_sample = NormalTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 mr_sample = MetallicRoughnessTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 emissive_sample = EmissiveTexture.Sample(ASH_SurfacePBRSampler, params.uv0);

    node.base_color = base_sample.rgb * BaseColorFactor.rgb;
    node.opacity = base_sample.a * BaseColorFactor.a;
    node.opacity_mask = node.opacity;
    node.normal_ts = normalize(float3(normal_sample.xy * 2.0f - 1.0f, normal_sample.z * 2.0f - 1.0f));
    node.metallic = saturate(Metallic * mr_sample.b);
    node.roughness = saturate(Roughness * mr_sample.g);
    node.emissive = emissive_sample.rgb * EmissiveColor.rgb;
    node.ambient_occlusion = 1.0f;
}
```

Synthesize the built-in V2 base material in `Material.cpp`:

```cpp
static auto build_surface_pbr_v2_material() -> std::shared_ptr<MaterialInterface>
{
    auto material = std::make_shared<Material>();
    material->set_is_v2_material(true);
    material->set_name("M_SurfacePBR");
    material->set_asset_path(k_builtin_surface_pbr_material_path);
    material->set_domain(MaterialDomain::Surface);
    material->set_material_shader_path("project/src/engine/Shaders/MaterialV2/Materials/M_SurfacePBR.hlsl");
    material->set_static_render_state(MaterialStaticRenderStateDesc{});
    material->set_parameter_descs({
        MaterialParameterDesc{ "BaseColorFactor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(1.0f), 0.0f, {} },
        MaterialParameterDesc{ "Metallic", MaterialParameterType::Scalar, {}, {}, {}, {}, 0.0f, {} },
        MaterialParameterDesc{ "Roughness", MaterialParameterType::Scalar, {}, {}, {}, {}, 1.0f, {} },
        MaterialParameterDesc{ "EmissiveColor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(0.0f), 0.0f, {} }
    });
    material->set_sampler_definitions({
        MaterialSamplerDefinition{ "WrapLinear", "ASH_SurfacePBRSampler", RenderSamplerDesc{} }
    });
    material->set_resource_descs({
        MaterialResourceDesc{ "BaseColorTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB },
        MaterialResourceDesc{ "NormalTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
        MaterialResourceDesc{ "MetallicRoughnessTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
        MaterialResourceDesc{ "EmissiveTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB }
    });
    material->reset_change_version();
    return material;
}
```

Synthesize the default instance:

```cpp
static auto build_default_surface_v2_instance() -> std::shared_ptr<MaterialInterface>
{
    auto material_instance = std::make_shared<MaterialInstance>();
    material_instance->set_is_v2_material(true);
    material_instance->set_name("MI_DefaultSurface");
    material_instance->set_asset_path(k_builtin_default_surface_material_path);
    material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
    material_instance->set_parent(make_builtin_material(k_builtin_surface_pbr_material_path));
    material_instance->reset_change_version();
    return material_instance;
}
```

Route `make_builtin_material(...)` and `canonical_builtin_material_path(...)` to these new V2 paths and remove the old V1 built-in path handling.

- [ ] **Step 4: Re-run the focused smoke and verify the new built-ins resolve cleanly**

Run the same commands from Step 2.

Expected:

- the smoke resolves `Engine/Materials/V2/M_SurfacePBR.AshMat`
- the fallback path resolves `Engine/Materials/V2/MI_DefaultSurface.AshMatIns`
- imported or defaulted sections continue to draw through V2 resources instead of failing at material load

## Task 3: Merge Parent And Instance Sampler Definitions

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Create: `D:\workspace\AshEngine\HASHEAEngine\product\assets\materials\v2\MI_V2_SurfacePBR_SamplerOverride.AshMatIns`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] **Step 1: Write the failing sampler-inheritance smoke asset**

Create a focused regression asset whose local sampler list only overrides one sampler name, while the parent still supplies the default sampler used by the other resources:

```json
{
  "version": 2,
  "class": "MaterialInstance",
  "name": "MI_V2_SurfacePBR_SamplerOverride",
  "parent": "Engine/Materials/V2/M_SurfacePBR.AshMat",
  "samplers": {
    "ClampLinear": {
      "shader_sampler_name": "ASH_ClampSampler",
      "address_u": "ClampToEdge",
      "address_v": "ClampToEdge",
      "address_w": "ClampToEdge",
      "min_filter": "Linear",
      "mag_filter": "Linear",
      "mip_filter": "Linear"
    }
  },
  "overrides": {
    "resources": {
      "BaseColorTexture": {
        "path": "models/gltfs/DamagedHelmet/glTF/Default_albedo.jpg",
        "sampler": "ClampLinear"
      }
    }
  }
}
```

Temporarily point Sandbox at this asset:

```cpp
static constexpr const char* k_v2_debug_material_override_path =
    "materials/v2/MI_V2_SurfacePBR_SamplerOverride.AshMatIns";
```

- [ ] **Step 2: Run the focused smoke and confirm the current “local sampler list replaces parent list” rule breaks V2 binding**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m

$env:ASH_ENGINE_SMOKE_TEST_SECONDS='8'
& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe'

$log = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\logs\AshEngineLogFile_*.logfile' |
  Sort-Object LastWriteTime |
  Select-Object -Last 1
Select-String -Path $log.FullName -Pattern "references unknown sampler 'WrapLinear'|resolved to an empty sampler name"
```

Expected:

- the smoke run fails on V2 sampler resolution because the instance-local sampler list currently drops the parent’s default sampler definition

- [ ] **Step 3: Implement merged sampler-definition resolution**

Replace the current all-or-nothing logic in `MaterialInstance::get_sampler_definitions()` with a merged view:

```cpp
const std::vector<MaterialSamplerDefinition>& MaterialInstance::get_sampler_definitions() const
{
    if (m_sampler_definitions.empty())
    {
        static const std::vector<MaterialSamplerDefinition> k_empty_definitions{};
        return m_parent ? m_parent->get_sampler_definitions() : k_empty_definitions;
    }

    m_cached_merged_sampler_definitions.clear();
    if (m_parent)
    {
        m_cached_merged_sampler_definitions = m_parent->get_sampler_definitions();
    }

    for (const MaterialSamplerDefinition& local_definition : m_sampler_definitions)
    {
        auto found = std::find_if(
            m_cached_merged_sampler_definitions.begin(),
            m_cached_merged_sampler_definitions.end(),
            [&local_definition](const MaterialSamplerDefinition& existing)
            {
                return existing.name == local_definition.name;
            });
        if (found != m_cached_merged_sampler_definitions.end())
        {
            *found = local_definition;
        }
        else
        {
            m_cached_merged_sampler_definitions.push_back(local_definition);
        }
    }
    return m_cached_merged_sampler_definitions;
}
```

Add a mutable cache field to `MaterialInstance` and clear it on any sampler-definition mutation:

```cpp
mutable std::vector<MaterialSamplerDefinition> m_cached_merged_sampler_definitions{};
```

- [ ] **Step 4: Re-run the smoke, verify the inherited sampler survives, and restore Sandbox to the standard sample instance**

Run the same commands from Step 2.

Expected:

- no “unknown sampler `WrapLinear`” error
- the log contains a normal V2 binding refresh for `MI_V2_SurfacePBR_SamplerOverride.AshMatIns`

Then restore Sandbox to the standard instance asset:

```cpp
static constexpr const char* k_v2_debug_material_override_path =
    "materials/v2/MI_V2_DebugSurface_Tint.AshMatIns";
```

## Task 4: Make Generated And Object-Bound Materials Instance-Only

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\sandbox\App\SandboxStandardScene.cpp`

- [ ] **Step 1: Write the failing negative smoke by intentionally binding a base material to the mesh override path**

Temporarily switch the Sandbox override to the base material:

```cpp
static constexpr const char* k_v2_debug_material_override_path =
    "materials/v2/M_V2_DebugSurface.AshMat";
```

- [ ] **Step 2: Run the smoke and prove the current runtime still accepts a base material at an object-binding entry point**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m

$env:ASH_ENGINE_SMOKE_TEST_SECONDS='8'
& 'D:\workspace\AshEngine\HASHEAEngine\product\bin64\Debug-windows-x86_64\Sandbox.exe'

$log = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\logs\AshEngineLogFile_*.logfile' |
  Sort-Object LastWriteTime |
  Select-Object -Last 1
Select-String -Path $log.FullName -Pattern "drawing Surface.StaticMesh.BasePass with V2 material 'materials/v2/M_V2_DebugSurface.AshMat'"
```

Expected:

- the smoke still draws with the base material asset, which is the incorrect pre-fix behavior

- [ ] **Step 3: Implement bindable-instance validation, generated-instance naming, and V2-only default routing**

Add a small helper in `RenderAssetManager.cpp`:

```cpp
static auto is_bindable_material_instance(const std::shared_ptr<const MaterialInterface>& material) -> bool
{
    return material != nullptr && material->is_material_instance();
}
```

Use it at explicit object-binding entry points:

```cpp
if (std::shared_ptr<const MaterialInterface> override_material =
        request_material_asset_internal(material_override->material_path, false))
{
    if (is_bindable_material_instance(override_material))
    {
        resolved_section.material = override_material;
    }
    else
    {
        HLogError(
            "RenderAssetManager: material '{}' resolved to base material 'Material'. "
            "Only '.AshMatIns' material instances can be assigned directly to mesh sections. "
            "Keeping the section default material.",
            material_override->material_path);
    }
}
```

Update generated material naming:

```cpp
std::string RenderAssetManager::make_generated_material_key(const std::string& asset_path, uint32_t material_slot)
{
    return "__generated__/materials/" + normalize_asset_key(asset_path) +
        "#slot=" + std::to_string(material_slot) + ".AshMatIns";
}
```

Parent generated/imported materials to the new built-in base:

```cpp
std::shared_ptr<const MaterialInterface> parent_material =
    request_material_asset_internal(k_builtin_surface_pbr_material_path, false);
material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
material_instance->set_parent(parent_material);
```

Map only the V2 `SurfacePBR` override names:

```cpp
material_instance->set_vector_override("BaseColorFactor", material_slot_data.base_color_factor);
material_instance->set_scalar_override("Metallic", material_slot_data.metallic_factor);
material_instance->set_scalar_override("Roughness", material_slot_data.roughness_factor);
material_instance->set_vector_override("EmissiveColor", glm::vec4(material_slot_data.emissive_factor, 1.0f));
```

Do **not** write the old `OpacityMask` override.

Make every default/fallback path return `k_builtin_default_surface_material_path`, which now points to `MI_DefaultSurface.AshMatIns`.

- [ ] **Step 4: Re-run the negative smoke and verify the base material is rejected with a clear fallback**

Keep the temporary base-material injection in place and run the same commands from Step 2.

Expected:

- the log contains the new “only `.AshMatIns` instances can be assigned directly” error
- the log no longer shows `drawing ... 'materials/v2/M_V2_DebugSurface.AshMat'`
- the section falls back to the generated material or the V2 default instance

- [ ] **Step 5: Restore Sandbox to the proper `.AshMatIns` instance path and verify the normal happy path still works**

Restore:

```cpp
static constexpr const char* k_v2_debug_material_override_path =
    "materials/v2/MI_V2_DebugSurface_Tint.AshMatIns";
```

Run the same build + smoke commands again.

Expected:

- the log contains normal V2 binding refresh for `MI_V2_DebugSurface_Tint.AshMatIns`
- no object-binding rejection fires on the normal happy path

## Task 5: Delete The Legacy V1 Schema And Fixed-PBR Runtime Path

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\SceneRenderer.cpp`
- Delete: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`

- [ ] **Step 1: Write the failing structural check by enumerating the V1 symbols that still exist**

Run:

```powershell
Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\project\src' -Recurse -Include *.h,*.cpp,*.hlsl |
  Select-String -Pattern 'k_material_file_version_legacy|SceneSurfacePBR|MaterialFixedPBRSurfaceInputs|ensure_legacy_program|get_legacy_resource|\.material\b' |
  ForEach-Object { $_.Path + ':' + $_.LineNumber + ':' + $_.Line.Trim() }
```

Expected:

- hits appear in `Material.cpp`, `Material.h`, `MaterialRenderProxy.cpp`, `SceneRenderer.cpp`, and `SceneSurfacePBR.hlsl`

- [ ] **Step 2: Remove the V1 schema branches and simplify material serialization to V2-only**

Delete the legacy parse/save branches in `Material.cpp`. The load path should collapse to:

```cpp
const uint32_t version = root.value("version", 0u);
ASH_PROCESS_ERROR(version == k_material_file_version_v2);

const std::string class_name = root.value("class", std::string{});
ASH_PROCESS_ERROR(validate_material_file_kind(path, class_name, out_error));

if (class_name == "Material")
{
    auto material = std::make_shared<Material>();
    ASH_PROCESS_ERROR(parse_material_root_v2(root, *material, out_error));
    material->set_asset_path(path.lexically_normal());
    material->reset_change_version();
    out_material = std::move(material);
}
else if (class_name == "MaterialInstance")
{
    auto material_instance = std::make_shared<MaterialInstance>();
    ASH_PROCESS_ERROR(parse_material_instance_root_v2(root, *material_instance, out_error));
    material_instance->set_asset_path(path.lexically_normal());
    material_instance->reset_change_version();
    out_material = std::move(material_instance);
}
else
{
    bResult = make_error(out_error, "Material file class is missing or unsupported.");
    break;
}
```

Delete:

- `k_material_file_version_legacy`
- `parse_material_root_legacy(...)`
- `parse_material_instance_root_legacy(...)`
- legacy `.material/.mat` save shape
- `MaterialFixedPBRSurfaceInputs`

- [ ] **Step 3: Remove the legacy render path from `MaterialRenderProxy` and `SceneRenderer`**

Collapse `ensure_program(...)` to V2-only:

```cpp
bool MaterialRenderProxy::ensure_program(Renderer& renderer)
{
    ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
    ASH_PROCESS_ERROR(m_material != nullptr);
    ASH_PROCESS_ERROR(m_material->is_v2_material());
    ASH_PROCESS_ERROR(ensure_v2_resources(renderer));
    ASH_PROCESS_GUARD_RETURN_END(bResult, false);
}
```

Delete from `MaterialRenderProxy.h`:

- `m_program`
- `m_resource`
- `m_uniform_data`
- legacy texture/sampler fields
- legacy sampler-binding macros/state

Delete from `SceneRenderer.cpp` the legacy fallback:

```cpp
const MaterialResource* material_resource =
    section.material_proxy ? section.material_proxy->get_surface_staticmesh_basepass_resource() : nullptr;
if (!material_resource)
{
    continue;
}
```

Then delete `project/src/engine/Shaders/SceneSurfacePBR.hlsl`.

- [ ] **Step 4: Build `Engine;Sandbox;Editor`, rerun the structural search, and confirm the V1 symbols are gone**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /t:Engine;Sandbox;Editor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m

Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\project\src' -Recurse -Include *.h,*.cpp,*.hlsl |
  Select-String -Pattern 'k_material_file_version_legacy|SceneSurfacePBR|MaterialFixedPBRSurfaceInputs|ensure_legacy_program|get_legacy_resource|\.material\b' |
  ForEach-Object { $_.Path + ':' + $_.LineNumber + ':' + $_.Line.Trim() }
```

Expected:

- `Engine`, `Sandbox`, and `Editor` build
- the search returns no source hits for the deleted V1 runtime/schema symbols

## Task 6: Update Documentation And Run The Full Vulkan/DX12 Validation Gate

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`

- [ ] **Step 1: Update the engine documentation for the V2-only asset model**

Add or update sections in `EngineDeveloperGuide.md` to state:

```md
- V1 `.material` / `.mat` assets are no longer supported.
- V2 uses two hard-split suffixes:
  - `.AshMat` for `Material`
  - `.AshMatIns` for `MaterialInstance`
- Only `MaterialInstance(.AshMatIns)` may be assigned directly to mesh sections or overrides.
- `Engine/Materials/V2/M_SurfacePBR.AshMat` is the built-in base material for imported/default surface PBR semantics.
- `Engine/Materials/V2/MI_DefaultSurface.AshMatIns` is the built-in fallback instance.
- Generated/imported runtime materials now use `__generated__/materials/... .AshMatIns`.
- `SceneSurfacePBR` and the V1 fixed-PBR runtime compatibility path were removed.
```

- [ ] **Step 2: Build the full solution once before the full runtime matrix**

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

- [ ] **Step 3: Run the full AshEngine validation loop**

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
- no Vulkan validation errors, DX12 debugger-side failures, abnormal exits, or VMA leak output

- [ ] **Step 4: Inspect the validation report and runtime logs for the key V2-only assertions**

Run:

```powershell
$summary = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\test-reports\validation-loop' -Directory |
  Sort-Object LastWriteTime |
  Select-Object -Last 1
Get-Content -Raw (Join-Path $summary.FullName 'summary.json')

$logs = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\logs\AshEngineLogFile_*.logfile' |
  Sort-Object LastWriteTime |
  Select-Object -Last 4
Select-String -Path $logs.FullName -Pattern `
  'MaterialRenderProxy: refreshed V2 bindings', `
  'SceneRenderer: drawing Surface.StaticMesh.BasePass with V2 material', `
  'Only ''.AshMatIns'' material instances can be assigned directly', `
  '__generated__/materials/.*\.AshMatIns'
```

Expected:

- positive V2 binding logs for `.AshMatIns` assets
- generated material debug names use `.AshMatIns`
- no V1 path or `.material` debug names appear
- any intentional base-material negative smoke during development has a clear rejection log

## Coverage Check

- Spec sections 4 and 5 are covered by Task 1 and Task 4 through hard suffix validation and instance-only object binding.
- Spec sections 6 and 7 are covered by Task 2 through `M_SurfacePBR.AshMat`, `MI_DefaultSurface.AshMatIns`, and the new engine-owned material shader.
- Spec section 8 is covered by Task 4 through generated/imported V2 instances and the `.AshMatIns` virtual path.
- Spec section 9 is covered by Task 3 through parent+instance sampler-definition merge.
- Spec section 10 is covered by Task 5 through explicit V1 code/resource deletion.
- Spec sections 11 and 12 are covered by Task 6 through asset renames, docs, and the full Vulkan/DX12 validation gate.

## Plan Self-Review

- **Spec coverage:** every approved requirement in the spec is mapped to at least one task above; there is no remaining V1 compatibility task.
- **Placeholder scan:** no `TODO`, `TBD`, “implement later”, or “write tests for the above” placeholders remain.
- **Type consistency:** the plan consistently uses:
  - `M_SurfacePBR.AshMat`
  - `MI_DefaultSurface.AshMatIns`
  - `Material`
  - `MaterialInstance`
  - `.AshMatIns` for generated/runtime-bindable instances
