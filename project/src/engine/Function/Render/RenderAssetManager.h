#pragma once

#include "Base/hcore.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/Material.h"
#include "Function/Render/MaterialSystem.h"
#include "Function/Render/StaticMeshRenderAsset.h"
#include "Function/Render/TextureAsset.h"
#include "Function/Scene/SceneComponents.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace AshEngine
{
	class MaterialInterface;
	class MaterialRenderProxy;

	enum class TextureFallbackKind : uint8_t
	{
		White = 0,
		Normal,
		Black
	};

	class ASH_API RenderAssetManager
	{
	public:
		RenderAssetManager() = default;

	public:
		void initialize(AssetDatabase* asset_database, Renderer* renderer);
		void shutdown();

		std::shared_ptr<StaticMeshRenderAsset> request_static_mesh_asset(const std::string& asset_path, uint32_t mesh_index);
		std::shared_ptr<const MaterialInterface> request_material_asset(const std::string& asset_path);
		std::shared_ptr<MaterialRenderProxy> request_material_render_proxy(const std::shared_ptr<const MaterialInterface>& material);
		std::shared_ptr<TextureAsset> request_texture_asset(
			const std::string& asset_path,
			TextureColorSpace color_space,
			TextureFallbackKind fallback_kind = TextureFallbackKind::White);
		std::shared_ptr<TextureAsset> request_fallback_texture(TextureFallbackKind fallback_kind);
		std::shared_ptr<RenderSampler> request_sampler(const RenderSamplerDesc& desc);
		std::shared_ptr<RenderSampler> request_default_sampler();
		bool resolve_static_mesh_primitive_sections(
			const StaticMeshRenderAsset& render_asset,
			const std::vector<MeshMaterialOverride>& material_overrides,
			std::vector<ResolvedStaticMeshSection>& out_sections);
		bool finalize_pending_static_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset);
		void finalize_pending_assets();

		AssetDatabase* get_asset_database() const;
		Renderer* get_renderer() const;
		MaterialSystem* get_material_system();
		const MaterialSystem* get_material_system() const;

	private:
		static std::string make_static_mesh_key(const std::string& asset_path, uint32_t mesh_index);
		static std::string make_material_key(const std::string& asset_path);
		static std::string make_material_proxy_key(const MaterialInterface& material);
		static std::string make_generated_material_key(const std::string& asset_path, uint32_t material_slot);
		static std::string make_texture_key(const std::string& asset_path, TextureColorSpace color_space);
		static std::string make_sampler_debug_name(const RenderSamplerDesc& desc);
		std::shared_ptr<const MaterialInterface> request_material_asset_internal(const std::string& asset_path, bool allow_default_fallback);
		std::shared_ptr<const MaterialInterface> request_generated_material_asset(
			const std::string& asset_path,
			uint32_t material_slot,
			const MaterialSlot& material_slot_data);
		std::shared_ptr<const MaterialInterface> resolve_default_section_material(const std::string& asset_path, const Model& model, uint32_t material_slot);
		bool populate_cpu_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset);
		bool create_gpu_mesh_resource(const std::shared_ptr<StaticMeshRenderAsset>& asset);
		std::shared_ptr<TextureAsset> get_fallback_texture(TextureFallbackKind fallback_kind);
		std::shared_ptr<TextureAsset> create_fallback_texture(
			const char* debug_name,
			TextureColorSpace color_space,
			const void* pixel_data,
			RenderTextureFormat format,
			uint32_t row_pitch);
		void log_material_warning_once(const std::string& warning_key, const std::string& message);
		void log_texture_warning_once(const std::string& warning_key, const std::string& message);

	private:
		AssetDatabase* m_asset_database = nullptr;
		Renderer* m_renderer = nullptr;
		std::mutex m_mutex{};
		std::unordered_map<std::string, std::shared_ptr<StaticMeshRenderAsset>> m_static_mesh_assets{};
		std::unordered_map<std::string, std::shared_ptr<const MaterialInterface>> m_material_assets{};
		std::unordered_map<std::string, std::shared_ptr<MaterialRenderProxy>> m_material_proxies{};
		std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_texture_assets{};
		std::unordered_map<RenderSamplerDesc, std::shared_ptr<RenderSampler>, RenderSamplerDescHash> m_sampler_pool{};
		std::unordered_set<std::string> m_logged_material_warnings{};
		std::unordered_set<std::string> m_logged_texture_warnings{};
		std::shared_ptr<TextureAsset> m_default_white_texture{};
		std::shared_ptr<TextureAsset> m_default_normal_texture{};
		std::shared_ptr<TextureAsset> m_default_black_texture{};
		MaterialSystem m_material_system{};
	};
}
