#pragma once

#include "Base/hcore.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	inline constexpr const char* k_builtin_surface_pbr_material_path = "Engine/Materials/V2/M_SurfacePBR.AshMat";
	inline constexpr const char* k_builtin_default_surface_material_path = "Engine/Materials/V2/MI_DefaultSurface.AshMatIns";

	enum class RenderCullMode : uint8_t;

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
		Vector4
	};

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

		bool operator!=(const RenderSamplerDesc& rhs) const
		{
			return !(*this == rhs);
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
		std::string sampler_name{};

		bool operator==(const MaterialTextureBinding& rhs) const
		{
			return texture_path == rhs.texture_path &&
				sampler_name == rhs.sampler_name;
		}

		bool operator!=(const MaterialTextureBinding& rhs) const
		{
			return !(*this == rhs);
		}
	};

	struct ASH_API MaterialSamplerDefinition
	{
		std::string name{};
		std::string shader_sampler_name{};
		RenderSamplerDesc desc{};

		bool operator==(const MaterialSamplerDefinition& rhs) const
		{
			return name == rhs.name &&
				shader_sampler_name == rhs.shader_sampler_name &&
				desc == rhs.desc;
		}

		bool operator!=(const MaterialSamplerDefinition& rhs) const
		{
			return !(*this == rhs);
		}
	};

	struct ASH_API MaterialParameterDesc
	{
		std::string name{};
		MaterialParameterType type = MaterialParameterType::Scalar;
		std::string group{};
		std::string semantic{};
		std::string texture_usage{};
		glm::vec4 default_vector{ 0.0f };
		float default_scalar = 0.0f;
	};

	struct ASH_API MaterialStaticSwitchDesc
	{
		std::string name{};
		bool value = false;
	};

	struct ASH_API MaterialResourceDesc
	{
		std::string name{};
		MaterialResourceType type = MaterialResourceType::Texture2D;
		std::string default_path{};
		std::string sampler{};
		MaterialResourceColorSpace color_space = MaterialResourceColorSpace::Linear;
	};

	struct ASH_API MaterialStaticRenderStateDesc
	{
		MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
		bool two_sided = false;
		RenderCullMode cull_mode;
		bool depth_write = true;
		MaterialCompareOp depth_test = MaterialCompareOp::LessEqual;
		float alpha_cutoff = 0.5f;

		MaterialStaticRenderStateDesc();
	};

	class Material;

	class ASH_API MaterialInterface
	{
	public:
		virtual ~MaterialInterface() = default;

		virtual bool is_material_instance() const = 0;
		virtual const std::string& get_name() const = 0;
		virtual const std::filesystem::path& get_asset_path() const = 0;
		virtual MaterialDomain get_domain() const = 0;
		virtual MaterialBlendMode get_blend_mode() const = 0;
		virtual MaterialShadingModel get_shading_model() const = 0;
		virtual bool is_two_sided() const = 0;
		virtual float get_alpha_cutoff() const = 0;
		virtual uint64_t get_change_version() const = 0;
		virtual std::string_view get_material_shader_path() const = 0;
		virtual const MaterialStaticRenderStateDesc& get_static_render_state() const = 0;
		virtual const std::vector<std::string>& get_required_capabilities() const = 0;
		virtual const std::vector<MaterialStaticSwitchDesc>& get_static_switches() const = 0;
		virtual const std::vector<MaterialResourceDesc>& get_resource_descs() const = 0;
		virtual uint64_t get_compile_hash() const = 0;
		virtual const Material* resolve_base_material() const = 0;
		virtual const std::vector<MaterialParameterDesc>& get_parameter_descs() const = 0;
		virtual const std::vector<MaterialSamplerDefinition>& get_sampler_definitions() const = 0;
		virtual bool try_get_scalar_parameter(const std::string& name, float& out_value) const = 0;
		virtual bool try_get_vector_parameter(const std::string& name, glm::vec4& out_value) const = 0;
		virtual bool try_get_resource_binding(const std::string& name, MaterialTextureBinding& out_binding) const = 0;
	};

	class ASH_API Material final : public MaterialInterface
	{
	public:
		Material() = default;

	public:
		bool is_material_instance() const override;
		const std::string& get_name() const override;
		const std::filesystem::path& get_asset_path() const override;
		MaterialDomain get_domain() const override;
		MaterialBlendMode get_blend_mode() const override;
		MaterialShadingModel get_shading_model() const override;
		bool is_two_sided() const override;
		float get_alpha_cutoff() const override;
		uint64_t get_change_version() const override;
		std::string_view get_material_shader_path() const override;
		const MaterialStaticRenderStateDesc& get_static_render_state() const override;
		const std::vector<std::string>& get_required_capabilities() const override;
		const std::vector<MaterialStaticSwitchDesc>& get_static_switches() const override;
		const std::vector<MaterialResourceDesc>& get_resource_descs() const override;
		uint64_t get_compile_hash() const override;
		const Material* resolve_base_material() const override;
		const std::vector<MaterialParameterDesc>& get_parameter_descs() const override;
		const std::vector<MaterialSamplerDefinition>& get_sampler_definitions() const override;
		bool try_get_scalar_parameter(const std::string& name, float& out_value) const override;
		bool try_get_vector_parameter(const std::string& name, glm::vec4& out_value) const override;
		bool try_get_resource_binding(const std::string& name, MaterialTextureBinding& out_binding) const override;

		void set_name(std::string name);
		void set_asset_path(std::filesystem::path asset_path);
		void set_domain(MaterialDomain domain);
		void set_blend_mode(MaterialBlendMode blend_mode);
		void set_shading_model(MaterialShadingModel shading_model);
		void set_two_sided(bool two_sided);
		void set_alpha_cutoff(float alpha_cutoff);
		void set_material_shader_path(std::string material_shader_path);
		void set_static_render_state(const MaterialStaticRenderStateDesc& static_render_state);
		void set_required_capabilities(std::vector<std::string> required_capabilities);
		void add_required_capability(std::string capability);
		void set_static_switches(std::vector<MaterialStaticSwitchDesc> static_switches);
		void add_static_switch(MaterialStaticSwitchDesc static_switch);
		void set_resource_descs(std::vector<MaterialResourceDesc> resource_descs);
		void add_resource_desc(MaterialResourceDesc resource_desc);
		void set_parameter_descs(std::vector<MaterialParameterDesc> parameter_descs);
		void add_parameter_desc(MaterialParameterDesc parameter_desc);
		void set_sampler_definitions(std::vector<MaterialSamplerDefinition> sampler_definitions);
		void add_sampler_definition(MaterialSamplerDefinition sampler_definition);
		void reset_change_version(uint64_t version = 1);
		void mark_changed();

		const MaterialParameterDesc* find_parameter_desc(const std::string& name) const;
		const MaterialSamplerDefinition* find_sampler_definition(const std::string& name) const;
		const MaterialResourceDesc* find_resource_desc(const std::string& name) const;

	private:
		std::string m_name{};
		std::filesystem::path m_asset_path{};
		MaterialDomain m_domain = MaterialDomain::Surface;
		MaterialBlendMode m_blend_mode = MaterialBlendMode::Opaque;
		MaterialShadingModel m_shading_model = MaterialShadingModel::DefaultLit;
		bool m_two_sided = false;
		float m_alpha_cutoff = 0.5f;
		std::string m_material_shader_path{};
		MaterialStaticRenderStateDesc m_static_render_state{};
		std::vector<std::string> m_required_capabilities{};
		std::vector<MaterialStaticSwitchDesc> m_static_switches{};
		std::vector<MaterialResourceDesc> m_resource_descs{};
		std::vector<MaterialParameterDesc> m_parameter_descs{};
		std::vector<MaterialSamplerDefinition> m_sampler_definitions{};
		uint64_t m_compile_hash = 0;
		uint64_t m_change_version = 1;
	};

	class ASH_API MaterialInstance final : public MaterialInterface
	{
	public:
		MaterialInstance() = default;

	public:
		bool is_material_instance() const override;
		const std::string& get_name() const override;
		const std::filesystem::path& get_asset_path() const override;
		MaterialDomain get_domain() const override;
		MaterialBlendMode get_blend_mode() const override;
		MaterialShadingModel get_shading_model() const override;
		bool is_two_sided() const override;
		float get_alpha_cutoff() const override;
		uint64_t get_change_version() const override;
		std::string_view get_material_shader_path() const override;
		const MaterialStaticRenderStateDesc& get_static_render_state() const override;
		const std::vector<std::string>& get_required_capabilities() const override;
		const std::vector<MaterialStaticSwitchDesc>& get_static_switches() const override;
		const std::vector<MaterialResourceDesc>& get_resource_descs() const override;
		uint64_t get_compile_hash() const override;
		const Material* resolve_base_material() const override;
		const std::vector<MaterialParameterDesc>& get_parameter_descs() const override;
		const std::vector<MaterialSamplerDefinition>& get_sampler_definitions() const override;
		bool try_get_scalar_parameter(const std::string& name, float& out_value) const override;
		bool try_get_vector_parameter(const std::string& name, glm::vec4& out_value) const override;
		bool try_get_resource_binding(const std::string& name, MaterialTextureBinding& out_binding) const override;

		void set_name(std::string name);
		void set_asset_path(std::filesystem::path asset_path);
		void set_parent(std::shared_ptr<const MaterialInterface> parent);
		void clear_parent();
		const std::shared_ptr<const MaterialInterface>& get_parent() const;
		void set_parent_asset_path(std::filesystem::path parent_asset_path);
		const std::filesystem::path& get_parent_asset_path() const;
		void clear_overrides();
		void set_sampler_definitions(std::vector<MaterialSamplerDefinition> sampler_definitions);
		void add_sampler_definition(MaterialSamplerDefinition sampler_definition);
		void clear_sampler_definitions();
		const std::vector<MaterialSamplerDefinition>& get_local_sampler_definitions() const;
		void set_scalar_override(const std::string& name, float value);
		void set_vector_override(const std::string& name, const glm::vec4& value);
		void set_resource_override(const std::string& name, MaterialTextureBinding value);
		void remove_override(const std::string& name);
		const std::unordered_map<std::string, float>& get_scalar_overrides() const;
		const std::unordered_map<std::string, glm::vec4>& get_vector_overrides() const;
		const std::unordered_map<std::string, MaterialTextureBinding>& get_resource_overrides() const;
		void reset_change_version(uint64_t version = 1);
		void mark_changed();

	private:
		std::string m_name{};
		std::filesystem::path m_asset_path{};
		std::shared_ptr<const MaterialInterface> m_parent{};
		std::filesystem::path m_parent_asset_path{};
		std::vector<MaterialSamplerDefinition> m_sampler_definitions{};
		mutable std::vector<MaterialSamplerDefinition> m_cached_merged_sampler_definitions{};
		std::unordered_map<std::string, float> m_scalar_overrides{};
		std::unordered_map<std::string, glm::vec4> m_vector_overrides{};
		std::unordered_map<std::string, MaterialTextureBinding> m_resource_overrides{};
		uint64_t m_change_version = 1;
	};

	ASH_API const MaterialParameterDesc* find_material_parameter_desc(const MaterialInterface& material, const std::string& name);
	ASH_API const MaterialSamplerDefinition* find_material_sampler_definition(const MaterialInterface& material, const std::string& name);
	ASH_API const MaterialResourceDesc* find_material_resource_desc(const MaterialInterface& material, const std::string& name);
	ASH_API bool load_material_from_file(
		const std::filesystem::path& path,
		std::shared_ptr<MaterialInterface>& out_material,
		std::string* out_error = nullptr);
	ASH_API bool save_material_to_file(
		const MaterialInterface& material,
		const std::filesystem::path& path,
		std::string* out_error = nullptr);
	ASH_API std::shared_ptr<MaterialInterface> make_builtin_material(std::string_view virtual_path);
}
