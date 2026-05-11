#include "Function/Render/Material.h"

#include <algorithm>
#include <cctype>

namespace AshEngine
{
	namespace
	{
		static auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		static auto normalize_material_key(std::string_view path) -> std::string
		{
			return to_lower_copy(std::filesystem::path(path).lexically_normal().generic_string());
		}

		static auto canonical_builtin_material_path(std::string_view path) -> std::string
		{
			const std::string key = normalize_material_key(path);
			if (key == normalize_material_key(k_builtin_surface_pbr_material_path))
			{
				return k_builtin_surface_pbr_material_path;
			}
			if (key == normalize_material_key(k_builtin_default_surface_material_path))
			{
				return k_builtin_default_surface_material_path;
			}
			return {};
		}

		static auto build_surface_pbr_material() -> std::shared_ptr<MaterialInterface>
		{
			auto material = std::make_shared<Material>();
			material->set_name("M_SurfacePBR");
			material->set_asset_path(k_builtin_surface_pbr_material_path);
			material->set_domain(MaterialDomain::Surface);
			material->set_material_shader_path("product/assets/materials/v2/M_SurfacePBR.hlsl");
			material->set_static_render_state(MaterialStaticRenderStateDesc{});
			material->set_parameter_descs({
				MaterialParameterDesc{ "BaseColorFactor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(1.0f), 0.0f },
				MaterialParameterDesc{ "Metallic", MaterialParameterType::Scalar, {}, {}, {}, {}, 0.0f },
				MaterialParameterDesc{ "Roughness", MaterialParameterType::Scalar, {}, {}, {}, {}, 1.0f },
				MaterialParameterDesc{ "EmissiveColor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(0.0f), 0.0f },
			});
			material->set_sampler_definitions({
				MaterialSamplerDefinition{ "WrapLinear", "ASH_SurfacePBRSampler", RenderSamplerDesc{} },
			});
			material->set_resource_descs({
				MaterialResourceDesc{ "BaseColorTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB },
				MaterialResourceDesc{ "NormalTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
				MaterialResourceDesc{ "MetallicRoughnessTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
				MaterialResourceDesc{ "EmissiveTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB },
			});
			material->reset_change_version();
			return material;
		}

		static auto build_default_surface_material() -> std::shared_ptr<MaterialInterface>
		{
			auto material_instance = std::make_shared<MaterialInstance>();
			material_instance->set_name("MI_DefaultSurface");
			material_instance->set_asset_path(k_builtin_default_surface_material_path);
			material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
			material_instance->set_parent(make_builtin_material(k_builtin_surface_pbr_material_path));
			material_instance->reset_change_version();
			return material_instance;
		}
	}

	std::shared_ptr<MaterialInterface> make_builtin_material(std::string_view virtual_path)
	{
		const std::string canonical_path = canonical_builtin_material_path(virtual_path);
		if (canonical_path.empty())
		{
			return nullptr;
		}

		if (canonical_path == k_builtin_surface_pbr_material_path)
		{
			static std::shared_ptr<MaterialInterface> surface_material = build_surface_pbr_material();
			return surface_material;
		}

		if (canonical_path == k_builtin_default_surface_material_path)
		{
			static std::shared_ptr<MaterialInterface> default_surface_material = build_default_surface_material();
			return default_surface_material;
		}

		return nullptr;
	}
}
