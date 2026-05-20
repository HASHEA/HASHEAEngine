#include "Function/Render/MaterialShaderMap.h"

#include "Base/hlog.h"
#include "Function/Render/MaterialShaderSourceBuilder.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/VertexLayoutPresets.h"
#include <sstream>

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
			switch (usage.pass)
			{
			case PassFamily::DepthOnly:
				usage_name += ".DepthOnly";
				break;
			case PassFamily::GBuffer:
				usage_name += ".GBuffer";
				break;
			case PassFamily::BasePass:
			default:
				usage_name += ".BasePass";
				break;
			}
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

		static auto build_program_name(const MaterialInterface& material, const MaterialUsageDesc& usage) -> std::string
		{
			std::string material_name = material.get_name();
			if (material_name.empty())
			{
				material_name = material.get_asset_path().stem().string();
			}
			if (material_name.empty())
			{
				material_name = "MaterialV2";
			}

			std::ostringstream stream{};
			stream << material_name << "_";
			switch (usage.family)
			{
			case EngineShaderFamily::SurfaceStaticMesh:
			default:
				stream << "SurfaceStaticMesh";
				break;
			}
			switch (usage.pass)
			{
			case PassFamily::DepthOnly:
				stream << "_DepthOnly";
				break;
			case PassFamily::GBuffer:
				stream << "_GBuffer";
				break;
			case PassFamily::BasePass:
			default:
				stream << "_BasePass";
				break;
			}
			return stream.str();
		}

		static auto build_combined_source_hash(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			const MaterialGeneratedSourcePaths& generated_source,
			std::string_view host_shader_path,
			uint64_t shader_file_signature_hash) -> uint64_t
		{
			uint64_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, material.get_compile_hash());
			ASH_HASH::hash_combine(hash_value, generated_source.combined_source_hash);
			ASH_HASH::hash_combine(hash_value, shader_file_signature_hash);
			ASH_HASH::hash_combine(hash_value, host_shader_path.data(), ASH_HASH::CStringHash{});
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.domain));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.family));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.pass));
			ASH_HASH::hash_combine(hash_value, usage.capability_mask);
			return hash_value;
		}

		static auto build_shader_file_signature_hash(
			EngineShaderFamily family,
			std::string_view host_shader_path,
			std::string_view user_shader_path,
			std::string_view generated_bindings_path = {}) -> uint64_t
		{
			uint64_t hash_value = 0;
			const std::string host_path(host_shader_path);
			const std::string user_path(user_shader_path);
			const std::string generated_path(generated_bindings_path);
			RHI::hash_shader_file_signature(hash_value, host_path.c_str());
			RHI::hash_shader_file_signature(hash_value, user_path.c_str());
			RHI::hash_shader_file_signature(hash_value, generated_path.c_str());
			hash_engine_shader_family_file_signatures(hash_value, family);
			return hash_value;
		}

		static auto build_permutation_cache_hash(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			std::string_view host_shader_path,
			uint64_t shader_file_signature_hash) -> uint64_t
		{
			uint64_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, material.get_compile_hash());
			ASH_HASH::hash_combine(hash_value, shader_file_signature_hash);
			ASH_HASH::hash_combine(hash_value, host_shader_path.data(), ASH_HASH::CStringHash{});
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.domain));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.family));
			ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(usage.pass));
			ASH_HASH::hash_combine(hash_value, usage.capability_mask);
			return hash_value;
		}

		static auto build_program_state(const MaterialStaticRenderStateDesc& render_state) -> GraphicsProgramState
		{
			GraphicsProgramState program_state{};
			program_state.cull_mode = render_state.two_sided ? RenderCullMode::None : render_state.cull_mode;
			program_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			program_state.depth_test = render_state.depth_test != MaterialCompareOp::Always;
			program_state.depth_write = render_state.depth_write;
			program_state.depth_compare = render_state.depth_test == MaterialCompareOp::Always ?
				RenderCompareOp::Always :
				RenderCompareOp::LessEqual;
			program_state.blend_mode = RenderBlendMode::Opaque;
			program_state.front_face = RenderFrontFace::CounterClockwise;
			return program_state;
		}

		static auto build_pass_relevance(const MaterialInterface& material, const MaterialUsageDesc& usage) -> MaterialPassRelevance
		{
			const MaterialBlendMode blend_mode = material.get_blend_mode();
			MaterialPassRelevance relevance{};
			relevance.domain = usage.domain;
			relevance.supports_surface = usage.domain == MaterialDomain::Surface;
			relevance.supports_base_pass =
				relevance.supports_surface &&
				usage.pass == PassFamily::BasePass &&
				blend_mode != MaterialBlendMode::Transparent;
			relevance.supports_gbuffer_pass =
				relevance.supports_surface &&
				usage.pass == PassFamily::GBuffer &&
				blend_mode != MaterialBlendMode::Transparent;
			relevance.supports_depth_prepass =
				relevance.supports_surface &&
				usage.pass == PassFamily::DepthOnly &&
				blend_mode != MaterialBlendMode::Transparent;
			relevance.is_masked = blend_mode == MaterialBlendMode::Masked;
			relevance.is_transparent = blend_mode == MaterialBlendMode::Transparent;
			return relevance;
		}

		static auto append_unique_binding_layouts(
			const std::vector<RHI::ShaderResourceBindingLayout>& reflected_layouts,
			std::vector<MaterialBindingLayoutEntry>& out_layouts) -> void
		{
			for (const RHI::ShaderResourceBindingLayout& reflected_layout : reflected_layouts)
			{
				bool found = false;
				for (const MaterialBindingLayoutEntry& existing : out_layouts)
				{
					if (existing.name == reflected_layout.name && existing.type == reflected_layout.type)
					{
						found = true;
						break;
					}
				}
				if (found)
				{
					continue;
				}

				out_layouts.push_back({
					reflected_layout.name,
					reflected_layout.type,
					reflected_layout.bind_point
				});
			}
		}
	}

	MaterialShaderMap::~MaterialShaderMap() = default;

	bool MaterialShaderMap::initialize(Renderer* renderer, const EngineShaderFamilyRegistry* family_registry)
	{
		m_renderer = renderer;
		m_family_registry = family_registry;
		m_generated_source_root = std::filesystem::path("product") / "caches" / "ShaderGenerated" / "Materials";
		m_source_builder = std::make_unique<MaterialShaderSourceBuilder>();
		m_resources.clear();
		return m_renderer != nullptr && m_family_registry != nullptr && m_source_builder != nullptr;
	}

	void MaterialShaderMap::shutdown()
	{
		m_resources.clear();
		m_source_builder.reset();
		m_generated_source_root.clear();
		m_family_registry = nullptr;
		m_renderer = nullptr;
	}

	const MaterialResource* MaterialShaderMap::find_or_create_resource(
		const MaterialInterface& material,
		const MaterialUsageDesc& usage,
		std::string* out_error)
	{
		if (!m_renderer || !m_family_registry || !m_source_builder)
		{
			if (out_error)
			{
				*out_error = "MaterialShaderMap is not initialized.";
			}
			return nullptr;
		}

		const EngineShaderFamilyDesc* family_desc = m_family_registry->find(usage.family);
		if (!family_desc)
		{
			if (out_error)
			{
				*out_error = "Engine shader family lookup failed.";
			}
			return nullptr;
		}

		const std::string host_shader_path(family_desc->resolve_host_shader_path(usage.pass));
		if (host_shader_path.empty())
		{
			if (out_error)
			{
				*out_error = "Requested pass has no registered host shader path.";
			}
			return nullptr;
		}

		const std::string user_shader_path(material.get_material_shader_path());
		const uint64_t permutation_file_signature_hash =
			build_shader_file_signature_hash(usage.family, host_shader_path, user_shader_path);
		const MaterialPermutationKey key{
			build_permutation_cache_hash(material, usage, host_shader_path, permutation_file_signature_hash),
			usage
		};
		if (const auto found = m_resources.find(key); found != m_resources.end())
		{
			return &found->second->resource;
		}

		MaterialGeneratedSourcePaths generated_source =
			m_source_builder->build_source(material, usage, *m_family_registry, m_generated_source_root, out_error);
		if (generated_source.bindings_include_path.empty())
		{
			return nullptr;
		}

		MaterialResource resource{};
		resource.usage = usage;
		resource.shader_file_signature_hash =
			build_shader_file_signature_hash(
				usage.family,
				host_shader_path,
				user_shader_path,
				generated_source.bindings_include_path.generic_string());
		resource.combined_source_hash =
			build_combined_source_hash(
				material,
				usage,
				generated_source,
				host_shader_path,
				resource.shader_file_signature_hash);
		resource.base_shader_path = host_shader_path;
		resource.user_shader_path = user_shader_path;
		resource.generated_bindings_path = generated_source.bindings_include_path.generic_string();
		resource.program_name = build_program_name(material, usage);
		resource.program_state = build_program_state(material.get_static_render_state());
		resource.vertex_decl = get_instanced_mesh_vertex_decl();
		resource.pass_relevance = build_pass_relevance(material, usage);
		resource.blend_mode = material.get_blend_mode();
		resource.shading_model = material.get_shading_model();

		GraphicsProgramDesc program_desc{};
		program_desc.shader_path = resource.base_shader_path.c_str();
		program_desc.base_shader_path = resource.base_shader_path.c_str();
		program_desc.user_shader_path =
			resource.user_shader_path.empty() ? nullptr : resource.user_shader_path.c_str();
		program_desc.generated_bindings_path =
			resource.generated_bindings_path.empty() ? nullptr : resource.generated_bindings_path.c_str();
		program_desc.vertex_entry = "VSMain";
		program_desc.fragment_entry = "PSMain";
		program_desc.shader_macro = nullptr;
		program_desc.source_hash = resource.combined_source_hash;
		program_desc.state = resource.program_state;
		program_desc.name = resource.program_name.c_str();
		program_desc.vertex_decl = resource.vertex_decl;

		std::vector<RHI::ShaderResourceBindingLayout> reflected_layouts{};
		RHI::ShaderParameterBlockLayout parameter_block_layout{};
		if (!m_renderer->reflect_graphics_program(
			program_desc,
			reflected_layouts,
			&parameter_block_layout,
			k_material_parameter_block_name))
		{
			if (out_error)
			{
				*out_error = "Failed to reflect shaders for material permutation.";
			}
			return nullptr;
		}
		if (!reflected_layouts.empty())
		{
			append_unique_binding_layouts(reflected_layouts, resource.binding_layout);
		}

		if (!parameter_block_layout.name.empty())
		{
			resource.parameter_block_name = k_material_parameter_block_name;
			resource.parameter_block_layout = std::move(parameter_block_layout);
		}

		auto cached_resource = std::make_unique<CachedMaterialResource>();
		cached_resource->resource = std::move(resource);
		const auto inserted = m_resources.emplace(key, std::move(cached_resource));
		const MaterialResource* result = &inserted.first->second->resource;
		HLogInfo(
			"MaterialShaderMap: created V2 permutation for material '{}' usage '{}' "
			"(host='{}', user='{}', bindings='{}', source_hash={}).",
			build_material_label(material),
			build_usage_name(usage),
			result->base_shader_path,
			result->user_shader_path,
			result->generated_bindings_path,
			result->combined_source_hash);
		return result;
	}
}
