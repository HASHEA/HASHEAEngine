#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TerrainHeightFileFormat : uint8_t
	{
		RawR16 = 0,
		RawR32F,
		Png,
		Exr
	};

	enum class TerrainByteOrder : uint8_t
	{
		LittleEndian = 0,
		BigEndian
	};

	enum class TerrainResizePolicy : uint8_t
	{
		Reject = 0,
		Crop,
		CatmullRom
	};

	enum class TerrainExportSource : uint8_t
	{
		FinalComposedHeight = 0,
		BaseHeight,
		HeightEditLayer,
		MaterialWeightLayer
	};

	enum class TerrainImportResult : uint8_t
	{
		Success = 0,
		Cancelled,
		InvalidArguments,
		InvalidDimensions,
		UnsupportedFormat,
		DecodeFailure,
		EncodeFailure,
		MemoryLimitExceeded,
		IoFailure
	};

	class TerrainCancellationToken
	{
	public:
		ASH_API TerrainCancellationToken();
		ASH_API TerrainCancellationToken(const TerrainCancellationToken& other);
		ASH_API TerrainCancellationToken(TerrainCancellationToken&& other) noexcept;
		ASH_API auto operator=(const TerrainCancellationToken& other)
			-> TerrainCancellationToken&;
		ASH_API auto operator=(TerrainCancellationToken&& other) noexcept
			-> TerrainCancellationToken&;
		ASH_API ~TerrainCancellationToken();
		ASH_API void cancel();
		ASH_API bool is_cancelled() const;

	private:
		std::shared_ptr<std::atomic<bool>> m_cancelled{};
	};

	struct TerrainHeightImportDesc
	{
		std::filesystem::path source_path{};
		TerrainHeightFileFormat format = TerrainHeightFileFormat::RawR16;
		TerrainGridLayout target_layout{};
		TerrainHeightMapping height_mapping{};
		uint32_t source_width = 0;
		uint32_t source_height = 0;
		TerrainByteOrder byte_order = TerrainByteOrder::LittleEndian;
		TerrainResizePolicy resize_policy = TerrainResizePolicy::Reject;
		bool flip_x = false;
		bool flip_z = false;
		std::string exr_channel{};
		uint64_t peak_memory_limit_bytes = 1024ull * 1024ull * 1024ull;
		TerrainCancellationToken cancellation{};
	};

	struct TerrainHeightExportDesc
	{
		std::filesystem::path destination_path{};
		TerrainHeightFileFormat format = TerrainHeightFileFormat::RawR16;
		TerrainExportSource source = TerrainExportSource::FinalComposedHeight;
		TerrainLayerId source_layer_id{};
		uint32_t material_layer_index = 0;
		TerrainByteOrder byte_order = TerrainByteOrder::LittleEndian;
		std::string exr_channel = "Y";
		TerrainCancellationToken cancellation{};
	};

	struct TerrainImportReport
	{
		uint32_t source_width = 0;
		uint32_t source_height = 0;
		uint32_t source_bits_per_sample = 0;
		std::vector<std::string> warnings{};
	};

	ASH_API auto import_terrain_height(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainImportResult;

	ASH_API auto import_terrain_height_to_container(
		TerrainAssetId asset_id,
		const TerrainHeightImportDesc& desc,
		const std::filesystem::path& destination_path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainImportReport* out_report = nullptr,
		std::string* out_error = nullptr) -> TerrainImportResult;

	ASH_API auto export_terrain_height(
		const TerrainAssetSnapshot& snapshot,
		const TerrainHeightExportDesc& desc,
		std::string* out_error = nullptr) -> TerrainImportResult;
}
