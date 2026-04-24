#include "Function/Render/MaterialRenderProxy.h"

#include "Base/hlog.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/TextureAsset.h"
#include "Function/Render/VertexLayoutPresets.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_scene_surface_shader_path = "project/src/engine/Shaders/SceneSurfacePBR.hlsl";
		static constexpr const char* k_material_uniforms_name = "MaterialUniforms";
		static constexpr const char* k_base_color_texture_name = "BaseColorTexture";
		static constexpr const char* k_normal_texture_name = "NormalTexture";
		static constexpr const char* k_metallic_roughness_texture_name = "MetallicRoughnessTexture";
		static constexpr const char* k_emissive_texture_name = "EmissiveTexture";
		static constexpr const char* k_implicit_default_material_sampler_name = "__ImplicitDefaultSampler";
		static constexpr const char* k_implicit_default_shader_sampler_name = "ASH_DefaultSampler";

		static constexpr const char* k_sampler_count_macro_name = "ASH_PBR_SAMPLER_COUNT";
		static constexpr const char* k_sampler_name_macro_names[] = {
			"ASH_SAMPLER_0_NAME",
			"ASH_SAMPLER_1_NAME",
			"ASH_SAMPLER_2_NAME",
			"ASH_SAMPLER_3_NAME",
		};
		static constexpr const char* k_base_color_sampler_macro_name = "ASH_BASE_COLOR_SAMPLER_NAME";
		static constexpr const char* k_normal_sampler_macro_name = "ASH_NORMAL_SAMPLER_NAME";
		static constexpr const char* k_metallic_roughness_sampler_macro_name = "ASH_METALLIC_ROUGHNESS_SAMPLER_NAME";
		static constexpr const char* k_emissive_sampler_macro_name = "ASH_EMISSIVE_SAMPLER_NAME";

		static auto sanitize_identifier(std::string value) -> std::string
		{
			for (char& c : value)
			{
				if (!std::isalnum(static_cast<unsigned char>(c)))
				{
					c = '_';
				}
			}
			return value;
		}

		static auto normalize_asset_key(std::string value) -> std::string
		{
			value = std::filesystem::path(value).lexically_normal().generic_string();
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		static auto build_program_state_key(const MaterialInterface& material) -> uint64_t
		{
			uint64_t key = 0;
			key |= static_cast<uint64_t>(material.get_domain()) << 0;
			key |= static_cast<uint64_t>(material.get_blend_mode()) << 8;
			key |= static_cast<uint64_t>(material.get_shading_model()) << 16;
			key |= material.is_two_sided() ? (1ull << 24) : 0ull;
			return key;
		}

		static auto resolve_surface_input_names(const MaterialInterface& material) -> MaterialFixedPBRSurfaceInputs
		{
			if (const MaterialFixedPBRSurfaceInputs* inputs = material.get_fixed_pbr_surface_inputs())
			{
				return *inputs;
			}
			return MaterialFixedPBRSurfaceInputs{};
		}

		static auto try_get_scalar_parameter_or_default(
			const MaterialInterface& material,
			const std::string& parameter_name,
			float fallback_value) -> float
		{
			float value = fallback_value;
			if (material.try_get_scalar_parameter(parameter_name, value))
			{
				return value;
			}
			return fallback_value;
		}

		static auto try_get_vector_parameter_or_default(
			const MaterialInterface& material,
			const std::string& parameter_name,
			const glm::vec4& fallback_value) -> glm::vec4
		{
			glm::vec4 value = fallback_value;
			if (material.try_get_vector_parameter(parameter_name, value))
			{
				return value;
			}
			return fallback_value;
		}

		static auto try_get_texture_parameter_binding(
			const MaterialInterface& material,
			const std::string& parameter_name) -> MaterialTextureBinding
		{
			MaterialTextureBinding binding{};
			if (material.try_get_texture_parameter(parameter_name, binding))
			{
				return binding;
			}
			return MaterialTextureBinding{};
		}

		static auto append_shader_macro_define(
			std::string& inout_macro_string,
			std::string_view key,
			std::string_view value) -> void
		{
			if (!inout_macro_string.empty())
			{
				inout_macro_string.push_back(';');
			}
			inout_macro_string.append(key);
			inout_macro_string.push_back('=');
			inout_macro_string.append(value);
		}

		static auto join_string_list(const std::vector<std::string>& values) -> std::string
		{
			std::ostringstream stream{};
			for (size_t index = 0; index < values.size(); ++index)
			{
				if (index > 0)
				{
					stream << ", ";
				}
				stream << values[index];
			}
			return stream.str();
		}

		static auto make_program_name(
			const MaterialInterface& material,
			std::string_view shader_macro) -> std::string
		{
			const std::string material_name = sanitize_identifier(
				material.get_name().empty() ? material.get_asset_path().generic_string() : material.get_name());
			const size_t shader_variant_hash = std::hash<std::string_view>{}(shader_macro);
			return
				(material_name.empty() ? std::string("SceneSurfacePBRMaterial") : material_name) +
				"_SceneSurfacePBR_" +
				std::to_string(shader_variant_hash);
		}
	}

	MaterialRenderProxy::MaterialRenderProxy(std::shared_ptr<const MaterialInterface> material)
		: m_material(std::move(material))
	{
	}

	const std::shared_ptr<const MaterialInterface>& MaterialRenderProxy::get_material() const
	{
		return m_material;
	}

	const MaterialResource& MaterialRenderProxy::get_resource() const
	{
		return m_resource;
	}

	GraphicsProgram* MaterialRenderProxy::get_program() const
	{
		return m_program.get();
	}

	bool MaterialRenderProxy::ensure_program(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_material != nullptr);

		if (!m_resource.pass_relevance.supports_surface || m_resource.pass_relevance.is_transparent)
		{
			break;
		}

		if (!m_program)
		{
			GraphicsProgramState program_state{};
			program_state.cull_mode = m_material->is_two_sided() ? RenderCullMode::None : RenderCullMode::Back;
			program_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			program_state.depth_test = true;
			program_state.depth_write = true;
			program_state.front_face = RenderFrontFace::CounterClockwise;

			const std::string program_name = make_program_name(*m_material, m_program_shader_macro);
			m_program = renderer.create_graphics_program({
				k_scene_surface_shader_path,
				"VSMain",
				"PSMain",
				m_program_shader_macro.empty() ? nullptr : m_program_shader_macro.c_str(),
				program_state,
				program_name.c_str(),
				get_mesh_vertex_decl(),
				{},
			});
			ASH_PROCESS_ERROR(m_program != nullptr);
		}

		ASH_PROCESS_ERROR(validate_program_sampler_layout(
			m_material ? m_material->get_asset_path().generic_string() : std::string("<null>")));
		ASH_PROCESS_ERROR(bind_program_resources());
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError(
				"MaterialRenderProxy: failed to ensure graphics program for material '{}'.",
				m_material ? m_material->get_asset_path().generic_string() : std::string("<null>"));
		}
		return bResult;
	}

	bool MaterialRenderProxy::update_bindings(RenderAssetManager& asset_manager)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_material != nullptr);
		const std::string material_asset_path =
			m_material ? m_material->get_asset_path().generic_string() : std::string("<null>");

		const uint64_t material_version = m_material->get_change_version();
		if (m_material_version == material_version &&
			m_resource.material_uniforms != nullptr &&
			m_base_color_texture != nullptr &&
			m_normal_texture != nullptr &&
			m_metallic_roughness_texture != nullptr &&
			m_emissive_texture != nullptr &&
			!m_sampler_bindings.empty())
		{
			if (m_program)
			{
				if (!bind_program_resources())
				{
					HLogError(
						"MaterialRenderProxy: cached program resource rebind failed for material '{}'.",
						material_asset_path);
					ASH_PROCESS_ERROR(false);
				}
			}
			break;
		}

		const MaterialFixedPBRSurfaceInputs surface_inputs = resolve_surface_input_names(*m_material);
		const MaterialBlendMode blend_mode = m_material->get_blend_mode();
		const MaterialDomain domain = m_material->get_domain();
		const uint64_t program_state_key = build_program_state_key(*m_material);
		if (m_program_state_key != 0 && m_program_state_key != program_state_key)
		{
			m_program.reset();
		}
		m_program_state_key = program_state_key;

		m_resource.pass_relevance = {};
		m_resource.pass_relevance.domain = domain;
		m_resource.pass_relevance.is_masked = blend_mode == MaterialBlendMode::Masked;
		m_resource.pass_relevance.is_transparent = blend_mode == MaterialBlendMode::Transparent;
		m_resource.pass_relevance.supports_surface = domain == MaterialDomain::Surface;
		m_resource.pass_relevance.supports_base_pass =
			m_resource.pass_relevance.supports_surface && !m_resource.pass_relevance.is_transparent;
		m_resource.pass_relevance.supports_depth_prepass =
			m_resource.pass_relevance.supports_surface && !m_resource.pass_relevance.is_transparent;
		m_resource.blend_mode = blend_mode;
		m_resource.shading_model = m_material->get_shading_model();

		m_uniform_data = {};
		m_uniform_data.base_color_factor = try_get_vector_parameter_or_default(
			*m_material,
			surface_inputs.base_color_factor,
			glm::vec4(1.0f));
		m_uniform_data.emissive_factor_and_alpha_cutoff = try_get_vector_parameter_or_default(
			*m_material,
			surface_inputs.emissive_factor,
			glm::vec4(0.0f));
		m_uniform_data.emissive_factor_and_alpha_cutoff.w =
			blend_mode == MaterialBlendMode::Masked ? std::max(0.0f, m_material->get_alpha_cutoff()) : -1.0f;
		m_uniform_data.metallic_roughness_and_flags = {
			try_get_scalar_parameter_or_default(*m_material, surface_inputs.metallic_factor, 0.0f),
			try_get_scalar_parameter_or_default(*m_material, surface_inputs.roughness_factor, 1.0f),
			0.0f,
			0.0f
		};
		m_uniform_data.texture_flags = glm::vec4(0.0f);

		const auto resolve_texture = [&asset_manager, &material_asset_path](
			const MaterialTextureBinding& binding,
			TextureColorSpace color_space,
			TextureFallbackKind fallback_kind,
			const char* binding_name,
			float& out_has_texture) -> std::shared_ptr<TextureAsset>
		{
			const std::string& texture_path = binding.texture_path;
			if (texture_path.empty())
			{
				out_has_texture = 0.0f;
				std::shared_ptr<TextureAsset> fallback_texture = asset_manager.request_fallback_texture(fallback_kind);
				if (!fallback_texture)
				{
					HLogError(
						"MaterialRenderProxy: failed to resolve fallback texture for material '{}' binding '{}'.",
						material_asset_path,
						binding_name ? binding_name : "<unknown>");
				}
				return fallback_texture;
			}

			std::shared_ptr<TextureAsset> texture = asset_manager.request_texture_asset(texture_path, color_space, fallback_kind);
			out_has_texture =
				texture &&
				normalize_asset_key(texture->asset_path) == normalize_asset_key(texture_path) ?
				1.0f :
				0.0f;
			if (!texture)
			{
				HLogError(
					"MaterialRenderProxy: failed to resolve texture '{}' for material '{}' binding '{}'; requesting fallback.",
					texture_path,
					material_asset_path,
					binding_name ? binding_name : "<unknown>");
				std::shared_ptr<TextureAsset> fallback_texture = asset_manager.request_fallback_texture(fallback_kind);
				if (!fallback_texture)
				{
					HLogError(
						"MaterialRenderProxy: failed to resolve fallback texture for material '{}' binding '{}' after texture '{}' failed.",
						material_asset_path,
						binding_name ? binding_name : "<unknown>",
						texture_path);
				}
				return fallback_texture;
			}
			return texture;
		};

		const MaterialTextureBinding base_color_binding =
			try_get_texture_parameter_binding(*m_material, surface_inputs.base_color_texture);
		const MaterialTextureBinding normal_binding =
			try_get_texture_parameter_binding(*m_material, surface_inputs.normal_texture);
		const MaterialTextureBinding metallic_roughness_binding =
			try_get_texture_parameter_binding(*m_material, surface_inputs.metallic_roughness_texture);
		const MaterialTextureBinding emissive_binding =
			try_get_texture_parameter_binding(*m_material, surface_inputs.emissive_texture);

		m_base_color_texture = resolve_texture(
			base_color_binding,
			TextureColorSpace::SRGB,
			TextureFallbackKind::White,
			surface_inputs.base_color_texture.c_str(),
			m_uniform_data.texture_flags.x);
		m_normal_texture = resolve_texture(
			normal_binding,
			TextureColorSpace::Linear,
			TextureFallbackKind::Normal,
			surface_inputs.normal_texture.c_str(),
			m_uniform_data.texture_flags.y);
		m_metallic_roughness_texture = resolve_texture(
			metallic_roughness_binding,
			TextureColorSpace::Linear,
			TextureFallbackKind::Black,
			surface_inputs.metallic_roughness_texture.c_str(),
			m_uniform_data.texture_flags.z);
		m_emissive_texture = resolve_texture(
			emissive_binding,
			TextureColorSpace::SRGB,
			TextureFallbackKind::Black,
			surface_inputs.emissive_texture.c_str(),
			m_uniform_data.texture_flags.w);

		if (!m_base_color_texture)
		{
			HLogError("MaterialRenderProxy: base color texture binding failed for material '{}'.", material_asset_path);
		}
		if (!m_normal_texture)
		{
			HLogError("MaterialRenderProxy: normal texture binding failed for material '{}'.", material_asset_path);
		}
		if (!m_metallic_roughness_texture)
		{
			HLogError("MaterialRenderProxy: metallic-roughness texture binding failed for material '{}'.", material_asset_path);
		}
		if (!m_emissive_texture)
		{
			HLogError("MaterialRenderProxy: emissive texture binding failed for material '{}'.", material_asset_path);
		}
		ASH_PROCESS_ERROR(m_base_color_texture != nullptr);
		ASH_PROCESS_ERROR(m_normal_texture != nullptr);
		ASH_PROCESS_ERROR(m_metallic_roughness_texture != nullptr);
		ASH_PROCESS_ERROR(m_emissive_texture != nullptr);

		std::vector<ResolvedSamplerBinding> sampler_bindings{};
		sampler_bindings.reserve(4);

		const auto ensure_sampler_binding = [&](
			const MaterialTextureBinding& binding,
			bool requires_sampler,
			const char* texture_parameter_name,
			std::string& out_shader_sampler_name) -> bool
		{
			if (!requires_sampler)
			{
				out_shader_sampler_name.clear();
				return true;
			}

			if (!binding.sampler_name.empty())
			{
				for (const ResolvedSamplerBinding& existing : sampler_bindings)
				{
					if (existing.material_sampler_name == binding.sampler_name)
					{
						out_shader_sampler_name = existing.shader_sampler_name;
						return true;
					}
				}

				const MaterialSamplerDefinition* sampler_definition = find_material_sampler_definition(*m_material, binding.sampler_name);
				if (!sampler_definition)
				{
					HLogError(
						"MaterialRenderProxy: material '{}' binding '{}' references unknown sampler '{}'.",
						material_asset_path,
						texture_parameter_name ? texture_parameter_name : "<unknown>",
						binding.sampler_name);
					return false;
				}

				std::shared_ptr<RenderSampler> sampler = asset_manager.request_sampler(sampler_definition->desc);
				if (!sampler)
				{
					HLogError(
						"MaterialRenderProxy: failed to resolve sampler '{}' for material '{}' binding '{}'.",
						sampler_definition->name,
						material_asset_path,
						texture_parameter_name ? texture_parameter_name : "<unknown>");
					return false;
				}

				sampler_bindings.push_back(ResolvedSamplerBinding{
					sampler_definition->name,
					sampler_definition->shader_sampler_name,
					std::move(sampler)
				});
				out_shader_sampler_name = sampler_bindings.back().shader_sampler_name;
				return true;
			}

			for (const ResolvedSamplerBinding& existing : sampler_bindings)
			{
				if (existing.material_sampler_name == k_implicit_default_material_sampler_name)
				{
					out_shader_sampler_name = existing.shader_sampler_name;
					return true;
				}
			}

			std::shared_ptr<RenderSampler> sampler = asset_manager.request_default_sampler();
			if (!sampler)
			{
				HLogError(
					"MaterialRenderProxy: failed to resolve implicit default sampler for material '{}' binding '{}'.",
					material_asset_path,
					texture_parameter_name ? texture_parameter_name : "<unknown>");
				return false;
			}

			std::string shader_sampler_name = k_implicit_default_shader_sampler_name;
			uint32_t implicit_suffix = 0;
			const auto has_conflicting_shader_sampler_name = [&sampler_bindings](std::string_view candidate_name) -> bool
			{
				for (const ResolvedSamplerBinding& existing : sampler_bindings)
				{
					if (existing.shader_sampler_name == candidate_name)
					{
						return true;
					}
				}
				return false;
			};
			while (has_conflicting_shader_sampler_name(shader_sampler_name))
			{
				++implicit_suffix;
				shader_sampler_name = std::string(k_implicit_default_shader_sampler_name) + std::to_string(implicit_suffix);
			}

			sampler_bindings.push_back(ResolvedSamplerBinding{
				k_implicit_default_material_sampler_name,
				std::move(shader_sampler_name),
				std::move(sampler)
			});
			out_shader_sampler_name = sampler_bindings.back().shader_sampler_name;
			return true;
		};

		std::string base_color_shader_sampler_name{};
		std::string normal_shader_sampler_name{};
		std::string metallic_roughness_shader_sampler_name{};
		std::string emissive_shader_sampler_name{};

		ASH_PROCESS_ERROR(ensure_sampler_binding(
			base_color_binding,
			!base_color_binding.texture_path.empty(),
			surface_inputs.base_color_texture.c_str(),
			base_color_shader_sampler_name));
		ASH_PROCESS_ERROR(ensure_sampler_binding(
			normal_binding,
			!normal_binding.texture_path.empty(),
			surface_inputs.normal_texture.c_str(),
			normal_shader_sampler_name));
		ASH_PROCESS_ERROR(ensure_sampler_binding(
			metallic_roughness_binding,
			!metallic_roughness_binding.texture_path.empty(),
			surface_inputs.metallic_roughness_texture.c_str(),
			metallic_roughness_shader_sampler_name));
		ASH_PROCESS_ERROR(ensure_sampler_binding(
			emissive_binding,
			!emissive_binding.texture_path.empty(),
			surface_inputs.emissive_texture.c_str(),
			emissive_shader_sampler_name));

		if (sampler_bindings.empty())
		{
			std::shared_ptr<RenderSampler> sampler = asset_manager.request_default_sampler();
			if (!sampler)
			{
				HLogError(
					"MaterialRenderProxy: failed to resolve fallback default sampler for material '{}'.",
					material_asset_path);
			}
			ASH_PROCESS_ERROR(sampler != nullptr);
			sampler_bindings.push_back(ResolvedSamplerBinding{
				k_implicit_default_material_sampler_name,
				k_implicit_default_shader_sampler_name,
				std::move(sampler)
			});
		}

		std::sort(
			sampler_bindings.begin(),
			sampler_bindings.end(),
			[](const ResolvedSamplerBinding& lhs, const ResolvedSamplerBinding& rhs)
			{
				return lhs.shader_sampler_name < rhs.shader_sampler_name;
			});

		const std::string fallback_shader_sampler_name = sampler_bindings.front().shader_sampler_name;
		if (base_color_shader_sampler_name.empty())
		{
			base_color_shader_sampler_name = fallback_shader_sampler_name;
		}
		if (normal_shader_sampler_name.empty())
		{
			normal_shader_sampler_name = fallback_shader_sampler_name;
		}
		if (metallic_roughness_shader_sampler_name.empty())
		{
			metallic_roughness_shader_sampler_name = fallback_shader_sampler_name;
		}
		if (emissive_shader_sampler_name.empty())
		{
			emissive_shader_sampler_name = fallback_shader_sampler_name;
		}

		constexpr size_t k_max_surface_sampler_count = sizeof(k_sampler_name_macro_names) / sizeof(k_sampler_name_macro_names[0]);
		if (sampler_bindings.size() > k_max_surface_sampler_count)
		{
			HLogError(
				"MaterialRenderProxy: material '{}' requires {} unique samplers, but SceneSurfacePBR supports at most {}.",
				material_asset_path,
				sampler_bindings.size(),
				k_max_surface_sampler_count);
		}
		ASH_PROCESS_ERROR(sampler_bindings.size() <= k_max_surface_sampler_count);

		std::string shader_macro{};
		append_shader_macro_define(shader_macro, k_sampler_count_macro_name, std::to_string(sampler_bindings.size()));
		for (size_t sampler_index = 0; sampler_index < sampler_bindings.size(); ++sampler_index)
		{
			append_shader_macro_define(
				shader_macro,
				k_sampler_name_macro_names[sampler_index],
				sampler_bindings[sampler_index].shader_sampler_name);
		}
		append_shader_macro_define(shader_macro, k_base_color_sampler_macro_name, base_color_shader_sampler_name);
		append_shader_macro_define(shader_macro, k_normal_sampler_macro_name, normal_shader_sampler_name);
		append_shader_macro_define(shader_macro, k_metallic_roughness_sampler_macro_name, metallic_roughness_shader_sampler_name);
		append_shader_macro_define(shader_macro, k_emissive_sampler_macro_name, emissive_shader_sampler_name);

		if (m_program && m_program_shader_macro != shader_macro)
		{
			m_program.reset();
		}
		m_program_shader_macro = std::move(shader_macro);
		m_sampler_bindings = std::move(sampler_bindings);

		Renderer* renderer = asset_manager.get_renderer();
		if (!renderer)
		{
			HLogError("MaterialRenderProxy: renderer was null while updating bindings for material '{}'.", material_asset_path);
		}
		ASH_PROCESS_ERROR(renderer != nullptr);
		if (!m_resource.material_uniforms)
		{
			m_resource.material_uniforms = renderer->create_uniform_buffer({
				static_cast<uint32_t>(sizeof(MaterialUniformData)),
				false,
				&m_uniform_data,
				"SceneSurfaceMaterialUniforms"
			});
			if (!m_resource.material_uniforms)
			{
				HLogError(
					"MaterialRenderProxy: failed to create material uniform buffer for material '{}' (size={}).",
					material_asset_path,
					static_cast<uint32_t>(sizeof(MaterialUniformData)));
			}
		}
		else
		{
			if (!m_resource.material_uniforms->update(
				0u,
				static_cast<uint32_t>(sizeof(MaterialUniformData)),
				&m_uniform_data))
			{
				HLogError(
					"MaterialRenderProxy: failed to update material uniform buffer for material '{}' (size={}).",
					material_asset_path,
					static_cast<uint32_t>(sizeof(MaterialUniformData)));
				ASH_PROCESS_ERROR(false);
			}
		}
		if (!m_resource.material_uniforms)
		{
			HLogError("MaterialRenderProxy: material uniform buffer is null after creation for material '{}'.", material_asset_path);
		}
		ASH_PROCESS_ERROR(m_resource.material_uniforms != nullptr);

		m_material_version = material_version;
		if (m_program)
		{
			if (!validate_program_sampler_layout(material_asset_path) || !bind_program_resources())
			{
				HLogError(
					"MaterialRenderProxy: program resource bind failed after refresh for material '{}'.",
					material_asset_path);
				ASH_PROCESS_ERROR(false);
			}
		}

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError(
				"MaterialRenderProxy: failed to refresh bindings for material '{}'.",
				m_material ? m_material->get_asset_path().generic_string() : std::string("<null>"));
		}
		return bResult;
	}

	bool MaterialRenderProxy::bind_program_resources()
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_ERROR(m_resource.material_uniforms != nullptr);
		ASH_PROCESS_ERROR(m_base_color_texture != nullptr && m_base_color_texture->resource != nullptr);
		ASH_PROCESS_ERROR(m_normal_texture != nullptr && m_normal_texture->resource != nullptr);
		ASH_PROCESS_ERROR(m_metallic_roughness_texture != nullptr && m_metallic_roughness_texture->resource != nullptr);
		ASH_PROCESS_ERROR(m_emissive_texture != nullptr && m_emissive_texture->resource != nullptr);
		ASH_PROCESS_ERROR(!m_sampler_bindings.empty());
		ASH_PROCESS_ERROR(validate_program_sampler_layout(
			m_material ? m_material->get_asset_path().generic_string() : std::string("<null>")));
		ASH_PROCESS_ERROR(m_program->set_uniform_buffer(k_material_uniforms_name, m_resource.material_uniforms));
		ASH_PROCESS_ERROR(m_program->set_texture(k_base_color_texture_name, m_base_color_texture->resource));
		ASH_PROCESS_ERROR(m_program->set_texture(k_normal_texture_name, m_normal_texture->resource));
		ASH_PROCESS_ERROR(m_program->set_texture(k_metallic_roughness_texture_name, m_metallic_roughness_texture->resource));
		ASH_PROCESS_ERROR(m_program->set_texture(k_emissive_texture_name, m_emissive_texture->resource));

		for (const ResolvedSamplerBinding& sampler_binding : m_sampler_bindings)
		{
			if (!sampler_binding.sampler)
			{
				ASH_PROCESS_ERROR(false);
			}
			ASH_PROCESS_ERROR(m_program->set_sampler(sampler_binding.shader_sampler_name.c_str(), sampler_binding.sampler));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::validate_program_sampler_layout(const std::string& material_asset_path) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_program != nullptr);

		std::vector<std::string> reflected_sampler_names{};
		if (!m_program->get_reflected_sampler_names(reflected_sampler_names))
		{
			HLogError(
				"MaterialRenderProxy: failed to query reflected sampler names for material '{}'.",
				material_asset_path);
			ASH_PROCESS_ERROR(false);
		}

		std::vector<std::string> expected_sampler_names{};
		expected_sampler_names.reserve(m_sampler_bindings.size());
		for (const ResolvedSamplerBinding& sampler_binding : m_sampler_bindings)
		{
			expected_sampler_names.push_back(sampler_binding.shader_sampler_name);
		}
		std::sort(expected_sampler_names.begin(), expected_sampler_names.end());
		std::sort(reflected_sampler_names.begin(), reflected_sampler_names.end());

		if (expected_sampler_names != reflected_sampler_names)
		{
			HLogError(
				"MaterialRenderProxy: material '{}' sampler set does not match shader reflection. material=[{}], shader=[{}]",
				material_asset_path,
				join_string_list(expected_sampler_names),
				join_string_list(reflected_sampler_names));
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
