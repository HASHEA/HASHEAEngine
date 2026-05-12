#include "Function/Render/MaterialShaderSourceBuilder.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_generated_parameter_block_name = "AshMaterialParameters";

		static auto write_text_file_if_changed(const std::filesystem::path& path, const std::string& text) -> bool
		{
			std::error_code create_error{};
			std::filesystem::create_directories(path.parent_path(), create_error);
			if (create_error)
			{
				return false;
			}

			std::ifstream input(path, std::ios::binary);
			if (input)
			{
				std::stringstream existing_stream{};
				existing_stream << input.rdbuf();
				if (existing_stream.str() == text)
				{
					return true;
				}
			}

			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output)
			{
				return false;
			}
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			return static_cast<bool>(output);
		}

		static auto parameter_type_to_hlsl(MaterialParameterType type) -> const char*
		{
			switch (type)
			{
			case MaterialParameterType::Scalar:
				return "float";
			case MaterialParameterType::Vector4:
				return "float4";
			default:
				return nullptr;
			}
		}

		static auto build_usage_name(const MaterialUsageDesc& usage) -> std::string
		{
			switch (usage.family)
			{
			case EngineShaderFamily::SurfaceStaticMesh:
			default:
				switch (usage.pass)
				{
				case PassFamily::DepthOnly:
					return "SurfaceStaticMesh_DepthOnly";
				case PassFamily::GBuffer:
					return "SurfaceStaticMesh_GBuffer";
				case PassFamily::BasePass:
				default:
					return "SurfaceStaticMesh_BasePass";
				}
			}
		}

		static auto build_generated_source_hash(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			const std::string& source_text) -> uint64_t
		{
			uint64_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, material.get_compile_hash());
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.domain));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.family));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.pass));
			ASH_HASH::hash_combine(hash_value, usage.capability_mask);
			ASH_HASH::hash_combine(hash_value, source_text, std::hash<std::string>{});
			return hash_value;
		}
	}

	MaterialGeneratedSourcePaths MaterialShaderSourceBuilder::build_source(
		const MaterialInterface& material,
		const MaterialUsageDesc& usage,
		const EngineShaderFamilyRegistry& family_registry,
		const std::filesystem::path& output_root,
		std::string* out_error) const
	{
		MaterialGeneratedSourcePaths result{};

		const EngineShaderFamilyDesc* family_desc = family_registry.find(usage.family);
		if (!family_desc)
		{
			if (out_error)
			{
				*out_error = "Unknown engine shader family.";
			}
			return result;
		}

		std::ostringstream bindings{};
		const MaterialStaticRenderStateDesc& render_state = material.get_static_render_state();
		bindings << "#pragma once\n";
		bindings << "#define ASH_DOMAIN_SURFACE " << (usage.domain == MaterialDomain::Surface ? 1 : 0) << "\n";
		bindings << "#define ASH_ENGINE_FAMILY_SURFACE_STATIC_MESH " << (usage.family == EngineShaderFamily::SurfaceStaticMesh ? 1 : 0) << "\n";
		bindings << "#define ASH_PASS_BASE_PASS " << (usage.pass == PassFamily::BasePass ? 1 : 0) << "\n";
		bindings << "#define ASH_PASS_DEPTH_ONLY " << (usage.pass == PassFamily::DepthOnly ? 1 : 0) << "\n";
		bindings << "#define ASH_PASS_GBUFFER " << (usage.pass == PassFamily::GBuffer ? 1 : 0) << "\n";
		if (usage.pass == PassFamily::GBuffer)
		{
			bindings << "#define ASH_GBUFFER_LAYOUT_DEFERRED_HQ 1\n";
			bindings << "#define ASH_GBUFFER_OUTPUT_COUNT 5\n";
			bindings << "#define ASH_GBUFFER_HAS_MOTION_VECTOR_3D 1\n";
		}
		bindings << "#define ASH_HAS_VERTEX_COLOR " << ((usage.capability_mask & static_cast<uint64_t>(MaterialCapability::VertexColor)) != 0 ? 1 : 0) << "\n";
		bindings << "#define ASH_HAS_UV1 " << ((usage.capability_mask & static_cast<uint64_t>(MaterialCapability::UV1)) != 0 ? 1 : 0) << "\n";
		bindings << "#define ASH_MATERIAL_BLEND_MODE_OPAQUE " << (render_state.blend_mode == MaterialBlendMode::Opaque ? 1 : 0) << "\n";
		bindings << "#define ASH_MATERIAL_BLEND_MODE_MASKED " << (render_state.blend_mode == MaterialBlendMode::Masked ? 1 : 0) << "\n";
		bindings << "#define ASH_MATERIAL_BLEND_MODE_TRANSPARENT " << (render_state.blend_mode == MaterialBlendMode::Transparent ? 1 : 0) << "\n";
		bindings << "#define ASH_MATERIAL_TWO_SIDED " << (render_state.two_sided ? 1 : 0) << "\n";
		bindings << "#define ASH_MATERIAL_ALPHA_CUTOFF " << render_state.alpha_cutoff << "\n";

		for (const MaterialStaticSwitchDesc& static_switch : material.get_static_switches())
		{
			bindings << "#define " << static_switch.name << " " << (static_switch.value ? 1 : 0) << "\n";
		}

		bool has_parameter_block = false;
		for (const MaterialParameterDesc& parameter_desc : material.get_parameter_descs())
		{
			const char* hlsl_type = parameter_type_to_hlsl(parameter_desc.type);
			if (!hlsl_type)
			{
				if (out_error)
				{
					*out_error = "Unsupported V2 material parameter type on '" + parameter_desc.name + "'.";
				}
				return result;
			}

			if (!has_parameter_block)
			{
				bindings << "\ncbuffer " << k_generated_parameter_block_name << " : register(b1)\n{\n";
				has_parameter_block = true;
			}
			bindings << "    " << hlsl_type << " " << parameter_desc.name << ";\n";
		}
		if (has_parameter_block)
		{
			bindings << "};\n";
		}

		std::unordered_map<std::string, uint32_t> sampler_bind_points{};
		uint32_t next_texture_bind_point = 0;
		uint32_t next_sampler_bind_point = 0;

		for (const MaterialResourceDesc& resource_desc : material.get_resource_descs())
		{
			if (resource_desc.type != MaterialResourceType::Texture2D)
			{
				if (out_error)
				{
					*out_error = "Unsupported V2 material resource type on '" + resource_desc.name + "'.";
				}
				return result;
			}

			if (resource_desc.sampler.empty())
			{
				if (out_error)
				{
					*out_error = "V2 material resource '" + resource_desc.name + "' is missing a sampler reference.";
				}
				return result;
			}

			const MaterialSamplerDefinition* sampler_definition = find_material_sampler_definition(material, resource_desc.sampler);
			if (!sampler_definition)
			{
				if (out_error)
				{
					*out_error = "V2 material resource '" + resource_desc.name + "' references unknown sampler '" + resource_desc.sampler + "'.";
				}
				return result;
			}

			const std::string shader_sampler_name =
				sampler_definition->shader_sampler_name.empty() ?
				sampler_definition->name :
				sampler_definition->shader_sampler_name;
			if (shader_sampler_name.empty())
			{
				if (out_error)
				{
					*out_error = "V2 material sampler definition '" + sampler_definition->name + "' resolved to an empty shader sampler name.";
				}
				return result;
			}

			bindings << "Texture2D<float4> " << resource_desc.name << " : register(t" << next_texture_bind_point++ << ");\n";
			if (sampler_bind_points.find(shader_sampler_name) == sampler_bind_points.end())
			{
				sampler_bind_points.emplace(shader_sampler_name, next_sampler_bind_point);
				bindings << "SamplerState " << shader_sampler_name << " : register(s" << next_sampler_bind_point++ << ");\n";
			}
		}

		const std::string bindings_text = bindings.str();
		const std::string usage_name = build_usage_name(usage);
		const std::filesystem::path output_dir =
			output_root /
			std::to_string(material.get_compile_hash()) /
			usage_name;
		const std::filesystem::path bindings_path = output_dir / "Bindings.generated.hlsli";
		if (!write_text_file_if_changed(bindings_path, bindings_text))
		{
			if (out_error)
			{
				*out_error = "Failed to write generated material bindings include: " + bindings_path.generic_string();
			}
			return result;
		}

		result.bindings_include_path = bindings_path.lexically_normal();
		result.combined_source_hash = build_generated_source_hash(material, usage, bindings_text);
		return result;
	}
}
