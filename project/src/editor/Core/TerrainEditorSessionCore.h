#pragma once

#include "Function/Asset/TerrainData.h"
#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainImport.h"
#include "Function/Scene/TerrainQuery.h"

#include <cstdint>
#include <filesystem>
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
		AshEngine::TerrainAssetId GetAssetId() const;
		const TerrainEditorPreviewState& GetPreviewState() const;
		bool HasActiveStroke() const;
		bool Reduce(const TerrainEditorIntent& refIntent);
		bool Open(AshEngine::TerrainWorkingSet workingSet);
		void Close();
		const AshEngine::TerrainWorkingSet* GetWorkingSet() const;
		bool ApplyStrokePatches(
			AshEngine::TerrainAssetId assetId,
			AshEngine::TerrainLayerId layerId,
			const std::vector<AshEngine::TerrainEditPatch>& refPatches,
			AshEngine::TerrainEditPatchDirection eDirection,
			std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
			std::string* pError = nullptr);
		bool IsDirty() const;
		void SetPreviewQueryStatus(AshEngine::TerrainQueryStatus eStatus);

	private:
		AshEngine::TerrainAssetId _assetId = 0;
		uint64_t _activeSequence = 0;
		uint64_t _persistedContentGeneration = 0;
		std::optional<AshEngine::TerrainWorkingSet> _optWorkingSet{};
		TerrainEditorPreviewState _preview{};
	};
}
