#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Function/Asset/AssetData.h"
#include "Function/Asset/TerrainData.h"
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	using AssetId = uint64_t;
	class MaterialInterface;

	enum class AssetType : uint8_t
	{
		Unknown = 0,
		Directory,
		Scene,
		Shader,
		Texture,
		Mesh,
		Model,
		Prefab,
		Material,
		Text,
		Binary,
		Terrain
	};

	enum class AssetLoadState : uint8_t
	{
		Unknown = 0,
		Unloaded,
		Loading,
		Loaded,
		Missing,
		Failed
	};

	struct AssetInfo
	{
		AssetId id = 0;
		AssetType type = AssetType::Unknown;
		std::string name{};
		std::filesystem::path relative_path{};
		std::filesystem::path parent_path{};
		bool is_directory = false;
		uint64_t file_size = 0;
		uint64_t last_write_time_ticks = 0;
	};

	// editor begin 修改原因：Terrain 候选接受必须比较完整的共享发布血缘，不能覆盖并发更新。
	struct TerrainSnapshotPublicationToken
	{
		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		TerrainAssetId asset_id = 0;
		uint64_t catalog_generation = 0;
		uint64_t load_serial = 0;
	};
	// editor end

	class ASH_API AssetDatabase
	{
	public:
		class Impl;

	public:
		AssetDatabase() = default;

	public:
		static AssetDatabase create(const std::filesystem::path& root_path);

		bool is_valid() const;
		bool set_root_path(const std::filesystem::path& root_path);
		const std::filesystem::path& get_root_path() const;

		bool refresh();
		const std::vector<AssetInfo>& get_assets() const;
		const AssetInfo* find_asset_by_id(AssetId id) const;
		const AssetInfo* find_asset_by_path(const std::filesystem::path& path) const;

		AssetLoadState get_asset_load_state(AssetId id) const;
		std::string get_asset_last_error(AssetId id) const;
		std::string get_last_error() const;

		bool load_text_by_id(AssetId id, std::string& out_text);
		bool load_text_by_path(const std::filesystem::path& path, std::string& out_text);
		bool load_binary_by_id(AssetId id, std::vector<uint8_t>& out_bytes);
		bool load_binary_by_path(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes);
		bool load_mesh_by_id(AssetId id, std::shared_ptr<const Mesh>& out_mesh);
		bool load_mesh_by_path(const std::filesystem::path& path, std::shared_ptr<const Mesh>& out_mesh);
		std::shared_future<std::shared_ptr<const Mesh>> load_mesh_by_id_async(AssetId id);
		std::shared_future<std::shared_ptr<const Mesh>> load_mesh_by_path_async(const std::filesystem::path& path);
		bool load_model_by_id(AssetId id, std::shared_ptr<const Model>& out_model);
		bool load_model_by_path(const std::filesystem::path& path, std::shared_ptr<const Model>& out_model);
		std::shared_future<std::shared_ptr<const Model>> load_model_by_id_async(AssetId id);
		std::shared_future<std::shared_ptr<const Model>> load_model_by_path_async(const std::filesystem::path& path);
		bool load_material_by_id(AssetId id, std::shared_ptr<const MaterialInterface>& out_material);
		bool load_material_by_path(const std::filesystem::path& path, std::shared_ptr<const MaterialInterface>& out_material);
		std::shared_future<std::shared_ptr<const MaterialInterface>> load_material_by_id_async(AssetId id);
		std::shared_future<std::shared_ptr<const MaterialInterface>> load_material_by_path_async(const std::filesystem::path& path);
		bool load_ashasset_by_id(AssetId id, std::shared_ptr<const AshAsset>& out_asset);
		bool load_ashasset_by_path(const std::filesystem::path& path, std::shared_ptr<const AshAsset>& out_asset);
		std::shared_future<std::shared_ptr<const AshAsset>> load_ashasset_by_id_async(AssetId id);
		std::shared_future<std::shared_ptr<const AshAsset>> load_ashasset_by_path_async(const std::filesystem::path& path);
		bool load_terrain_by_id(
			TerrainAssetId id,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot);
		bool load_terrain_by_path(
			const std::filesystem::path& path,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot);
		std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> load_terrain_by_id_async(
			TerrainAssetId id);
		std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>> load_terrain_by_path_async(
			const std::filesystem::path& path);
		// editor begin 修改原因：冲突候选必须隔离加载，用户确认前不得污染全局 Terrain cache。
		// Loads a disk candidate without changing the globally published Terrain cache.
		std::shared_future<std::shared_ptr<const TerrainAssetSnapshot>>
			load_terrain_candidate_by_id_async(TerrainAssetId id);
		TerrainSnapshotPublicationToken capture_terrain_snapshot_publication(
			TerrainAssetId id) const;
		bool publish_terrain_snapshot(
			TerrainAssetId id,
			std::shared_ptr<const TerrainAssetSnapshot> snapshot);
		// Replaces the globally published snapshot only if its captured lineage is unchanged.
		bool compare_exchange_terrain_snapshot(
			TerrainAssetId id,
			const TerrainSnapshotPublicationToken& expected,
			std::shared_ptr<const TerrainAssetSnapshot> snapshot,
			TerrainSnapshotPublicationToken* p_result = nullptr);
		// editor end
		bool invalidate_terrain_snapshot(TerrainAssetId id);

	private:
		std::shared_ptr<Impl> m_impl{};

	private:
		explicit AssetDatabase(std::shared_ptr<Impl> impl);
	};
}
