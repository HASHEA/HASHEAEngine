#include "Function/Render/MaterialRenderProxy.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/MaterialSystem.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/TextureAsset.h"
#include <chrono>
#include <cstring>
#include <string>
#include <utility>

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

		static auto build_material_resource_file_signature_hash(const MaterialResource& resource) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, resource.base_shader_path.c_str());
			RHI::hash_shader_file_signature(hash_value, resource.user_shader_path.c_str());
			RHI::hash_shader_file_signature(hash_value, resource.generated_bindings_path.c_str());
			return hash_value;
		}

		static auto material_resource_file_signatures_current(const MaterialResource& resource) -> bool
		{
			return resource.shader_file_signature_hash != 0 &&
				build_material_resource_file_signature_hash(resource) == resource.shader_file_signature_hash;
		}

		constexpr auto k_shader_file_signature_check_interval = std::chrono::seconds(1);
	}

	MaterialRenderProxy::MaterialRenderProxy(
		std::shared_ptr<const MaterialInterface> material,
		MaterialSystem* material_system)
		: m_material(std::move(material))
		, m_material_system(material_system)
	{
	}

	const std::shared_ptr<const MaterialInterface>& MaterialRenderProxy::get_material() const
	{
		return m_material;
	}

	const MaterialResource* MaterialRenderProxy::get_surface_staticmesh_basepass_resource() const
	{
		return m_surface_staticmesh_basepass_template != nullptr ?
			&m_surface_staticmesh_basepass_resource :
			nullptr;
	}

	const MaterialResource* MaterialRenderProxy::get_surface_staticmesh_depthonly_resource() const
	{
		return m_surface_staticmesh_depthonly_template != nullptr ?
			&m_surface_staticmesh_depthonly_resource :
			nullptr;
	}

	const MaterialResource* MaterialRenderProxy::get_surface_staticmesh_gbuffer_resource() const
	{
		return m_surface_staticmesh_gbuffer_template != nullptr ?
			&m_surface_staticmesh_gbuffer_resource :
			nullptr;
	}

	bool MaterialRenderProxy::needs_surface_staticmesh_preparation() const
	{
		if (!m_material)
		{
			return true;
		}
		if (m_v2_compile_hash != m_material->get_compile_hash() ||
			m_material_version != m_material->get_change_version())
		{
			return true;
		}
		if (!m_surface_staticmesh_basepass_template ||
			!m_surface_staticmesh_depthonly_template ||
			!m_surface_staticmesh_gbuffer_template ||
			!m_surface_staticmesh_basepass_program ||
			!m_surface_staticmesh_depthonly_program ||
			!m_surface_staticmesh_gbuffer_program)
		{
			return true;
		}
		if (!shader_file_signatures_current_throttled())
		{
			return true;
		}
		for (const auto& [texture_name, texture_asset] : m_binding_snapshot.texture_assets)
		{
			const auto version = m_binding_snapshot.texture_versions.find(texture_name);
			if (!texture_asset ||
				!texture_asset->resource ||
				version == m_binding_snapshot.texture_versions.end() ||
				version->second != texture_asset->change_version)
			{
				return true;
			}
		}
		return m_binding_snapshot.version == 0 ||
			m_bound_binding_version != m_binding_snapshot.version ||
			m_surface_staticmesh_basepass_resource.program != m_surface_staticmesh_basepass_program.get() ||
			m_surface_staticmesh_depthonly_resource.program != m_surface_staticmesh_depthonly_program.get() ||
			m_surface_staticmesh_gbuffer_resource.program != m_surface_staticmesh_gbuffer_program.get();
	}

	bool MaterialRenderProxy::prepare_surface_staticmesh(RenderAssetManager& asset_manager, Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::prepare_surface_staticmesh", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(update_bindings(asset_manager));
		ASH_PROCESS_ERROR(ensure_program(renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::ensure_program(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::ensure_program", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_material != nullptr);
		ASH_PROCESS_ERROR(ensure_v2_resource_templates());

		if (!m_surface_staticmesh_basepass_program)
		{
			ASH_PROCESS_ERROR(create_v2_program_instance(
				m_surface_staticmesh_basepass_resource,
				renderer,
				m_surface_staticmesh_basepass_program));
			m_surface_staticmesh_basepass_resource.program = m_surface_staticmesh_basepass_program.get();
		}

		if (!m_surface_staticmesh_depthonly_program)
		{
			ASH_PROCESS_ERROR(create_v2_program_instance(
				m_surface_staticmesh_depthonly_resource,
				renderer,
				m_surface_staticmesh_depthonly_program));
			m_surface_staticmesh_depthonly_resource.program = m_surface_staticmesh_depthonly_program.get();
		}

		if (!m_surface_staticmesh_gbuffer_program)
		{
			ASH_PROCESS_ERROR(create_v2_program_instance(
				m_surface_staticmesh_gbuffer_resource,
				renderer,
				m_surface_staticmesh_gbuffer_program));
			m_surface_staticmesh_gbuffer_resource.program = m_surface_staticmesh_gbuffer_program.get();
		}

		if (m_binding_snapshot.version != 0 && m_bound_binding_version != m_binding_snapshot.version)
		{
			ASH_PROCESS_ERROR(bind_v2_program_resources());
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::update_bindings(RenderAssetManager& asset_manager)
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::update_bindings", AshEngine::Profile::Color::Descriptor);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_material != nullptr);
		ASH_PROCESS_ERROR(ensure_v2_resource_templates());

		const std::string material_asset_path =
			m_material ? m_material->get_asset_path().generic_string() : std::string("<null>");
		const uint64_t material_version = m_material->get_change_version();
		bool texture_bindings_current = true;
		for (const auto& [texture_name, texture_asset] : m_binding_snapshot.texture_assets)
		{
			const auto version = m_binding_snapshot.texture_versions.find(texture_name);
			if (!texture_asset ||
				!texture_asset->resource ||
				version == m_binding_snapshot.texture_versions.end() ||
				version->second != texture_asset->change_version)
			{
				texture_bindings_current = false;
				break;
			}
		}

		if (m_material_version == material_version && m_binding_snapshot.version != 0 && texture_bindings_current)
		{
			if ((m_surface_staticmesh_basepass_program ||
					m_surface_staticmesh_depthonly_program ||
					m_surface_staticmesh_gbuffer_program) &&
				m_bound_binding_version != m_binding_snapshot.version)
			{
				ASH_PROCESS_ERROR(bind_v2_program_resources());
			}
			break;
		}

		MaterialBindingSnapshot snapshot{};
		snapshot.version = ++m_runtime_binding_version;

		const RHI::ShaderParameterBlockLayout* parameter_block_layout = nullptr;
		if (!m_surface_staticmesh_basepass_resource.parameter_block_name.empty())
		{
			parameter_block_layout = &m_surface_staticmesh_basepass_resource.parameter_block_layout;
		}
		else if (!m_surface_staticmesh_depthonly_resource.parameter_block_name.empty())
		{
			parameter_block_layout = &m_surface_staticmesh_depthonly_resource.parameter_block_layout;
		}
		else if (!m_surface_staticmesh_gbuffer_resource.parameter_block_name.empty())
		{
			parameter_block_layout = &m_surface_staticmesh_gbuffer_resource.parameter_block_layout;
		}

		if (parameter_block_layout && parameter_block_layout->byte_size > 0)
		{
			snapshot.packed_parameter_data.resize(parameter_block_layout->byte_size, 0u);
			for (const RHI::ShaderParameterMember& member : parameter_block_layout->members)
			{
				const MaterialParameterDesc* parameter_desc = find_material_parameter_desc(*m_material, member.name);
				if (!parameter_desc)
				{
					continue;
				}

				switch (parameter_desc->type)
				{
				case MaterialParameterType::Scalar:
				{
					float value = parameter_desc->default_scalar;
					(void)m_material->try_get_scalar_parameter(member.name, value);
					const size_t copy_size = std::min<size_t>(sizeof(value), member.size);
					std::memcpy(snapshot.packed_parameter_data.data() + member.offset, &value, copy_size);
					break;
				}
				case MaterialParameterType::Vector4:
				{
					glm::vec4 value = parameter_desc->default_vector;
					(void)m_material->try_get_vector_parameter(member.name, value);
					const size_t copy_size = std::min<size_t>(sizeof(value), member.size);
					std::memcpy(snapshot.packed_parameter_data.data() + member.offset, &value, copy_size);
					break;
				}
				default:
					break;
				}
			}
		}

		for (const MaterialResourceDesc& resource_desc : m_material->get_resource_descs())
		{
			MaterialTextureBinding binding{};
			if (!m_material->try_get_resource_binding(resource_desc.name, binding))
			{
				binding.texture_path = resource_desc.default_path;
				binding.sampler_name = resource_desc.sampler;
			}
			if (binding.texture_path.empty())
			{
				binding.texture_path = resource_desc.default_path;
			}
			if (binding.sampler_name.empty())
			{
				binding.sampler_name = resource_desc.sampler;
			}

			const TextureColorSpace color_space =
				resource_desc.color_space == MaterialResourceColorSpace::SRGB ?
				TextureColorSpace::SRGB :
				TextureColorSpace::Linear;
			std::shared_ptr<TextureAsset> texture_asset =
				asset_manager.request_texture_asset(binding.texture_path, color_space, TextureFallbackKind::White);
			if (!texture_asset || !texture_asset->resource)
			{
				HLogError(
					"MaterialRenderProxy: failed to resolve V2 resource texture '{}' for material '{}'.",
					resource_desc.name,
					material_asset_path);
			}
			ASH_PROCESS_ERROR(texture_asset != nullptr && texture_asset->resource != nullptr);
			snapshot.texture_assets[resource_desc.name] = texture_asset;
			snapshot.texture_versions[resource_desc.name] = texture_asset->change_version;
			snapshot.textures[resource_desc.name] = texture_asset->resource;

			if (binding.sampler_name.empty())
			{
				HLogError(
					"MaterialRenderProxy: V2 resource '{}' on material '{}' resolved to an empty sampler name.",
					resource_desc.name,
					material_asset_path);
			}
			ASH_PROCESS_ERROR(!binding.sampler_name.empty());

			const MaterialSamplerDefinition* sampler_definition =
				find_material_sampler_definition(*m_material, binding.sampler_name);
			if (!sampler_definition)
			{
				HLogError(
					"MaterialRenderProxy: V2 resource '{}' on material '{}' references unknown sampler '{}'.",
					resource_desc.name,
					material_asset_path,
					binding.sampler_name);
			}
			ASH_PROCESS_ERROR(sampler_definition != nullptr);

			const MaterialSamplerDefinition* shader_sampler_definition =
				find_material_sampler_definition(*m_material, resource_desc.sampler);
			if (!shader_sampler_definition)
			{
				HLogError(
					"MaterialRenderProxy: V2 resource '{}' on material '{}' references unknown shader sampler '{}'.",
					resource_desc.name,
					material_asset_path,
					resource_desc.sampler);
			}
			ASH_PROCESS_ERROR(shader_sampler_definition != nullptr);

			const std::string shader_sampler_name =
				shader_sampler_definition->shader_sampler_name.empty() ?
				shader_sampler_definition->name :
				shader_sampler_definition->shader_sampler_name;
			if (shader_sampler_name.empty())
			{
				HLogError(
					"MaterialRenderProxy: V2 resource '{}' on material '{}' resolved to an empty shader sampler name.",
					resource_desc.name,
					material_asset_path);
			}
			ASH_PROCESS_ERROR(!shader_sampler_name.empty());
			std::shared_ptr<RenderSampler> sampler = asset_manager.request_sampler(sampler_definition->desc);
			ASH_PROCESS_ERROR(sampler != nullptr);
			snapshot.samplers[shader_sampler_name] = sampler;
		}

		Renderer* renderer = asset_manager.get_renderer();
		ASH_PROCESS_ERROR(renderer != nullptr);
		if (parameter_block_layout && parameter_block_layout->byte_size > 0)
		{
			if (!m_v2_material_uniforms)
			{
				m_v2_material_uniforms = renderer->create_uniform_buffer({
					parameter_block_layout->byte_size,
					false,
					snapshot.packed_parameter_data.empty() ? nullptr : snapshot.packed_parameter_data.data(),
					"MaterialV2Parameters"
				});
			}
			else
			{
				ASH_PROCESS_ERROR(m_v2_material_uniforms->update(
					0u,
					parameter_block_layout->byte_size,
					snapshot.packed_parameter_data.empty() ? nullptr : snapshot.packed_parameter_data.data()));
			}
			ASH_PROCESS_ERROR(m_v2_material_uniforms != nullptr);
		}
		else
		{
			m_v2_material_uniforms.reset();
		}

		m_binding_snapshot = std::move(snapshot);
		m_material_version = material_version;
		ASH_PROFILE_PLOT("Material/TextureBindings", static_cast<int64_t>(m_binding_snapshot.textures.size()));
		ASH_PROFILE_PLOT("Material/SamplerBindings", static_cast<int64_t>(m_binding_snapshot.samplers.size()));
		HLogInfo(
			"MaterialRenderProxy: refreshed V2 bindings for material '{}' "
			"(binding_version={}, parameter_bytes={}, textures={}, samplers={}).",
			material_asset_path,
			m_binding_snapshot.version,
			m_binding_snapshot.packed_parameter_data.size(),
			m_binding_snapshot.textures.size(),
			m_binding_snapshot.samplers.size());
		if (m_surface_staticmesh_basepass_program ||
			m_surface_staticmesh_depthonly_program ||
			m_surface_staticmesh_gbuffer_program)
		{
			ASH_PROCESS_ERROR(bind_v2_program_resources());
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::ensure_v2_resource_templates()
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::ensure_v2_resource_templates", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_material != nullptr);
		ASH_PROCESS_ERROR(m_material_system != nullptr);

		const uint64_t compile_hash = m_material->get_compile_hash();
		if (m_surface_staticmesh_basepass_template != nullptr &&
			m_surface_staticmesh_depthonly_template != nullptr &&
			m_surface_staticmesh_gbuffer_template != nullptr &&
			m_v2_compile_hash == compile_hash &&
			shader_file_signatures_current_throttled())
		{
			m_surface_staticmesh_basepass_resource.program = m_surface_staticmesh_basepass_program.get();
			m_surface_staticmesh_depthonly_resource.program = m_surface_staticmesh_depthonly_program.get();
			m_surface_staticmesh_gbuffer_resource.program = m_surface_staticmesh_gbuffer_program.get();
			break;
		}

		const uint64_t capability_mask =
			static_cast<uint64_t>(MaterialCapability::VertexColor) |
			static_cast<uint64_t>(MaterialCapability::UV1);
		const MaterialUsageDesc basepass_usage{
			MaterialDomain::Surface,
			EngineShaderFamily::SurfaceStaticMesh,
			PassFamily::BasePass,
			capability_mask
		};
		const MaterialUsageDesc depthonly_usage{
			MaterialDomain::Surface,
			EngineShaderFamily::SurfaceStaticMesh,
			PassFamily::DepthOnly,
			capability_mask
		};
		const MaterialUsageDesc gbuffer_usage{
			MaterialDomain::Surface,
			EngineShaderFamily::SurfaceStaticMesh,
			PassFamily::GBuffer,
			capability_mask
		};

		std::string error{};
		const MaterialResource* basepass_template =
			m_material_system->get_or_create_resource(*m_material, basepass_usage, &error);
		if (!basepass_template)
		{
			HLogError(
				"MaterialRenderProxy: failed to resolve V2 basepass template for material '{}': {}",
				m_material->get_asset_path().generic_string(),
				error.empty() ? std::string("unknown error") : error);
		}
		ASH_PROCESS_ERROR(basepass_template != nullptr);

		error.clear();
		const MaterialResource* depthonly_template =
			m_material_system->get_or_create_resource(*m_material, depthonly_usage, &error);
		if (!depthonly_template)
		{
			HLogError(
				"MaterialRenderProxy: failed to resolve V2 depth-only template for material '{}': {}",
				m_material->get_asset_path().generic_string(),
				error.empty() ? std::string("unknown error") : error);
		}
		ASH_PROCESS_ERROR(depthonly_template != nullptr);

		error.clear();
		const MaterialResource* gbuffer_template =
			m_material_system->get_or_create_resource(*m_material, gbuffer_usage, &error);
		if (!gbuffer_template)
		{
			HLogError(
				"MaterialRenderProxy: failed to resolve V2 GBuffer template for material '{}': {}",
				m_material->get_asset_path().generic_string(),
				error.empty() ? std::string("unknown error") : error);
		}
		ASH_PROCESS_ERROR(gbuffer_template != nullptr);

		const bool basepass_changed =
			m_surface_staticmesh_basepass_template != basepass_template ||
			m_surface_staticmesh_basepass_resource.combined_source_hash != basepass_template->combined_source_hash;
		const bool depthonly_changed =
			m_surface_staticmesh_depthonly_template != depthonly_template ||
			m_surface_staticmesh_depthonly_resource.combined_source_hash != depthonly_template->combined_source_hash;
		const bool gbuffer_changed =
			m_surface_staticmesh_gbuffer_template != gbuffer_template ||
			m_surface_staticmesh_gbuffer_resource.combined_source_hash != gbuffer_template->combined_source_hash;

		if (basepass_changed)
		{
			m_surface_staticmesh_basepass_program.reset();
		}
		if (depthonly_changed)
		{
			m_surface_staticmesh_depthonly_program.reset();
		}
		if (gbuffer_changed)
		{
			m_surface_staticmesh_gbuffer_program.reset();
		}
		if (basepass_changed || depthonly_changed || gbuffer_changed)
		{
			m_v2_material_uniforms.reset();
			m_bound_binding_version = 0;
		}

		m_surface_staticmesh_basepass_template = basepass_template;
		m_surface_staticmesh_depthonly_template = depthonly_template;
		m_surface_staticmesh_gbuffer_template = gbuffer_template;
		m_surface_staticmesh_basepass_resource = *basepass_template;
		m_surface_staticmesh_depthonly_resource = *depthonly_template;
		m_surface_staticmesh_gbuffer_resource = *gbuffer_template;
		m_surface_staticmesh_basepass_resource.program = m_surface_staticmesh_basepass_program.get();
		m_surface_staticmesh_depthonly_resource.program = m_surface_staticmesh_depthonly_program.get();
		m_surface_staticmesh_gbuffer_resource.program = m_surface_staticmesh_gbuffer_program.get();
		m_surface_staticmesh_basepass_resource.material_uniforms.reset();
		m_surface_staticmesh_depthonly_resource.material_uniforms.reset();
		m_surface_staticmesh_gbuffer_resource.material_uniforms.reset();
		m_v2_compile_hash = compile_hash;
		mark_shader_file_signatures_current();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::shader_file_signatures_current_throttled() const
	{
		if (m_surface_staticmesh_basepass_template == nullptr ||
			m_surface_staticmesh_depthonly_template == nullptr ||
			m_surface_staticmesh_gbuffer_template == nullptr ||
			m_surface_staticmesh_basepass_resource.shader_file_signature_hash == 0 ||
			m_surface_staticmesh_depthonly_resource.shader_file_signature_hash == 0 ||
			m_surface_staticmesh_gbuffer_resource.shader_file_signature_hash == 0)
		{
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		if (m_shader_file_signature_status_valid && now < m_next_shader_file_signature_check)
		{
			return m_shader_file_signatures_current;
		}

		m_shader_file_signatures_current =
			material_resource_file_signatures_current(m_surface_staticmesh_basepass_resource) &&
			material_resource_file_signatures_current(m_surface_staticmesh_depthonly_resource) &&
			material_resource_file_signatures_current(m_surface_staticmesh_gbuffer_resource);
		m_shader_file_signature_status_valid = true;
		m_next_shader_file_signature_check = now + k_shader_file_signature_check_interval;
		return m_shader_file_signatures_current;
	}

	void MaterialRenderProxy::mark_shader_file_signatures_current() const
	{
		m_shader_file_signatures_current = true;
		m_shader_file_signature_status_valid = true;
		m_next_shader_file_signature_check =
			std::chrono::steady_clock::now() + k_shader_file_signature_check_interval;
	}

	bool MaterialRenderProxy::create_v2_program_instance(
		const MaterialResource& template_resource,
		Renderer& renderer,
		std::unique_ptr<GraphicsProgram>& out_program) const
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::create_v2_program_instance", AshEngine::Profile::Color::Pipeline);
		ASH_PROFILE_SCOPE_TEXT(template_resource.program_name.c_str(), template_resource.program_name.size());
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		GraphicsProgramDesc program_desc{};
		program_desc.shader_path = template_resource.base_shader_path.c_str();
		program_desc.base_shader_path = template_resource.base_shader_path.c_str();
		program_desc.user_shader_path =
			template_resource.user_shader_path.empty() ? nullptr : template_resource.user_shader_path.c_str();
		program_desc.generated_bindings_path =
			template_resource.generated_bindings_path.empty() ? nullptr : template_resource.generated_bindings_path.c_str();
		program_desc.vertex_entry = "VSMain";
		program_desc.fragment_entry = "PSMain";
		program_desc.shader_macro =
			template_resource.shader_macro.empty() ? nullptr : template_resource.shader_macro.c_str();
		program_desc.source_hash = template_resource.combined_source_hash;
		program_desc.state = template_resource.program_state;
		program_desc.name = template_resource.program_name.c_str();
		program_desc.vertex_decl = template_resource.vertex_decl;
		out_program = renderer.create_graphics_program(program_desc);
		ASH_PROCESS_ERROR(out_program != nullptr);
		HLogInfo(
			"MaterialRenderProxy: created V2 program instance '{}' for material '{}' usage '{}' "
			"(source_hash={}, host='{}', user='{}', bindings='{}').",
			template_resource.program_name,
			m_material ? build_material_label(*m_material) : std::string("<null-material>"),
			build_usage_name(template_resource.usage),
			template_resource.combined_source_hash,
			template_resource.base_shader_path,
			template_resource.user_shader_path,
			template_resource.generated_bindings_path);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool MaterialRenderProxy::bind_v2_program_resources()
	{
		ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::bind_v2_program_resources", AshEngine::Profile::Color::Descriptor);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		const auto bind_program_resources = [&](MaterialResource& resource, std::unique_ptr<GraphicsProgram>& program) -> bool
		{
			ASH_PROFILE_SCOPE_NC("MaterialRenderProxy::BindProgramResourceSet", AshEngine::Profile::Color::Descriptor);
			ASH_PROFILE_SCOPE_TEXT(resource.program_name.c_str(), resource.program_name.size());
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(resource.binding_layout.size()));
			ASH_PROCESS_GUARD_RETURN(bool, bind_result, true, false);
			if (!program)
			{
				break;
			}

			resource.program = program.get();
			if (!resource.parameter_block_name.empty())
			{
				ASH_PROCESS_ERROR(m_v2_material_uniforms != nullptr);
				ASH_PROCESS_ERROR(program->set_uniform_buffer(resource.parameter_block_name.c_str(), m_v2_material_uniforms));
			}

			for (const MaterialBindingLayoutEntry& binding_entry : resource.binding_layout)
			{
				switch (binding_entry.type)
				{
				case RHI::ShaderResourceBindingType::ConstantBuffer:
					if (binding_entry.name == resource.parameter_block_name)
					{
						ASH_PROCESS_ERROR(program->set_uniform_buffer(binding_entry.name.c_str(), m_v2_material_uniforms));
					}
					break;
				case RHI::ShaderResourceBindingType::ShaderResource:
				case RHI::ShaderResourceBindingType::CombinedImageSampler:
				{
					const auto found_texture = m_binding_snapshot.textures.find(binding_entry.name);
					ASH_PROCESS_ERROR(found_texture != m_binding_snapshot.textures.end());
					ASH_PROCESS_ERROR(found_texture->second != nullptr);
					ASH_PROCESS_ERROR(program->set_texture(binding_entry.name.c_str(), found_texture->second));
					break;
				}
				case RHI::ShaderResourceBindingType::Sampler:
				{
					const auto found_sampler = m_binding_snapshot.samplers.find(binding_entry.name);
					if (found_sampler == m_binding_snapshot.samplers.end())
					{
						HLogError(
							"MaterialRenderProxy: material '{}' is missing sampler binding '{}' required by usage '{}'.",
							m_material ? build_material_label(*m_material) : std::string("<null-material>"),
							binding_entry.name,
							build_usage_name(resource.usage));
					}
					ASH_PROCESS_ERROR(found_sampler != m_binding_snapshot.samplers.end());
					ASH_PROCESS_ERROR(found_sampler->second != nullptr);
					ASH_PROCESS_ERROR(program->set_sampler(binding_entry.name.c_str(), found_sampler->second));
					break;
				}
				default:
					break;
				}
			}
			ASH_PROCESS_GUARD_RETURN_END(bind_result, false);
		};

		ASH_PROCESS_ERROR(bind_program_resources(m_surface_staticmesh_basepass_resource, m_surface_staticmesh_basepass_program));
		ASH_PROCESS_ERROR(bind_program_resources(m_surface_staticmesh_depthonly_resource, m_surface_staticmesh_depthonly_program));
		ASH_PROCESS_ERROR(bind_program_resources(m_surface_staticmesh_gbuffer_resource, m_surface_staticmesh_gbuffer_program));
		m_bound_binding_version = m_binding_snapshot.version;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
