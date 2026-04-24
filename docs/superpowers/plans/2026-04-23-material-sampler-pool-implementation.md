# Material Sampler Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement engine-side per-texture sampler definitions plus a process-wide sampler pool so materials render with the same wrap/filter semantics on Vulkan and DX12 without touching `project/src/editor`.

**Architecture:** Extend `Function/Render/Material.*` so every texture parameter resolves to a `MaterialTextureBinding` carrying `texture_path + optional sampler`. Thread that binding through imported `MaterialSlot`, generated materials, `RenderAssetManager`, and `MaterialRenderProxy`, then add a high-level `RenderSampler` wrapper and pooled sampler creation path that map down to backend `SamplerCreation` objects. Keep legacy `RenderSamplerState::Default` only for compatibility, but normalize its meaning to `Repeat + Linear min/mag/mip` on both backends while material rendering moves to explicit sampler-object bindings.

**Tech Stack:** C++17, `nlohmann::json`, `tinygltf`, existing `Renderer` / `RenderDevice` / `GraphicsContext` abstractions, HLSL, Vulkan + DX12 validation loop.

**Execution Notes:** The worktree is already dirty; do not revert unrelated user changes. Do not edit `project/src/editor`. `docs/EngineDeveloperGuide.md` already has local edits, so merge documentation updates carefully instead of replacing sections wholesale. Final acceptance must use the AshEngine validation-loop script rather than ad-hoc manual launches.

---

## File Structure

### Existing files to modify

- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
  - add shared sampler enums/descriptors, `MaterialTextureBinding`, and texture-parameter API changes
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`
  - implement material JSON compatibility for sampler objects and texture-binding overrides
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
  - replace imported material-slot texture-path strings with `MaterialTextureBinding`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
  - import glTF sampler state, keep OBJ/FBX on default sampler, and preserve runtime material-slot bindings
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
  - add global sampler-pool API and ownership
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`
  - implement pooled sampler lookup/creation/default fallback and generated-material binding propagation
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
  - add `RenderSampler`, shared sampler descriptors, and object-based sampler binding APIs
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
  - implement `RenderSampler`, descriptor-to-RHI mapping, and explicit sampler-object binding consumption
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
  - forward `create_sampler(...)`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
  - forward `create_sampler(...)`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
  - add per-texture sampler members
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
  - resolve texture bindings into `TextureAsset + RenderSampler` pairs and bind them into the graphics program
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\GraphicsContext.h`
  - add explicit sampler creation API beside the legacy enum-based cache accessor
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Context.h`
  - declare explicit sampler creation override
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Context.cpp`
  - implement explicit sampler creation and normalize legacy default-sampler semantics
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Sampler.h`
  - own sampler debug-name storage instead of relying on external pointer lifetime
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Sampler.cpp`
  - persist debug-name storage during initialization
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.h`
  - declare explicit sampler creation override and rename `create_sampler(const AshSamplerState&)` to `create_cached_state_sampler(const AshSamplerState&)`
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.cpp`
  - implement explicit sampler creation and normalize legacy default-sampler semantics
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanSampler.h`
  - own sampler and sampler-view debug-name storage
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanSampler.cpp`
  - persist debug-name storage during sampler construction
- `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`
  - switch from one shared sampler to per-texture sampler bindings
- `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`
  - document material texture bindings, sampler pooling, default semantics, and validation expectations

## Task 1: Add Shared Sampler Types And Texture-Binding Serialization To Material Assets

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Material.cpp`

- [ ] Add backend-agnostic sampler enums, descriptor hashing, and the texture-binding carrier in `Material.h`.

