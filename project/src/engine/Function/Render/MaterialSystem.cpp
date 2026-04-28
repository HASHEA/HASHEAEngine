#include "Function/Render/MaterialSystem.h"

#include "Base/hlog.h"

namespace AshEngine
{
	namespace
	{
		static auto build_usage_name(const MaterialUsageDesc& usage) -> std::string
		{
			std::string usage_name =
				usage.family == EngineShaderFamily::SurfaceStaticMesh ?
				"Surface.StaticMesh" :
				"UnknownFamily";
			usage_name += usage.pass == PassFamily::DepthOnly ? ".DepthOnly" : ".BasePass";
			return usage_name;
		}

		static auto build_material_label(const MaterialInterface& material) -> std::string
		{
			if (!material.get_asset_path().empty())
			{
				return material.get_asset_path().generic_string();
			}
			if (!material.get_name().empty())
			{
				return material.get_name();
			}
			return "<unnamed-material>";
		}
	}

	bool MaterialSystem::initialize(Renderer* renderer)
	{
		m_renderer = renderer;
		m_domain_fallbacks.clear();

		if (!m_renderer || !m_family_registry.initialize() || !m_shader_map.initialize(renderer, &m_family_registry))
		{
			m_shader_map.shutdown();
			m_family_registry.shutdown();
			m_renderer = nullptr;
			return false;
		}

		if (std::shared_ptr<MaterialInterface> surface_fallback = make_builtin_material(k_builtin_default_surface_material_path))
		{
			m_domain_fallbacks.emplace(MaterialDomain::Surface, std::move(surface_fallback));
		}
		return true;
	}

	void MaterialSystem::shutdown()
	{
		m_domain_fallbacks.clear();
		m_shader_map.shutdown();
		m_family_registry.shutdown();
		m_renderer = nullptr;
	}

	const MaterialResource* MaterialSystem::get_or_create_resource(
		const MaterialInterface& material,
		const MaterialUsageDesc& usage,
		std::string* out_error)
	{
		if (!m_renderer)
		{
			if (out_error)
			{
				*out_error = "MaterialSystem is not initialized.";
			}
			return nullptr;
		}

		std::string last_error{};
		const MaterialResource* resource = nullptr;

		const auto try_resolve_resource = [&](const MaterialInterface& candidate, std::string* error_text) -> const MaterialResource*
		{
			if (!m_family_registry.validate_usage(candidate, usage, error_text))
			{
				return nullptr;
			}
			return m_shader_map.find_or_create_resource(candidate, usage, error_text);
		};

		resource = try_resolve_resource(material, &last_error);
		if (resource)
		{
			if (out_error)
			{
				out_error->clear();
			}
			return resource;
		}

		const MaterialInterface* fallback_material = get_domain_fallback(usage.domain);
		if (fallback_material && fallback_material != &material)
		{
			HLogWarning(
				"MaterialSystem: material '{}' failed for usage '{}': {}. Falling back to '{}'.",
				build_material_label(material),
				build_usage_name(usage),
				last_error.empty() ? std::string("unknown error") : last_error,
				build_material_label(*fallback_material));

			std::string fallback_error{};
			resource = try_resolve_resource(*fallback_material, &fallback_error);
			if (resource)
			{
				if (out_error)
				{
					out_error->clear();
				}
				return resource;
			}

			last_error = fallback_error.empty() ? last_error : fallback_error;
		}

		if (out_error)
		{
			*out_error = last_error.empty() ? "MaterialSystem failed to resolve a material resource." : last_error;
		}
		return nullptr;
	}

	const MaterialInterface* MaterialSystem::get_domain_fallback(MaterialDomain domain) const
	{
		const auto found = m_domain_fallbacks.find(domain);
		return found != m_domain_fallbacks.end() ? found->second.get() : nullptr;
	}
}
