#pragma once

#include "Function/Asset/TerrainData.h"
#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainImport.h"
#include "Function/Asset/TerrainLayerStack.h"
#include "Function/Scene/TerrainQuery.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace AshEditor
{
	enum class TerrainEditorMode : uint8_t
	{
		Manage = 0,
		Sculpt,
		Paint,
		Layers
	};

	enum class TerrainLayerActionKind : uint8_t
	{
		Add = 0,
		Delete,
		Duplicate,
		Rename,
		Move,
		SetVisible,
		SetLocked,
		SetOpacity
	};

	struct TerrainLayerActionIntent
	{
		TerrainLayerActionKind kind = TerrainLayerActionKind::Add;
		AshEngine::TerrainLayerId layer_id{};
		AshEngine::TerrainHeightBlendMode blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
		std::string name{};
		uint32_t destination_index = 0;
		float opacity = 1.0f;
		bool flag_value = false;
	};

	struct TerrainEditorIntent
	{
		enum class Kind : uint8_t
		{
			SelectAsset = 0,
			SelectLayer,
			BeginStroke,
			AddStrokeSample,
			EndStroke,
			CancelStroke,
			LayerAction,
			Save,
			Reload,
			KeepLocal,
			SaveAs,
			Optimize,
			Import,
			Export
		};

		Kind kind = Kind::SelectAsset;
		AshEngine::TerrainAssetId asset_id = 0;
		AshEngine::TerrainLayerId layer_id{};
		uint64_t sequence = 0;
		glm::vec3 world_position{ 0.0f };
		AshEngine::TerrainStrokeSample stroke_sample{};
		AshEngine::TerrainBrushMetric brush_metric{};
		AshEngine::TerrainBrushParameters brush{};
		TerrainLayerActionIntent layer_action{};
		std::filesystem::path asset_path{};
		std::optional<AshEngine::TerrainHeightImportDesc> import_desc{};
		std::optional<AshEngine::TerrainHeightExportDesc> export_desc{};
	};

	struct TerrainEditorPreviewState
	{
		AshEngine::TerrainQueryStatus query_status = AshEngine::TerrainQueryStatus::Outside;
		glm::vec3 center_ws{ 0.0f };
		glm::vec3 normal_ws{ 0.0f, 1.0f, 0.0f };
		float radius = 1.0f;
		bool layer_locked = false;
		bool stroke_active = false;
	};

	class TerrainEditorSessionCore final
	{
	public:
		using TerrainSnapshotPublisher = std::function<bool(
			AshEngine::TerrainAssetId,
			const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>&)>;

		AshEngine::TerrainAssetId GetAssetId() const;
		AshEngine::TerrainLayerId GetSelectedLayerId() const;
		const TerrainEditorPreviewState& GetPreviewState() const;
		bool HasActiveStroke() const;
		bool Reduce(const TerrainEditorIntent& refIntent);
		bool Open(AshEngine::TerrainWorkingSet workingSet);
		void Close();
		const AshEngine::TerrainWorkingSet* GetWorkingSet() const;
		bool SelectLayer(AshEngine::TerrainLayerId layerId);
		bool BeginStroke(uint64_t sequence);
		bool EndStroke(uint64_t sequence);
		void CancelStroke();
		bool ApplyBrushStroke(
			uint64_t sequence,
			const AshEngine::TerrainBrushParameters& refParameters,
			const AshEngine::TerrainBrushMetric& refMetric,
			const std::vector<AshEngine::TerrainStrokeSample>& refRawSamples,
			std::vector<AshEngine::TerrainEditPatch>& refPatches,
			std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
			std::string* pError = nullptr);
		bool ApplyStrokePatches(
			AshEngine::TerrainAssetId assetId,
			AshEngine::TerrainLayerId layerId,
			const std::vector<AshEngine::TerrainEditPatch>& refPatches,
			AshEngine::TerrainEditPatchDirection eDirection,
			std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
			std::string* pError = nullptr);
		bool ApplyLayerStackEdit(
			const AshEngine::TerrainLayerStackEdit& refEdit,
			AshEngine::TerrainLayerStackPatch& refPatch,
			std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
			std::string* pError = nullptr);
		bool ApplyLayerStackPatch(
			AshEngine::TerrainAssetId assetId,
			const AshEngine::TerrainLayerStackPatch& refPatch,
			AshEngine::TerrainEditPatchDirection eDirection,
			AshEngine::TerrainLayerId selectedLayerId,
			std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
			std::string* pError = nullptr);
		bool ComposeComponents(
			const std::vector<AshEngine::TerrainComponentCoord>& refRequestedComponents,
			std::vector<AshEngine::TerrainDirtyComponentPayload>& refPayloads,
			std::string* pError = nullptr) const;
		bool PublishDirtyComponents(
			const std::vector<AshEngine::TerrainDirtyComponentPayload>& refPayloads,
			const TerrainSnapshotPublisher& refPublisher,
			std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
			std::string* pError = nullptr);
		bool IsDirty() const;
		void SetPreviewQueryStatus(AshEngine::TerrainQueryStatus eStatus);

	private:
		void SelectAfterLayerTransition(
			const AshEngine::TerrainLayerStackPatch& refPatch,
			AshEngine::TerrainEditPatchDirection eDirection);
		void RefreshSelectedLayerPreview();

	private:
		AshEngine::TerrainAssetId _assetId = 0;
		AshEngine::TerrainLayerId _selectedLayerId{};
		uint64_t _activeSequence = 0;
		uint64_t _persistedContentGeneration = 0;
		std::optional<AshEngine::TerrainWorkingSet> _optWorkingSet{};
		TerrainEditorPreviewState _preview{};
	};
}