```cpp
enum class RenderSamplerAddressMode : uint8_t
{
    Repeat = 0,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge
};

enum class RenderSamplerFilter : uint8_t
{
    Nearest = 0,
    Linear
};

struct ASH_API RenderSamplerDesc
{
    RenderSamplerAddressMode address_u = RenderSamplerAddressMode::Repeat;
    RenderSamplerAddressMode address_v = RenderSamplerAddressMode::Repeat;
    RenderSamplerAddressMode address_w = RenderSamplerAddressMode::Repeat;
    RenderSamplerFilter min_filter = RenderSamplerFilter::Linear;
    RenderSamplerFilter mag_filter = RenderSamplerFilter::Linear;
    RenderSamplerFilter mip_filter = RenderSamplerFilter::Linear;

    bool operator==(const RenderSamplerDesc& rhs) const
    {
        return address_u == rhs.address_u &&
            address_v == rhs.address_v &&
            address_w == rhs.address_w &&
            min_filter == rhs.min_filter &&
            mag_filter == rhs.mag_filter &&
            mip_filter == rhs.mip_filter;
    }
};

struct ASH_API RenderSamplerDescHash
{
    size_t operator()(const RenderSamplerDesc& desc) const noexcept
    {
        size_t hash_value = static_cast<size_t>(desc.address_u);
        auto mix = [&hash_value](uint8_t value)
        {
            hash_value ^= static_cast<size_t>(value) + 0x9e3779b9u + (hash_value << 6) + (hash_value >> 2);
        };

        mix(static_cast<uint8_t>(desc.address_v));
        mix(static_cast<uint8_t>(desc.address_w));
        mix(static_cast<uint8_t>(desc.min_filter));
        mix(static_cast<uint8_t>(desc.mag_filter));
        mix(static_cast<uint8_t>(desc.mip_filter));
        return hash_value;
    }
};

struct ASH_API MaterialTextureBinding
{
    std::string texture_path{};
    bool has_explicit_sampler = false;
    RenderSamplerDesc sampler{};
};
```

- [ ] Replace string-only texture defaults and overrides with `MaterialTextureBinding` so the texture and sampler travel together.

```cpp
struct ASH_API MaterialParameterDesc
{
    std::string name{};
    MaterialParameterType type = MaterialParameterType::Scalar;
    std::string group{};
    std::string semantic{};
    std::string texture_usage{};
    glm::vec4 default_vector{ 0.0f };
    float default_scalar = 0.0f;
    MaterialTextureBinding default_texture{};
};

class ASH_API MaterialInterface
{
public:
    virtual bool try_get_texture_parameter(const std::string& name, MaterialTextureBinding& out_binding) const = 0;
};

class ASH_API MaterialInstance final : public MaterialInterface
{
public:
    void set_texture_override(const std::string& name, MaterialTextureBinding value);
    const std::unordered_map<std::string, MaterialTextureBinding>& get_texture_overrides() const;

private:
    std::unordered_map<std::string, MaterialTextureBinding> m_texture_overrides{};
};
```

- [ ] Add JSON helpers in `Material.cpp` that read the new object form and keep old string values loadable as “path only, default sampler”.

```cpp
static bool parse_material_texture_binding(const json& value, MaterialTextureBinding& out_binding, std::string* out_error)
{
    out_binding = MaterialTextureBinding{};
    if (value.is_string())
    {
        out_binding.texture_path = value.get<std::string>();
        out_binding.has_explicit_sampler = false;
        return true;
    }

    if (!value.is_object())
    {
        return make_error(out_error, "Texture material parameter default must be a string or object.");
    }

    out_binding.texture_path = value.value("path", std::string{});
    if (const auto sampler_it = value.find("sampler"); sampler_it != value.end())
    {
        out_binding.has_explicit_sampler = true;
        ASH_PROCESS_ERROR(parse_render_sampler_desc(*sampler_it, out_binding.sampler, out_error));
    }
    return clear_error(out_error), true;
}

static json material_texture_binding_to_json(const MaterialTextureBinding& binding)
{
    json result{};
    result["path"] = binding.texture_path;
    if (binding.has_explicit_sampler)
    {
        result["sampler"] = render_sampler_desc_to_json(binding.sampler);
    }
    return result;
}
```

- [ ] Update the material/root-instance parsing and saving branches so defaults and overrides use `MaterialTextureBinding`, while unsupported sampler JSON still hard-fails the material load.

```cpp
case MaterialParameterType::Texture:
    if (!parse_material_texture_binding(default_value, out_desc.default_texture, out_error))
    {
        return false;
    }
    break;

for (auto it = overrides_json.begin(); it != overrides_json.end(); ++it)
{
    if (it.value().is_number())
    {
        material_instance.set_scalar_override(it.key(), it.value().get<float>());
        continue;
    }
    if (it.value().is_array() && (it.value().size() == 3 || it.value().size() == 4))
    {
        material_instance.set_vector_override(it.key(), from_json_vec4(it.value(), glm::vec4(0.0f)));
        continue;
    }

    MaterialTextureBinding texture_override{};
    if (!parse_material_texture_binding(it.value(), texture_override, out_error))
    {
        return false;
    }
    material_instance.set_texture_override(it.key(), std::move(texture_override));
}
```

