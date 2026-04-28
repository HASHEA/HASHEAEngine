#include "Function/Render/EngineShaderFamilyRegistry.h"

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

		static constexpr auto to_capability_mask(MaterialCapability capability) -> uint64_t
		{
			return static_cast<uint64_t>(capability);
		}
	}

	std::string_view EngineShaderFamilyDesc::resolve_host_shader_path(PassFamily pass) const
	{
		switch (pass)
		{
		case PassFamily::DepthOnly:
			return depth_only_shader_path;
		case PassFamily::BasePass:
		default:
			return base_pass_shader_path;
		}
	}

	bool EngineShaderFamilyRegistry::initialize()
	{
		m_families.clear();
		m_families.push_back({
			EngineShaderFamily::SurfaceStaticMesh,
			MaterialDomain::Surface,
			"Surface.StaticMesh",
			"project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshBasePass.hlsl",
			"project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshDepthOnly.hlsl",
			to_capability_mask(MaterialCapability::VertexColor) |
				to_capability_mask(MaterialCapability::UV1)
		});
		return true;
	}

	void EngineShaderFamilyRegistry::shutdown()
	{
		m_families.clear();
	}

	const EngineShaderFamilyDesc* EngineShaderFamilyRegistry::find(EngineShaderFamily family) const
	{
		for (const EngineShaderFamilyDesc& desc : m_families)
		{
			if (desc.family == family)
			{
				return &desc;
			}
		}
		return nullptr;
	}

	bool EngineShaderFamilyRegistry::validate_usage(
		const MaterialInterface& material,
		const MaterialUsageDesc& usage,
		std::string* out_error) const
	{
		const EngineShaderFamilyDesc* family_desc = find(usage.family);
		if (!family_desc)
		{
			if (out_error)
			{
				*out_error = "Unknown engine shader family.";
			}
			return false;
		}

		if (material.get_domain() != usage.domain || family_desc->domain != usage.domain)
		{
			if (out_error)
			{
				*out_error = "Material domain does not match requested usage.";
			}
			return false;
		}

		if (family_desc->resolve_host_shader_path(usage.pass).empty())
		{
			if (out_error)
			{
				*out_error = "Requested pass is not registered for this engine shader family.";
			}
			return false;
		}

		std::string capability_error{};
		const uint64_t required_capability_mask = resolve_required_capability_mask(material, &capability_error);
		if (!capability_error.empty())
		{
			if (out_error)
			{
				*out_error = capability_error;
			}
			return false;
		}

		if ((required_capability_mask & ~usage.capability_mask) != 0)
		{
			if (out_error)
			{
				*out_error = "Material requires capabilities that are missing from this usage.";
			}
			return false;
		}

		if ((usage.capability_mask & ~family_desc->supported_capability_mask) != 0)
		{
			if (out_error)
			{
				*out_error = "Usage requests capabilities unsupported by this engine shader family.";
			}
			return false;
		}

		return true;
	}

	uint64_t EngineShaderFamilyRegistry::resolve_required_capability_mask(
		const MaterialInterface& material,
		std::string* out_error) const
	{
		uint64_t capability_mask = 0;
		for (const std::string& capability_name : material.get_required_capabilities())
		{
			const uint64_t capability = capability_from_name(capability_name);
			if (capability == 0)
			{
				if (out_error)
				{
					*out_error = "Unknown material required capability: " + capability_name;
				}
				return 0;
			}
			capability_mask |= capability;
		}
		return capability_mask;
	}

	uint64_t EngineShaderFamilyRegistry::capability_from_name(std::string_view name)
	{
		const std::string lowered = to_lower_copy(std::string(name));
		if (lowered == "vertexcolor" || lowered == "vertex_color")
		{
			return to_capability_mask(MaterialCapability::VertexColor);
		}
		if (lowered == "uv1")
		{
			return to_capability_mask(MaterialCapability::UV1);
		}
		return 0;
	}
}
