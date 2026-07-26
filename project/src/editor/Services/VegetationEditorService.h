#pragma once

#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/VegetationBrush.h"
#include "Function/Asset/VegetationChunkSet.h"
#include "Function/Asset/VegetationFileOps.h"
#include "Function/Asset/VegetationStorage.h"
#include "Function/Scene/VegetationSurfaceProvider.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AshEditor
{
	class IEditorCommandExecutor;
	class IVegetationEditorTaskExecutor;

	enum class VegetationSessionState : uint8_t
	{
		Failed = 0,
		Clean,
		Dirty,
		Saving,
		SourceChanged
	};

	enum class VegetationOperationState : uint8_t
	{
		Idle = 0,
		Pending,
		Running,
		Succeeded,
		DirtyConflict,
		SourceChanged,
		AlreadyExists,
		Cancelled,
		TimedOut,
		Failed
	};

	struct VegetationEditorCapabilities
	{
		bool can_create = false;
		bool can_load = false;
		bool can_save = false;
		bool can_save_copy_as = false;
		bool can_reload = false;
		bool can_edit_palette = false;
		bool can_paint = false;
		bool can_erase = false;
		bool can_bake = false;
		std::string surface_unavailable_reason{};
	};

	struct VegetationPaletteViewEntry
	{
		std::filesystem::path species_path{};
		AshEngine::VegetationId species_id{};
		AshEngine::VegetationSha256 species_digest{};
		std::string display_name{};
		AshEngine::AssetLoadState load_state =
			AshEngine::AssetLoadState::Unloaded;
		std::string error{};
	};

	using VegetationPaletteView = std::vector<VegetationPaletteViewEntry>;

	struct VegetationEditorServiceDeps
	{
		AshEngine::AssetDatabase* pAssetDatabase = nullptr;
		std::filesystem::path asset_root{};
		IEditorCommandExecutor* pCommandExecutor = nullptr;
		const AshEngine::IVegetationSurfaceProvider* pSurfaceProvider = nullptr;
		IVegetationEditorTaskExecutor* pTaskExecutor = nullptr;
		AshEngine::VegetationLoadBudget load_budget{};
		AshEngine::VegetationChunkSetLoadBudget chunk_set_load_budget{};
		std::function<AshEngine::VegetationId()> create_layer_id{};
		AshEngine::IVegetationFileOps* pFileOps = nullptr;
	};

	class VegetationEditorService final
	{
	public:
		explicit VegetationEditorService(VegetationEditorServiceDeps deps);
		~VegetationEditorService();

		VegetationEditorService(const VegetationEditorService&) = delete;
		VegetationEditorService& operator=(const VegetationEditorService&) = delete;

		static AshEngine::VegetationLoadBudget DefaultLoadBudget();
		static AshEngine::VegetationChunkSetLoadBudget
			DefaultChunkSetLoadBudget();

		bool Initialize();
		void Shutdown();
		void Tick(std::chrono::steady_clock::time_point now);

		bool CreateLayer(
			const std::filesystem::path& layer_path, uint64_t seed);
		bool OpenLayer(const std::filesystem::path& layer_path);
		bool AddPaletteSpecies(
			const std::filesystem::path& species_path);
		bool ReplacePaletteSpecies(
			AshEngine::VegetationId target_species_id,
			const std::filesystem::path& species_path);
		bool RemovePaletteSpecies(
			AshEngine::VegetationId target_species_id,
			bool confirmed_clear_weights);

		bool BeginStroke(
			const AshEngine::VegetationBrushStroke& stroke_without_path,
			AshEngine::VegetationSurfaceBinding binding);
		bool AppendStrokePoint(
			const AshEngine::VegetationSurfaceSampleRequest& point);
		bool EndStroke(std::chrono::steady_clock::time_point now);

		bool RequestSave(std::chrono::steady_clock::time_point now);
		bool RequestSaveCopyAs(
			const std::filesystem::path& destination,
			std::chrono::steady_clock::time_point now);
		bool RequestReload(std::chrono::steady_clock::time_point now);
		bool RequestReloadDiscard(
			bool confirmed, std::chrono::steady_clock::time_point now);
		bool RequestBake(
			const AshEngine::VegetationSurfaceBinding& binding,
			std::chrono::steady_clock::time_point now);

		VegetationEditorCapabilities GetCapabilities() const;
		std::shared_ptr<const VegetationPaletteView> GetPaletteView() const;
		VegetationOperationState GetOperationState() const;
		uint64_t GetContentGeneration() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl{};
	};
}