- [ ] Update built-in material defaults so every texture parameter starts with an empty `MaterialTextureBinding{}` and therefore resolves to the shared default repeat sampler when no explicit sampler is supplied.

```cpp
material->set_parameter_descs({
    MaterialParameterDesc{ "BaseColorTexture", MaterialParameterType::Texture, "Surface", "BaseColorTexture", "Color", {}, 0.0f, MaterialTextureBinding{} },
    MaterialParameterDesc{ "NormalTexture", MaterialParameterType::Texture, "Surface", "NormalTexture", "Normal", {}, 0.0f, MaterialTextureBinding{} },
    MaterialParameterDesc{ "MetallicRoughnessTexture", MaterialParameterType::Texture, "Surface", "MetallicRoughnessTexture", "Linear", {}, 0.0f, MaterialTextureBinding{} },
    MaterialParameterDesc{ "EmissiveTexture", MaterialParameterType::Texture, "Surface", "EmissiveTexture", "Color", {}, 0.0f, MaterialTextureBinding{} },
});
```

- [ ] Run a focused Engine build to catch header/API fallout before touching the import/render path.

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
- old `.material` string texture defaults still load
- new object-form texture defaults serialize back out with an embedded `sampler` object only when explicitly present

## Task 2: Preserve Sampler Definitions In Imported Material Slots And Generated Materials

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Asset\AssetData.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Replace `MaterialSlot` texture-path strings with `MaterialTextureBinding` fields so imported CPU-side material metadata can preserve sampler intent.

```cpp
struct MaterialSlot
{
    std::string name{};
    glm::vec4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec3 emissive_factor{ 0.0f, 0.0f, 0.0f };
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;
    MaterialTextureBinding base_color_texture{};
    MaterialTextureBinding normal_texture{};
    MaterialTextureBinding metallic_roughness_texture{};
    MaterialTextureBinding emissive_texture{};
};
```

- [ ] Add glTF sampler-mapping helpers in `AssetData.cpp` that convert `wrapS / wrapT / minFilter / magFilter` into `RenderSamplerDesc` and leave `has_explicit_sampler = false` when the glTF texture omits a sampler.

```cpp
static RenderSamplerAddressMode map_gltf_wrap_mode(int wrap_mode)
{
    switch (wrap_mode)
    {
    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
        return RenderSamplerAddressMode::ClampToEdge;
    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
        return RenderSamplerAddressMode::MirroredRepeat;
    case TINYGLTF_TEXTURE_WRAP_REPEAT:
    default:
        return RenderSamplerAddressMode::Repeat;
    }
}

static void map_gltf_min_filter(int min_filter, RenderSamplerFilter& out_min_filter, RenderSamplerFilter& out_mip_filter)
{
    switch (min_filter)
    {
    case TINYGLTF_TEXTURE_FILTER_NEAREST:
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        out_min_filter = RenderSamplerFilter::Nearest;
        out_mip_filter = RenderSamplerFilter::Nearest;
        break;
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
        out_min_filter = RenderSamplerFilter::Nearest;
        out_mip_filter = RenderSamplerFilter::Linear;
        break;
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        out_min_filter = RenderSamplerFilter::Linear;
        out_mip_filter = RenderSamplerFilter::Nearest;
        break;
    case TINYGLTF_TEXTURE_FILTER_LINEAR:
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    default:
        out_min_filter = RenderSamplerFilter::Linear;
        out_mip_filter = RenderSamplerFilter::Linear;
        break;
    }
}

static MaterialTextureBinding get_gltf_texture_binding(const tinygltf::Model& model, const std::filesystem::path& asset_path, int texture_index)
{
    MaterialTextureBinding binding{};
    binding.texture_path = get_gltf_texture_path(model, asset_path, texture_index);
    if (texture_index < 0 || static_cast<size_t>(texture_index) >= model.textures.size())
    {
        return binding;
    }

    const tinygltf::Texture& texture = model.textures[static_cast<size_t>(texture_index)];
    if (texture.sampler < 0 || static_cast<size_t>(texture.sampler) >= model.samplers.size())
    {
        return binding;
    }

    const tinygltf::Sampler& sampler = model.samplers[static_cast<size_t>(texture.sampler)];
    binding.has_explicit_sampler = true;
    binding.sampler.address_u = map_gltf_wrap_mode(sampler.wrapS);
    binding.sampler.address_v = map_gltf_wrap_mode(sampler.wrapT);
    binding.sampler.address_w = RenderSamplerAddressMode::Repeat;
    binding.sampler.mag_filter = sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ? RenderSamplerFilter::Nearest : RenderSamplerFilter::Linear;
    map_gltf_min_filter(sampler.minFilter, binding.sampler.min_filter, binding.sampler.mip_filter);
    return binding;
}
```

