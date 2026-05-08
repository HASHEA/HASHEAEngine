#include "Function/Render/RenderAssetManager.h"

#include "Base/hlog.h"
#include "Base/hthreading.h"
#include "Function/Render/Material.h"
#include "Function/Render/MaterialRenderProxy.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/VertexLayoutPresets.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>

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

		static auto normalize_asset_key(std::string_view asset_path) -> std::string
		{
			return to_lower_copy(std::filesystem::path(asset_path).lexically_normal().generic_string());
		}

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

		static auto sampler_address_mode_to_string(RenderSamplerAddressMode mode) -> const char*
		{
			switch (mode)
			{
			case RenderSamplerAddressMode::MirroredRepeat:
				return "MirroredRepeat";
			case RenderSamplerAddressMode::ClampToEdge:
				return "ClampToEdge";
			case RenderSamplerAddressMode::ClampToBorder:
				return "ClampToBorder";
			case RenderSamplerAddressMode::MirrorClampToEdge:
				return "MirrorClampToEdge";
			case RenderSamplerAddressMode::Repeat:
			default:
				return "Repeat";
			}
		}

		static auto sampler_filter_to_string(RenderSamplerFilter filter) -> const char*
		{
			switch (filter)
			{
			case RenderSamplerFilter::Nearest:
				return "Nearest";
			case RenderSamplerFilter::Linear:
			default:
				return "Linear";
			}
		}

		static auto build_generated_material_name(const std::string& asset_path, uint32_t material_slot, const MaterialSlot& material_slot_data) -> std::string
		{
			std::string model_name = sanitize_identifier(std::filesystem::path(asset_path).stem().string());
			if (model_name.empty())
			{
				model_name = "Model";
			}

			std::string slot_name = sanitize_identifier(material_slot_data.name);
			if (slot_name.empty())
			{
				slot_name = "Slot" + std::to_string(material_slot);
			}

			return "MI_" + model_name + "_" + slot_name;
		}

		static auto find_model_default_material_reference(const Model& model, uint32_t material_slot) -> const ModelMaterialReference*
		{
			for (const ModelMaterialReference& reference : model.default_materials)
			{
				if (reference.material_slot == material_slot)
				{
					return &reference;
				}
			}
			return nullptr;
		}

		static auto find_material_override(
			const std::vector<MeshMaterialOverride>& overrides,
			uint32_t material_slot) -> const MeshMaterialOverride*
		{
			for (const MeshMaterialOverride& material_override : overrides)
			{
				if (material_override.material_slot == material_slot)
				{
					return &material_override;
				}
			}
			return nullptr;
		}

		static auto resolve_texture_asset_path(AssetDatabase* asset_database, const std::string& asset_path) -> std::filesystem::path
		{
			std::filesystem::path path = std::filesystem::path(asset_path).lexically_normal();
			if (!path.is_absolute() && asset_database && !asset_database->get_root_path().empty())
			{
				path = (asset_database->get_root_path() / path).lexically_normal();
			}
			return path;
		}

		static auto choose_default_material_fallback_path(const std::string& asset_path) -> const char*
		{
			(void)asset_path;
			return k_builtin_default_surface_material_path;
		}

		static auto is_bindable_material_instance(const std::shared_ptr<const MaterialInterface>& material) -> bool
		{
			return material != nullptr && material->is_material_instance();
		}

		static auto require_render_thread_for_gpu_asset_work(const char* operation) -> bool
		{
			if (is_in_render_thread())
			{
				return true;
			}

			HLogError(
				"RenderAssetManager: '{}' must run on the render thread, current thread role is '{}'.",
				operation ? operation : "GPU asset work",
				thread_role_to_string(get_current_thread_role()));
			return false;
		}
	}

	void RenderAssetManager::initialize(AssetDatabase* asset_database, Renderer* renderer)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_asset_database = asset_database;
		m_renderer = renderer;
		if (!m_material_system.initialize(renderer))
		{
			HLogError("RenderAssetManager: failed to initialize MaterialSystem.");
		}
	}

	void RenderAssetManager::shutdown()
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_static_mesh_assets.clear();
		m_material_assets.clear();
		m_material_proxies.clear();
		m_texture_assets.clear();
		m_sampler_pool.clear();
		m_failed_texture_requests.clear();
		m_logged_material_warnings.clear();
		m_logged_texture_warnings.clear();
		m_default_white_texture.reset();
		m_default_normal_texture.reset();
		m_default_black_texture.reset();
		m_material_system.shutdown();
		m_asset_database = nullptr;
		m_renderer = nullptr;
	}

	std::shared_ptr<StaticMeshRenderAsset> RenderAssetManager::request_static_mesh_asset(const std::string& asset_path, uint32_t mesh_index)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<StaticMeshRenderAsset>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_asset_database);
		ASH_PROCESS_ERROR(!asset_path.empty());

		const std::string key = make_static_mesh_key(asset_path, mesh_index);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_static_mesh_assets.find(key);
			if (found != m_static_mesh_assets.end())
			{
				result = found->second;
				break;
			}
		}

		result = std::make_shared<StaticMeshRenderAsset>();
		result->asset_path = asset_path;
		result->mesh_index = mesh_index;
		result->state = StaticMeshRenderAssetState::Loading;
		ASH_PROCESS_ERROR(populate_cpu_mesh_asset(result));

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_static_mesh_assets[key] = result;
		}
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<const MaterialInterface> RenderAssetManager::request_material_asset(const std::string& asset_path)
	{
		return request_material_asset_internal(asset_path, true);
	}

	std::shared_ptr<MaterialRenderProxy> RenderAssetManager::request_material_render_proxy(const std::shared_ptr<const MaterialInterface>& material)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<MaterialRenderProxy>, result, nullptr, nullptr);
		std::shared_ptr<const MaterialInterface> resolved_material = material;
		if (!resolved_material)
		{
			resolved_material = request_material_asset(k_builtin_default_surface_material_path);
		}
		ASH_PROCESS_ERROR(resolved_material != nullptr);

		const std::string key = make_material_proxy_key(*resolved_material);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_material_proxies.find(key);
			if (found != m_material_proxies.end() &&
				found->second != nullptr &&
				found->second->get_material().get() == resolved_material.get())
			{
				result = found->second;
				break;
			}
		}

		result = std::make_shared<MaterialRenderProxy>(resolved_material, &m_material_system);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_material_proxies[key] = result;
		}
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<TextureAsset> RenderAssetManager::request_texture_asset(
		const std::string& asset_path,
		TextureColorSpace color_space,
		TextureFallbackKind fallback_kind)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<TextureAsset>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(require_render_thread_for_gpu_asset_work("request_texture_asset"));

		if (asset_path.empty())
		{
			result = get_fallback_texture(fallback_kind);
			break;
		}

		const std::string key = make_texture_key(asset_path, color_space);
		bool has_cached_failure = false;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_texture_assets.find(key);
			if (found != m_texture_assets.end())
			{
				result = found->second;
				break;
			}
			if (m_failed_texture_requests.find(key) != m_failed_texture_requests.end())
			{
				has_cached_failure = true;
			}
		}
		if (has_cached_failure)
		{
			result = get_fallback_texture(fallback_kind);
			break;
		}

		TextureSourceData source{};
		std::string error{};
		const std::filesystem::path texture_path = resolve_texture_asset_path(m_asset_database, asset_path);
		if (!decode_texture_source_from_file(texture_path, color_space, source, &error))
		{
			log_texture_warning_once(
				key,
				"RenderAssetManager: failed to decode texture '" + asset_path + "': " + (error.empty() ? std::string("unsupported or invalid texture.") : error));
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_failed_texture_requests.insert(key);
			}
			result = get_fallback_texture(fallback_kind);
			break;
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_texture_assets.find(key);
			if (found != m_texture_assets.end())
			{
				result = found->second;
				break;
			}
		}

		auto texture_asset = std::make_shared<TextureAsset>();
		texture_asset->asset_path = asset_path;
		texture_asset->width = source.width;
		texture_asset->height = source.height;
		texture_asset->format = source.format;
		texture_asset->color_space = source.color_space;
		texture_asset->resource = m_renderer->create_texture_2d({
			static_cast<uint16_t>(source.width),
			static_cast<uint16_t>(source.height),
			source.format,
			source.pixel_data.data(),
			source.row_pitch,
			source.mip_level_count,
			source.color_space == TextureColorSpace::SRGB,
			asset_path.c_str()
		});
		if (!texture_asset->resource)
		{
			log_texture_warning_once(
				key,
				"RenderAssetManager: failed to create GPU texture for '" + asset_path + "'.");
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_failed_texture_requests.insert(key);
			}
			result = get_fallback_texture(fallback_kind);
			break;
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto [it, inserted] = m_texture_assets.emplace(key, texture_asset);
			m_failed_texture_requests.erase(key);
			if (!inserted && it->second)
			{
				texture_asset = it->second;
			}
		}
		result = texture_asset;
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<TextureAsset> RenderAssetManager::request_fallback_texture(TextureFallbackKind fallback_kind)
	{
		return get_fallback_texture(fallback_kind);
	}

	std::shared_ptr<RenderSampler> RenderAssetManager::request_sampler(const RenderSamplerDesc& desc)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<RenderSampler>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(require_render_thread_for_gpu_asset_work("request_sampler"));

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_sampler_pool.find(desc);
			if (found != m_sampler_pool.end())
			{
				result = found->second;
				break;
			}
		}

		const std::string debug_name = make_sampler_debug_name(desc);
		result = m_renderer->create_sampler(desc, debug_name.c_str());
		ASH_PROCESS_ERROR(result != nullptr);

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			auto [it, inserted] = m_sampler_pool.emplace(desc, result);
			if (!inserted)
			{
				result = it->second;
			}
		}

		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<RenderSampler> RenderAssetManager::request_default_sampler()
	{
		return request_sampler(RenderSamplerDesc{});
	}

	bool RenderAssetManager::resolve_static_mesh_primitive_sections(
		const StaticMeshRenderAsset& render_asset,
		const std::vector<MeshMaterialOverride>& material_overrides,
		std::vector<ResolvedStaticMeshSection>& out_sections)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_sections.clear();
		out_sections.reserve(render_asset.sections.size());

		for (const StaticMeshRenderSection& source_section : render_asset.sections)
		{
			ResolvedStaticMeshSection resolved_section{};
			resolved_section.first_index = source_section.first_index;
			resolved_section.index_count = source_section.index_count;
			resolved_section.material_slot = source_section.material_slot;
			resolved_section.topology = source_section.topology;
			resolved_section.material = source_section.material;

			if (const MeshMaterialOverride* material_override = find_material_override(material_overrides, source_section.material_slot))
			{
				if (!material_override->material_path.empty())
				{
					if (std::shared_ptr<const MaterialInterface> override_material = request_material_asset_internal(material_override->material_path, false))
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
					else
					{
						log_material_warning_once(
							make_material_key(material_override->material_path) + "#override",
							"RenderAssetManager: failed to load mesh material override '" + material_override->material_path + "', keeping section default material.");
					}
				}
			}

			if (!resolved_section.material)
			{
				resolved_section.material = request_material_asset(k_builtin_default_surface_material_path);
			}
			ASH_PROCESS_ERROR(resolved_section.material != nullptr);
			// RenderScene rebuild can run on the logic thread. Keep this stage CPU-only;
			// ScenePresentation submit prepares/caches the GPU material proxy on the render thread.
			resolved_section.material_proxy = nullptr;
			out_sections.push_back(std::move(resolved_section));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderAssetManager::finalize_pending_static_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);

		std::scoped_lock<std::mutex> asset_lock(asset->mutex);
		if (asset->state == StaticMeshRenderAssetState::GpuReady)
		{
			break;
		}
		ASH_PROCESS_ERROR(asset->state == StaticMeshRenderAssetState::CpuReady);
		ASH_PROCESS_ERROR(create_gpu_mesh_resource(asset));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void RenderAssetManager::finalize_pending_assets()
	{
		std::vector<std::shared_ptr<StaticMeshRenderAsset>> assets{};
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			assets.reserve(m_static_mesh_assets.size());
			for (const auto& [key, asset] : m_static_mesh_assets)
			{
				(void)key;
				assets.push_back(asset);
			}
		}

		for (const std::shared_ptr<StaticMeshRenderAsset>& asset : assets)
		{
			if (!asset)
			{
				continue;
			}
			finalize_pending_static_mesh_asset(asset);
		}
	}

	AssetDatabase* RenderAssetManager::get_asset_database() const
	{
		return m_asset_database;
	}

	Renderer* RenderAssetManager::get_renderer() const
	{
		return m_renderer;
	}

	MaterialSystem* RenderAssetManager::get_material_system()
	{
		return &m_material_system;
	}

	const MaterialSystem* RenderAssetManager::get_material_system() const
	{
		return &m_material_system;
	}

	std::string RenderAssetManager::make_static_mesh_key(const std::string& asset_path, uint32_t mesh_index)
	{
		return normalize_asset_key(asset_path) + "#" + std::to_string(mesh_index);
	}

	std::string RenderAssetManager::make_material_key(const std::string& asset_path)
	{
		return normalize_asset_key(asset_path);
	}

	std::string RenderAssetManager::make_material_proxy_key(const MaterialInterface& material)
	{
		const std::string asset_path = material.get_asset_path().generic_string();
		if (!asset_path.empty())
		{
			return make_material_key(asset_path);
		}

		return "__material_proxy__/ptr=" + std::to_string(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&material)));
	}

	std::string RenderAssetManager::make_generated_material_key(const std::string& asset_path, uint32_t material_slot)
	{
		return "__generated__/materials/" + normalize_asset_key(asset_path) + "#slot=" + std::to_string(material_slot) + ".AshMatIns";
	}

	std::string RenderAssetManager::make_texture_key(const std::string& asset_path, TextureColorSpace color_space)
	{
		return normalize_asset_key(asset_path) + (color_space == TextureColorSpace::SRGB ? "#srgb" : "#linear");
	}

	std::string RenderAssetManager::make_sampler_debug_name(const RenderSamplerDesc& desc)
	{
		return "Engine/Samplers/U=" + std::string(sampler_address_mode_to_string(desc.address_u)) +
			"_V=" + sampler_address_mode_to_string(desc.address_v) +
			"_W=" + sampler_address_mode_to_string(desc.address_w) +
			"_Min=" + sampler_filter_to_string(desc.min_filter) +
			"_Mag=" + sampler_filter_to_string(desc.mag_filter) +
			"_Mip=" + sampler_filter_to_string(desc.mip_filter);
	}

	std::shared_ptr<const MaterialInterface> RenderAssetManager::request_material_asset_internal(const std::string& asset_path, bool allow_default_fallback)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<const MaterialInterface>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_asset_database);
		ASH_PROCESS_ERROR(!asset_path.empty());

		const std::string cache_key = make_material_key(asset_path);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_material_assets.find(cache_key);
			if (found != m_material_assets.end())
			{
				result = found->second;
				break;
			}
		}

		std::shared_ptr<const MaterialInterface> material{};
		if (m_asset_database->load_material_by_path(asset_path, material) && material)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_material_assets[cache_key] = material;
			result = material;
			break;
		}

		if (!allow_default_fallback)
		{
			ASH_PROCESS_ERROR(false);
		}

		const char* fallback_material_path = choose_default_material_fallback_path(asset_path);
		log_material_warning_once(
			cache_key,
			"RenderAssetManager: failed to load material '" + asset_path + "', falling back to '" + std::string(fallback_material_path) + "'.");
		ASH_PROCESS_ERROR(m_asset_database->load_material_by_path(fallback_material_path, material));
		ASH_PROCESS_ERROR(material != nullptr);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_material_assets[cache_key] = material;
		}
		result = material;
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<const MaterialInterface> RenderAssetManager::request_generated_material_asset(
		const std::string& asset_path,
		uint32_t material_slot,
		const MaterialSlot& material_slot_data)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<const MaterialInterface>, result, nullptr, nullptr);

		const std::string generated_key = make_generated_material_key(asset_path, material_slot);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_material_assets.find(generated_key);
			if (found != m_material_assets.end())
			{
				result = found->second;
				break;
			}
		}

		std::shared_ptr<const MaterialInterface> parent_material = request_material_asset_internal(k_builtin_surface_pbr_material_path, false);
		if (!parent_material)
		{
			parent_material = make_builtin_material(k_builtin_surface_pbr_material_path);
		}
		ASH_PROCESS_ERROR(parent_material != nullptr);

		auto material_instance = std::make_shared<MaterialInstance>();
		material_instance->set_name(build_generated_material_name(asset_path, material_slot, material_slot_data));
		material_instance->set_asset_path(std::filesystem::path(generated_key));
		material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
		material_instance->set_parent(parent_material);
		material_instance->set_sampler_definitions(material_slot_data.sampler_definitions);
		material_instance->set_vector_override("BaseColorFactor", material_slot_data.base_color_factor);
		material_instance->set_scalar_override("Metallic", material_slot_data.metallic_factor);
		material_instance->set_scalar_override("Roughness", material_slot_data.roughness_factor);
		material_instance->set_vector_override("EmissiveColor", glm::vec4(material_slot_data.emissive_factor, 1.0f));
		if (!material_slot_data.base_color_texture.texture_path.empty() || !material_slot_data.base_color_texture.sampler_name.empty())
		{
			material_instance->set_resource_override("BaseColorTexture", material_slot_data.base_color_texture);
		}
		if (!material_slot_data.normal_texture.texture_path.empty() || !material_slot_data.normal_texture.sampler_name.empty())
		{
			material_instance->set_resource_override("NormalTexture", material_slot_data.normal_texture);
		}
		if (!material_slot_data.metallic_roughness_texture.texture_path.empty() || !material_slot_data.metallic_roughness_texture.sampler_name.empty())
		{
			material_instance->set_resource_override("MetallicRoughnessTexture", material_slot_data.metallic_roughness_texture);
		}
		if (!material_slot_data.emissive_texture.texture_path.empty() || !material_slot_data.emissive_texture.sampler_name.empty())
		{
			material_instance->set_resource_override("EmissiveTexture", material_slot_data.emissive_texture);
		}
		material_instance->reset_change_version();

		result = material_instance;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_material_assets[generated_key] = result;
		}
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<const MaterialInterface> RenderAssetManager::resolve_default_section_material(const std::string& asset_path, const Model& model, uint32_t material_slot)
	{
		if (const ModelMaterialReference* default_reference = find_model_default_material_reference(model, material_slot))
		{
			if (!default_reference->material_path.empty())
			{
				if (std::shared_ptr<const MaterialInterface> explicit_material = request_material_asset_internal(default_reference->material_path, false))
				{
					if (is_bindable_material_instance(explicit_material))
					{
						return explicit_material;
					}

					HLogError(
						"RenderAssetManager: material '{}' resolved to base material 'Material'. "
						"Only '.AshMatIns' material instances can be assigned directly to mesh sections. "
						"Falling back to generated or default material.",
						default_reference->material_path);
				}
			}
		}

		if (const MaterialSlot* source_slot = get_model_material_slot_by_index(model, material_slot))
		{
			if (std::shared_ptr<const MaterialInterface> generated_material = request_generated_material_asset(asset_path, material_slot, *source_slot))
			{
				return generated_material;
			}
		}

		return request_material_asset(k_builtin_default_surface_material_path);
	}

	std::shared_ptr<TextureAsset> RenderAssetManager::get_fallback_texture(TextureFallbackKind fallback_kind)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<TextureAsset>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer);

		static const std::array<uint8_t, 4> k_white_pixel{ 255u, 255u, 255u, 255u };
		static const std::array<uint8_t, 4> k_normal_pixel{ 128u, 128u, 255u, 255u };
		static const std::array<uint8_t, 4> k_black_pixel{ 0u, 0u, 0u, 255u };

		std::scoped_lock<std::mutex> lock(m_mutex);
		switch (fallback_kind)
		{
		case TextureFallbackKind::Normal:
			if (!m_default_normal_texture)
			{
				m_default_normal_texture = create_fallback_texture(
					"Engine/Textures/T_DefaultNormal",
					TextureColorSpace::Linear,
					k_normal_pixel.data(),
					RenderTextureFormat::RGBA8_UNORM,
					4u);
			}
			result = m_default_normal_texture;
			break;
		case TextureFallbackKind::Black:
			if (!m_default_black_texture)
			{
				m_default_black_texture = create_fallback_texture(
					"Engine/Textures/T_DefaultBlack",
					TextureColorSpace::SRGB,
					k_black_pixel.data(),
					RenderTextureFormat::RGBA8_SRGB,
					4u);
			}
			result = m_default_black_texture;
			break;
		case TextureFallbackKind::White:
		default:
			if (!m_default_white_texture)
			{
				m_default_white_texture = create_fallback_texture(
					"Engine/Textures/T_DefaultWhite",
					TextureColorSpace::SRGB,
					k_white_pixel.data(),
					RenderTextureFormat::RGBA8_SRGB,
					4u);
			}
			result = m_default_white_texture;
			break;
		}

		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	std::shared_ptr<TextureAsset> RenderAssetManager::create_fallback_texture(
		const char* debug_name,
		TextureColorSpace color_space,
		const void* pixel_data,
		RenderTextureFormat format,
		uint32_t row_pitch)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<TextureAsset>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(pixel_data != nullptr);

		auto texture_asset = std::make_shared<TextureAsset>();
		texture_asset->asset_path = debug_name ? debug_name : "FallbackTexture";
		texture_asset->width = 1;
		texture_asset->height = 1;
		texture_asset->format = format;
		texture_asset->color_space = color_space;
		texture_asset->resource = m_renderer->create_texture_2d({
			1u,
			1u,
			format,
			pixel_data,
			row_pitch,
			1,
			color_space == TextureColorSpace::SRGB,
			debug_name
		});
		ASH_PROCESS_ERROR(texture_asset->resource != nullptr);
		result = texture_asset;
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	void RenderAssetManager::log_material_warning_once(const std::string& warning_key, const std::string& message)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (!m_logged_material_warnings.insert(warning_key).second)
		{
			return;
		}
		HLogWarning("{}", message);
	}

	void RenderAssetManager::log_texture_warning_once(const std::string& warning_key, const std::string& message)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		if (!m_logged_texture_warnings.insert(warning_key).second)
		{
			return;
		}
		HLogWarning("{}", message);
	}

	bool RenderAssetManager::populate_cpu_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);
		ASH_PROCESS_ERROR(m_asset_database);

		std::shared_ptr<const Model> model{};
		ASH_PROCESS_ERROR(m_asset_database->load_model_by_path(asset->asset_path, model));
		ASH_PROCESS_ERROR(model);

		const Mesh* mesh = get_model_mesh_by_index(*model, asset->mesh_index);
		ASH_PROCESS_ERROR(mesh);
		ASH_PROCESS_ERROR(mesh->has_geometry());

		asset->name = mesh->name.empty() ? asset->asset_path : mesh->name;
		asset->bounds.is_valid = true;
		asset->bounds.local_min = mesh->bounds_min;
		asset->bounds.local_max = mesh->bounds_max;
		asset->vertices = mesh->vertices;
		asset->indices = mesh->indices;
		asset->material_slots = model->material_slots;
		asset->sections.clear();
		asset->sections.reserve(mesh->sections.size());
		for (const MeshSection& source_section : mesh->sections)
		{
			StaticMeshRenderSection section{};
			section.first_index = source_section.index_offset;
			section.index_count = source_section.index_count;
			section.material_slot = source_section.material_slot;
			section.topology = source_section.topology;
			section.material = resolve_default_section_material(asset->asset_path, *model, source_section.material_slot);
			asset->sections.push_back(std::move(section));
		}
		if (asset->sections.empty())
		{
			StaticMeshRenderSection section{};
			section.first_index = 0;
			section.index_count = static_cast<uint32_t>(asset->indices.size());
			section.material_slot = k_invalid_material_slot;
			section.topology = MeshPrimitiveTopology::Triangles;
			section.material = request_material_asset(k_builtin_default_surface_material_path);
			asset->sections.push_back(std::move(section));
		}

		asset->last_error.clear();
		asset->state = StaticMeshRenderAssetState::CpuReady;
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			asset->state = StaticMeshRenderAssetState::Failed;
			if (asset->last_error.empty())
			{
				asset->last_error = "Failed to populate static mesh CPU data.";
			}
		}
		return bResult;
	}

	bool RenderAssetManager::create_gpu_mesh_resource(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(require_render_thread_for_gpu_asset_work("create_gpu_mesh_resource"));
		ASH_PROCESS_ERROR(!asset->vertices.empty());
		ASH_PROCESS_ERROR(!asset->indices.empty());

		auto resource = std::make_shared<StaticMeshRenderResource>();
		resource->vertex_buffer = m_renderer->create_vertex_buffer({
			static_cast<uint32_t>(asset->vertices.size() * sizeof(MeshVertex)),
			static_cast<uint32_t>(sizeof(MeshVertex)),
			false,
			asset->vertices.data(),
			asset->name.c_str()
		});
		ASH_PROCESS_ERROR(resource->vertex_buffer);

		resource->index_buffer = m_renderer->create_index_buffer({
			static_cast<uint32_t>(asset->indices.size() * sizeof(uint32_t)),
			RenderIndexFormat::UInt32,
			false,
			asset->indices.data(),
			asset->name.c_str()
		});
		ASH_PROCESS_ERROR(resource->index_buffer);

		resource->vertex_count = static_cast<uint32_t>(asset->vertices.size());
		resource->index_count = static_cast<uint32_t>(asset->indices.size());
		resource->index_format = RenderIndexFormat::UInt32;
		resource->vertex_decl = get_mesh_vertex_decl();
		ASH_PROCESS_ERROR(resource->vertex_decl != nullptr);

		asset->resource = resource;
		asset->last_error.clear();
		asset->state = StaticMeshRenderAssetState::GpuReady;
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			asset->state = StaticMeshRenderAssetState::Failed;
			if (asset->last_error.empty())
			{
				asset->last_error = "Failed to create static mesh GPU resources.";
			}
			HLogError(
				"RenderAssetManager failed to create GPU resource for '{}' mesh {}.",
				asset->asset_path,
				asset->mesh_index);
		}
		return bResult;
	}
}