- [ ] Update the glTF import path to use `get_gltf_texture_binding(...)`, and keep OBJ / FBX imports on “path only, default sampler” semantics.

```cpp
slot.base_color_texture = get_gltf_texture_binding(gltf_model, path, material.pbrMetallicRoughness.baseColorTexture.index);
slot.normal_texture = get_gltf_texture_binding(gltf_model, path, material.normalTexture.index);
slot.metallic_roughness_texture = get_gltf_texture_binding(gltf_model, path, material.pbrMetallicRoughness.metallicRoughnessTexture.index);
slot.emissive_texture = get_gltf_texture_binding(gltf_model, path, material.emissiveTexture.index);

slot.base_color_texture.texture_path = resolve_embedded_resource_path(path, material.diffuse_texname);
slot.base_color_texture.has_explicit_sampler = false;
slot.normal_texture.texture_path = resolve_embedded_resource_path(path, material.normal_texname);
slot.normal_texture.has_explicit_sampler = false;
```

- [ ] Update generated-material creation so `RenderAssetManager::request_generated_material_asset(...)` copies the entire texture binding into the instance override, not just the path string.

```cpp
if (!material_slot_data.base_color_texture.texture_path.empty())
{
    material_instance->set_texture_override("BaseColorTexture", material_slot_data.base_color_texture);
}
if (!material_slot_data.normal_texture.texture_path.empty())
{
    material_instance->set_texture_override("NormalTexture", material_slot_data.normal_texture);
}
if (!material_slot_data.metallic_roughness_texture.texture_path.empty())
{
    material_instance->set_texture_override("MetallicRoughnessTexture", material_slot_data.metallic_roughness_texture);
}
if (!material_slot_data.emissive_texture.texture_path.empty())
{
    material_instance->set_texture_override("EmissiveTexture", material_slot_data.emissive_texture);
}
```

- [ ] Run a focused Engine build again after the importer/model metadata signatures change.

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
- generated material instances now retain both imported texture paths and sampler definitions
- OBJ / FBX imports still compile and resolve through the default repeat sampler path

## Task 3: Add High-Level RenderSampler Objects And Explicit Sampler Creation Above The RHI

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderDevice.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\Renderer.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\GraphicsContext.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Context.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Context.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Sampler.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Sampler.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanSampler.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanSampler.cpp`

- [ ] Add a high-level `RenderSampler` wrapper and creation API next to the other public render resources.

```cpp
class ASH_API RenderSampler
{
public:
    class Impl;

public:
    RenderSampler();
    ~RenderSampler();

public:
    const RenderSamplerDesc& get_desc() const;

private:
    std::shared_ptr<Impl> m_impl;

private:
    explicit RenderSampler(std::shared_ptr<Impl> impl);
    friend class GraphicsProgram;
    friend class ComputeProgram;
    friend class RenderDevice;
};

class ASH_API RenderDevice
{
public:
    std::shared_ptr<RenderSampler> create_sampler(const RenderSamplerDesc& desc);
};
```

- [ ] Add explicit sampler-object overloads for `GraphicsProgram` and `ComputeProgram`, and store them separately from the legacy enum sampler path inside `ProgramBindingState`.

```cpp
struct ProgramBindingState
{
    std::unordered_map<std::string, std::shared_ptr<RenderSampler::Impl>> sampler_objects;
    std::unordered_map<std::string, std::vector<std::shared_ptr<RenderSampler::Impl>>> sampler_object_arrays;
    std::unordered_map<std::string, RenderSamplerState> sampler_states;
    std::unordered_map<std::string, std::vector<RenderSamplerState>> sampler_state_arrays;
};

bool GraphicsProgram::set_sampler(const char* name, const std::shared_ptr<RenderSampler>& sampler);
bool GraphicsProgram::set_sampler_array(const char* name, const std::vector<std::shared_ptr<RenderSampler>>& samplers);
bool ComputeProgram::set_sampler(const char* name, const std::shared_ptr<RenderSampler>& sampler);
bool ComputeProgram::set_sampler_array(const char* name, const std::vector<std::shared_ptr<RenderSampler>>& samplers);
```

- [ ] Implement descriptor-to-RHI conversion in `RenderDevice.cpp`, add `GraphicsContext::create_sampler(const SamplerCreation&)`, and bind explicit sampler objects straight through to `IRenderProgramBinder`.

```cpp
static RHI::AshSamplerAddressMode to_rhi_sampler_address_mode(RenderSamplerAddressMode mode)
{
    switch (mode)
    {
    case RenderSamplerAddressMode::MirroredRepeat:
        return RHI::ASH_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RenderSamplerAddressMode::ClampToEdge:
        return RHI::ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case RenderSamplerAddressMode::ClampToBorder:
        return RHI::ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case RenderSamplerAddressMode::MirrorClampToEdge:
        return RHI::ASH_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case RenderSamplerAddressMode::Repeat:
    default:
        return RHI::ASH_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static RHI::AshFilter to_rhi_filter(RenderSamplerFilter filter)
{
    return filter == RenderSamplerFilter::Nearest ? RHI::ASH_FILTER_NEAREST : RHI::ASH_FILTER_LINEAR;
}

std::shared_ptr<RenderSampler> RenderDevice::create_sampler(const RenderSamplerDesc& desc)
{
    RHI::SamplerCreation creation{};
    creation.address_mode_u = to_rhi_sampler_address_mode(desc.address_u);
    creation.address_mode_v = to_rhi_sampler_address_mode(desc.address_v);
    creation.address_mode_w = to_rhi_sampler_address_mode(desc.address_w);
    creation.minFilter = to_rhi_filter(desc.min_filter);
    creation.magFilter = to_rhi_filter(desc.mag_filter);
    creation.mipFilter = to_rhi_filter(desc.mip_filter);

    std::shared_ptr<RHI::Sampler> sampler = m_impl->graphics_context->create_sampler(creation);
    ASH_PROCESS_ERROR(sampler != nullptr);

    auto impl = std::make_shared<RenderSampler::Impl>();
    impl->desc = desc;
    impl->sampler = std::move(sampler);
    return std::shared_ptr<RenderSampler>(new RenderSampler(std::move(impl)));
}

for (const auto& [name, sampler_impl] : bindings.sampler_objects)
{
    if (!sampler_impl || !sampler_impl->sampler)
    {
        bindings_valid = false;
        break;
    }
    binder.add_bind_sampler(name.c_str(), sampler_impl->sampler);
}
```

- [ ] Implement explicit sampler creation in both backends and fix debug-name lifetime by owning the name string inside the sampler objects instead of storing raw external pointers.

```cpp
class VulkanContext : public GraphicsContext
{
private:
    std::shared_ptr<Sampler> create_cached_state_sampler(const AshSamplerState& ss);

public:
    std::shared_ptr<Sampler> create_sampler(const SamplerCreation& ci) override;
};

class DX12Sampler : public Sampler
{
private:
    std::string m_name{};
    SamplerCreation m_creation{};
};

bool DX12Sampler::init(const SamplerCreation& ci, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
{
    m_creation = ci;
    m_name = ci.name ? ci.name : "DX12Sampler";
    m_creation.name = m_name.c_str();
    m_descriptorHandle = heapMgr->cpuSampler.allocate();
    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = ash_to_d3d12_filter(ci.minFilter, ci.magFilter, ci.mipFilter, ci.enable_anisotropy, ci.reductionMode, ci.enable_compare);
    samplerDesc.AddressU = ash_to_d3d12_address_mode(ci.address_mode_u);
    samplerDesc.AddressV = ash_to_d3d12_address_mode(ci.address_mode_v);
    samplerDesc.AddressW = ash_to_d3d12_address_mode(ci.address_mode_w);
    device->CreateSampler(&samplerDesc, m_descriptorHandle.cpuHandle);
    return true;
}

class VulkanSampler : public Sampler
{
private:
    std::string m_name{};
};

class VulkanSamplerView : public SamplerView
{
private:
    std::string m_name{};
};

VulkanSampler::VulkanSampler(const SamplerCreation& ci)
{
    m_name = ci.name ? ci.name : "VulkanSampler";
    VulkanContext::set_resource_name(VK_OBJECT_TYPE_SAMPLER, static_cast<uint64_t>(vkSampler), m_name.c_str());
}

VulkanSamplerView::VulkanSamplerView(const char* name, std::shared_ptr<Sampler> parent)
    : m_name(name ? name : "VulkanSamplerView")
{
    parentSampler = parent;
}
```

- [ ] Run a focused Engine build before moving the higher-level material proxy over to the new sampler-object APIs.

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
- explicit `RenderSampler` objects can be created without using `RenderSamplerState`
- backend sampler objects no longer depend on caller-owned debug-name storage

## Task 4: Add A Process-Wide Sampler Pool To RenderAssetManager

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\RenderAssetManager.cpp`

- [ ] Add the sampler-pool API and storage to `RenderAssetManager`, keyed by `RenderSamplerDesc`.

```cpp
class ASH_API RenderAssetManager
{
public:
    std::shared_ptr<RenderSampler> request_sampler(const RenderSamplerDesc& desc);
    std::shared_ptr<RenderSampler> request_default_sampler();

private:
    std::unordered_map<RenderSamplerDesc, std::shared_ptr<RenderSampler>, RenderSamplerDescHash> m_sampler_pool{};
    std::shared_ptr<RenderSampler> m_default_sampler{};
};
```

- [ ] Implement `request_default_sampler()` and `request_sampler(...)` so the default path is always `Repeat + Linear min/mag/mip`, cache hits are reused globally, and failures fall back to the default sampler before the material bind is declared broken.

```cpp
std::shared_ptr<RenderSampler> RenderAssetManager::request_default_sampler()
{
    if (m_default_sampler)
    {
        return m_default_sampler;
    }

    m_default_sampler = request_sampler(RenderSamplerDesc{});
    return m_default_sampler;
}

std::shared_ptr<RenderSampler> RenderAssetManager::request_sampler(const RenderSamplerDesc& desc)
{
    ASH_PROCESS_GUARD_RETURN(std::shared_ptr<RenderSampler>, result, nullptr, nullptr);
    ASH_PROCESS_ERROR(m_renderer != nullptr);

    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        const auto found = m_sampler_pool.find(desc);
        if (found != m_sampler_pool.end())
        {
            result = found->second;
            break;
        }
    }

    result = m_renderer->create_sampler(desc);
    if (!result && !(desc == RenderSamplerDesc{}))
    {
        log_material_warning_once("default-sampler-fallback", "RenderAssetManager: sampler creation failed, falling back to default repeat sampler.");
        result = request_default_sampler();
        break;
    }
    ASH_PROCESS_ERROR(result != nullptr);

    std::scoped_lock<std::mutex> lock(m_mutex);
    m_sampler_pool.emplace(desc, result);
    if (desc == RenderSamplerDesc{})
    {
        m_default_sampler = result;
    }
    ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
}
```

- [ ] Clear the sampler pool during shutdown alongside the other render-resource caches so sampler objects live for the whole process and only release on teardown.

```cpp
void RenderAssetManager::shutdown()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_material_proxies.clear();
    m_sampler_pool.clear();
    m_default_sampler.reset();
    m_texture_assets.clear();
    m_logged_material_warnings.clear();
    m_logged_texture_warnings.clear();
    m_default_white_texture.reset();
    m_default_normal_texture.reset();
    m_default_black_texture.reset();
    m_asset_database = nullptr;
    m_renderer = nullptr;
}
```

- [ ] Run a focused Engine build to ensure the new pool API and hash key compile cleanly before wiring it into `MaterialRenderProxy`.

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
- requesting the same `RenderSamplerDesc` twice returns the same cached object
- the default sampler path now exists independently of backend enum defaults

## Task 5: Resolve And Bind Per-Texture Samplers In MaterialRenderProxy And SceneSurfacePBR

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.h`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Function\Render\MaterialRenderProxy.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Shaders\SceneSurfacePBR.hlsl`

- [ ] Add four sampler members to `MaterialRenderProxy` and make the cache-valid fast path require both textures and samplers to already exist.

```cpp
class ASH_API MaterialRenderProxy
{
private:
    std::shared_ptr<TextureAsset> m_base_color_texture = nullptr;
    std::shared_ptr<RenderSampler> m_base_color_sampler = nullptr;
    std::shared_ptr<TextureAsset> m_normal_texture = nullptr;
    std::shared_ptr<RenderSampler> m_normal_sampler = nullptr;
    std::shared_ptr<TextureAsset> m_metallic_roughness_texture = nullptr;
    std::shared_ptr<RenderSampler> m_metallic_roughness_sampler = nullptr;
    std::shared_ptr<TextureAsset> m_emissive_texture = nullptr;
    std::shared_ptr<RenderSampler> m_emissive_sampler = nullptr;
};

if (m_material_version == material_version &&
    m_resource.material_uniforms != nullptr &&
    m_base_color_texture != nullptr && m_base_color_sampler != nullptr &&
    m_normal_texture != nullptr && m_normal_sampler != nullptr &&
    m_metallic_roughness_texture != nullptr && m_metallic_roughness_sampler != nullptr &&
    m_emissive_texture != nullptr && m_emissive_sampler != nullptr)
{
    if (m_program && !bind_program_resources())
    {
        ASH_PROCESS_ERROR(false);
    }
    break;
}
```

- [ ] Resolve `MaterialTextureBinding` instead of raw texture paths inside `update_bindings(...)`, and request both the texture asset and the pooled sampler for each material input.

```cpp
static MaterialTextureBinding try_get_texture_parameter_binding(const MaterialInterface& material, const std::string& parameter_name)
{
    MaterialTextureBinding binding{};
    if (material.try_get_texture_parameter(parameter_name, binding))
    {
        return binding;
    }
    return {};
}

const auto resolve_texture_and_sampler = [&asset_manager, &material_asset_path](
    const MaterialTextureBinding& binding,
    TextureColorSpace color_space,
    TextureFallbackKind fallback_kind,
    const char* binding_name,
    float& out_has_texture,
    std::shared_ptr<TextureAsset>& out_texture,
    std::shared_ptr<RenderSampler>& out_sampler) -> bool
{
    out_texture = asset_manager.request_texture_asset(binding.texture_path, color_space, fallback_kind);
    out_sampler = binding.has_explicit_sampler ? asset_manager.request_sampler(binding.sampler) : asset_manager.request_default_sampler();
    out_has_texture =
        out_texture && !binding.texture_path.empty() && out_texture->asset_path == binding.texture_path ?
        1.0f :
        0.0f;
    return out_texture != nullptr && out_sampler != nullptr;
};
```

- [ ] Bind the explicit sampler objects in `bind_program_resources()` and stop routing material rendering through `RenderSamplerState::Default`.

```cpp
ASH_PROCESS_ERROR(m_program->set_uniform_buffer(k_material_uniforms_name, m_resource.material_uniforms));
ASH_PROCESS_ERROR(m_program->set_texture(k_base_color_texture_name, m_base_color_texture->resource));
ASH_PROCESS_ERROR(m_program->set_sampler(k_base_color_sampler_name, m_base_color_sampler));
ASH_PROCESS_ERROR(m_program->set_texture(k_normal_texture_name, m_normal_texture->resource));
ASH_PROCESS_ERROR(m_program->set_sampler(k_normal_sampler_name, m_normal_sampler));
ASH_PROCESS_ERROR(m_program->set_texture(k_metallic_roughness_texture_name, m_metallic_roughness_texture->resource));
ASH_PROCESS_ERROR(m_program->set_sampler(k_metallic_roughness_sampler_name, m_metallic_roughness_sampler));
ASH_PROCESS_ERROR(m_program->set_texture(k_emissive_texture_name, m_emissive_texture->resource));
ASH_PROCESS_ERROR(m_program->set_sampler(k_emissive_sampler_name, m_emissive_sampler));
```

- [ ] Update `SceneSurfacePBR.hlsl` so each texture samples from its own sampler register instead of all four textures sharing `LinearWrapSampler`.

```hlsl
Texture2D<float4> BaseColorTexture : register(t0);
Texture2D<float4> NormalTexture : register(t1);
Texture2D<float4> MetallicRoughnessTexture : register(t2);
Texture2D<float4> EmissiveTexture : register(t3);
SamplerState BaseColorSampler : register(s0);
SamplerState NormalSampler : register(s1);
SamplerState MetallicRoughnessSampler : register(s2);
SamplerState EmissiveSampler : register(s3);

float3 tangentSpaceNormal = NormalTexture.Sample(NormalSampler, input.uv0).xyz * 2.0 - 1.0;
baseSample = BaseColorTexture.Sample(BaseColorSampler, input.uv0);
emissiveSample = EmissiveTexture.Sample(EmissiveSampler, input.uv0).rgb;
float4 packedSample = MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, input.uv0);
```

- [ ] Rebuild the full solution once the shader-reflection-visible binding layout changes from one sampler to four.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine`, `Sandbox`, and `Editor` all build successfully
- shader reflection sees four sampler bindings and `RenderDevice` can satisfy all of them
- material proxies no longer depend on backend-owned “default sampler” behavior

## Task 6: Normalize Legacy Default-Sampler Semantics Across DX12 And Vulkan

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\DirectX12\DX12Context.cpp`
- Modify: `D:\workspace\AshEngine\HASHEAEngine\project\src\engine\Graphics\Vullkan\VulkanContext.cpp`

- [ ] Change the Vulkan enum-based `ASH_SAMPLER_STATE_DEFAULT` creation path from clamp-to-edge to repeat so old call sites no longer diverge from the new material default semantics.

```cpp
case ASH_SAMPLER_STATE_DEFAULT:
    ci.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.minFilter = ASH_FILTER_LINEAR;
    ci.magFilter = ASH_FILTER_LINEAR;
    ci.mipFilter = ASH_FILTER_LINEAR;
    ci.name = "default sampler";
    ret = create_sampler(ci);
    break;
```

- [ ] Normalize the DX12 enum-based default sampler to the same repeat/linear semantics and remove anisotropy from this compatibility path so Vulkan and DX12 share one effective meaning for `RenderSamplerState::Default`.

```cpp
SamplerCreation sc{};
sc.minFilter = ASH_FILTER_LINEAR;
sc.magFilter = ASH_FILTER_LINEAR;
sc.mipFilter = ASH_FILTER_LINEAR;
sc.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
sc.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
sc.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
sc.enable_anisotropy = false;
sc.max_anisotropy = 1.0f;
sc.max_lod = 16.0f;
```

- [ ] Rebuild the full solution after changing backend default sampler semantics, because any remaining legacy enum users still need to compile and behave consistently.

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\workspace\AshEngine\HASHEAEngine\AshEngine.sln' `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Expected:

- `Engine`, `Sandbox`, and `Editor` all build successfully
- `RenderSamplerState::Default` now means repeat/linear on both backends
- any non-material legacy sampler call sites still behave consistently between DX12 and Vulkan

## Task 7: Update Documentation And Run The Full Validation Loop

**Files:**
- Modify: `D:\workspace\AshEngine\HASHEAEngine\docs\EngineDeveloperGuide.md`

- [ ] Update the material/rendering documentation to describe `MaterialTextureBinding`, the global sampler pool, default-repeat semantics, and the fact that per-texture samplers are now bound through `MaterialRenderProxy`.

```md
- `MaterialParameterDesc` texture defaults and `MaterialInstance` texture overrides now use `MaterialTextureBinding`
- `MaterialTextureBinding` stores `texture_path` plus an optional backend-agnostic `RenderSamplerDesc`
- `RenderAssetManager` owns a process-wide sampler pool keyed by `RenderSamplerDesc`
- when a texture binding does not provide an explicit sampler, the engine uses the shared default sampler:
  - `address_u/v/w = Repeat`
  - `min/mag/mip = Linear`
- `MaterialRenderProxy` now binds one sampler per surface texture instead of relying on a single shared `LinearWrapSampler`
```

- [ ] Run the AshEngine validation loop so the change is accepted only after `Sandbox` and `Editor` both pass on Vulkan and DX12 with graceful shutdown.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\huyizhou\.codex\skills\ash-engine-validation-loop\scripts\run-validation-loop.ps1" `
  -Configuration Debug `
  -RunSeconds 25 `
  -CloseTimeoutSeconds 20
```

Expected:

- Premake regeneration succeeds
- solution rebuild succeeds
- `Sandbox` passes on Vulkan and DX12
- `Editor` passes on Vulkan and DX12
- no Vulkan validation output, DX12 debug-layer failures, non-zero exits, early exits, or VMA leak reports appear

- [ ] During the 25-second validation runs, verify the user-visible sampler behaviors that motivated the change instead of relying on clean logs alone.

Checklist:

```md
- a wall or ground material that should tile still repeats correctly on both Vulkan and DX12
- a material authored with explicit `ClampToEdge` no longer wraps at UV borders on either backend
- base color, normal, metallic-roughness, and emissive textures can use different samplers without binding failure
- a material with no explicit texture path still renders through the fallback texture plus default repeat sampler path
```

- [ ] Open the latest validation summary and inspect the referenced logs before declaring success.

Run:

```powershell
$latest = Get-ChildItem 'D:\workspace\AshEngine\HASHEAEngine\product\test-reports\validation-loop' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-Content (Join-Path $latest.FullName 'summary.json') -Raw
```

Expected:

- `summary.json` reports four clean passes
- the requested backend matches the runtime backend for every pass
- no follow-up fixes remain before handoff
