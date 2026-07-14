#include "Panels/Inspector/TerrainComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Services/AssetDatabaseService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorAssetPathWidgets.h"

#include <cmath>
#include <string>

namespace AshEditor
{
	namespace
	{
		inline constexpr const char* kTerrainComponentMenuId = "InspectorTerrainComponentMenu";

		bool IsTerrainTransformValid(const AshEngine::TransformComponent& refTransform)
		{
			return
				std::isfinite(refTransform.position.x) &&
				std::isfinite(refTransform.position.y) &&
				std::isfinite(refTransform.position.z) &&
				refTransform.rotation_euler_degrees == glm::vec3(0.0f) &&
				std::isfinite(refTransform.scale.x) && refTransform.scale.x > 0.0f &&
				std::isfinite(refTransform.scale.y) && refTransform.scale.y > 0.0f &&
				std::isfinite(refTransform.scale.z) && refTransform.scale.z > 0.0f;
		}

		bool IsTerrainHierarchyTransformValid(AshEngine::Entity entity)
		{
			while (entity.is_valid())
			{
				if (!IsTerrainTransformValid(entity.get_transform_component()))
				{
					return false;
				}
				entity = entity.get_parent();
			}
			return true;
		}

		bool DrawTerrainAssetPath(
			IInspectorComponentHost& refHost,
			AshEngine::UIContext& refUi,
			AshEngine::TerrainComponent& refTerrain)
		{
			InspectorPanelState& refState = refHost.AccessInspectorState();
			const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
			InspectorAssetPathWidgetState widgetState{};
			widgetState.pVecRecentPaths = &refState.vecRecentTerrainPaths;
			widgetState.pStrSearch = &refState.strTerrainAssetPickerSearch;

			InspectorAssetPathFieldDesc desc = MakeInspectorSceneAssetPathFieldDesc(
				{
					AshEngine::SceneComponentType::Terrain,
					"asset_path",
					"Terrain Asset",
					"Relative path to the .AshTerrain asset rendered and authored by this entity.",
					"Empty",
					"Terrain asset path",
					"Choose a Terrain asset before the component can be committed."
				},
				"##TerrainAssetPath",
				"TerrainAssetPickerPopup",
				"Select Terrain Asset");
			desc.vecAllowedAssetTypes = { AshEngine::AssetType::Terrain };
			desc.pDropLabelEmpty = "Drop Terrain asset here";
			desc.pDropLabelReplace = "Drop Terrain asset here to replace";
			desc.bDrawDropZone = true;
			return DrawInspectorAssetPathField(
				refUi,
				refTerrain.asset_path,
				desc,
				widgetState,
				refDeps.pAssetDatabaseService,
				refDeps.pDragDropTransferService);
		}

		bool DrawTerrainMaterialOverride(
			IInspectorComponentHost& refHost,
			AshEngine::UIContext& refUi,
			AshEngine::TerrainComponent& refTerrain,
			const uint32_t uLayerIndex)
		{
			InspectorPanelState& refState = refHost.AccessInspectorState();
			const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
			InspectorAssetPathWidgetState widgetState{};
			widgetState.pVecRecentPaths = &refState.vecRecentMaterialPaths;
			widgetState.pStrSearch = &refState.strMaterialAssetPickerSearch;

			const std::string strLabel = "Layer " + std::to_string(uLayerIndex) + " Override";
			const std::string strHiddenLabel = "##TerrainMaterialOverride" + std::to_string(uLayerIndex);
			const std::string strPopupId = "TerrainMaterialOverridePopup" + std::to_string(uLayerIndex);
			InspectorAssetPathFieldDesc desc{};
			desc.pLabel = strHiddenLabel.c_str();
			desc.pPopupId = strPopupId.c_str();
			desc.pPickerTitle = strLabel.c_str();
			desc.vecAllowedAssetTypes = { AshEngine::AssetType::Material };
			desc.fieldSpec = {
				strLabel.c_str(),
				"Optional material override for this fixed Terrain material layer.",
				"Empty uses the Terrain asset default",
				"Material asset path or empty",
				"The layer index remains stable across both render backends."
			};
			return DrawInspectorAssetPathField(
				refUi,
				refTerrain.material_layer_overrides[uLayerIndex],
				desc,
				widgetState,
				refDeps.pAssetDatabaseService,
				refDeps.pDragDropTransferService);
		}
	}

	AshEngine::SceneComponentType TerrainComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Terrain;
	}

	const char* TerrainComponentEditor::GetDisplayName() const
	{
		return "Terrain";
	}

	bool TerrainComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		const bool bHasPendingDraft =
			refState.draftTerrain.uEntityId == entity.get_id() &&
			refState.draftTerrain.optCurrentValue.has_value();
		return !bHasPendingDraft && AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool TerrainComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		refHost.ResetTerrainDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftTerrain.optCurrentValue = AshEngine::TerrainComponent{};
		refState.strTerrainAssetPickerSearch.clear();
		refUi.open_popup("TerrainAssetPickerPopup");
		return true;
	}

	bool TerrainComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncTerrainDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftTerrain.uEntityId == entity.get_id() &&
			refState.draftTerrain.optCurrentValue.has_value();
	}

	void TerrainComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
		if (!refState.draftTerrain.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Terrain", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kTerrainComponentMenuId))
		{
			refState.draftTerrain.optCurrentValue.reset();
			refHost.CommitTerrainDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::TerrainComponent& refTerrain = *refState.draftTerrain.optCurrentValue;
		bool bCommitRequested = DrawTerrainAssetPath(refHost, refUi, refTerrain);
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			{ AshEngine::SceneComponentType::Terrain, "visible", "Visible", "Submit this Terrain for rendering.", "Enabled", "On / Off", "Applies after the immutable Scene snapshot refresh." },
			"Visible",
			refTerrain.visible) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			{ AshEngine::SceneComponentType::Terrain, "casts_shadow", "Casts Shadow", "Allow this Terrain to render into shadow maps.", "Enabled", "On / Off", "Uses the shared Terrain.Shadow pass." },
			"Casts Shadow",
			refTerrain.casts_shadow) || bCommitRequested;
		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			{ AshEngine::SceneComponentType::Terrain, "receives_shadow", "Receives Shadow", "Allow scene shadows to affect this Terrain.", "Enabled", "On / Off", "Uses the normal scene lighting path." },
			"Receives Shadow",
			refTerrain.receives_shadow) || bCommitRequested;

		if (refUi.tree_node("Material Layer Overrides", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			for (uint32_t uLayerIndex = 0; uLayerIndex < refTerrain.material_layer_overrides.size(); ++uLayerIndex)
			{
				bCommitRequested = DrawTerrainMaterialOverride(
					refHost,
					refUi,
					refTerrain,
					uLayerIndex) || bCommitRequested;
			}
			refUi.tree_pop();
		}

		std::string strValidationMessage{};
		InspectorAssetPathValidationDesc validation{};
		validation.strAssetPath = refTerrain.asset_path;
		validation.strOriginalAssetPath = refState.draftTerrain.optOriginalValue.has_value()
			? refState.draftTerrain.optOriginalValue->asset_path
			: std::string{};
		validation.vecAllowedAssetTypes = { AshEngine::AssetType::Terrain };
		validation.pEmptyAssetPathMessage = "Choose a Terrain asset before committing this component.";
		validation.pMissingAssetMessage = "The selected Terrain asset is not indexed by the Asset Database.";
		validation.pUnsupportedAssetTypeMessage = "The selected asset is not a Terrain asset.";
		validation.pLoadStateProblemPrefix = "Terrain asset is not ready";
		validation.bBlockWhenEmpty = true;
		const bool bAssetBlocksCommit = TryGetInspectorAssetPathValidationMessage(
			validation,
			refDeps.pAssetDatabaseService,
			strValidationMessage);
		if (!strValidationMessage.empty())
		{
			refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", strValidationMessage.c_str());
		}

		bool bMaterialBlocksCommit = false;
		for (uint32_t uLayerIndex = 0; uLayerIndex < refTerrain.material_layer_overrides.size(); ++uLayerIndex)
		{
			const std::string& refOverride = refTerrain.material_layer_overrides[uLayerIndex];
			if (refOverride.empty())
			{
				continue;
			}

			InspectorAssetPathValidationDesc materialValidation{};
			materialValidation.strAssetPath = refOverride;
			materialValidation.strOriginalAssetPath = refState.draftTerrain.optOriginalValue.has_value()
				? refState.draftTerrain.optOriginalValue->material_layer_overrides[uLayerIndex]
				: std::string{};
			materialValidation.vecAllowedAssetTypes = { AshEngine::AssetType::Material };
			materialValidation.pMissingAssetMessage = "A Terrain material override is not indexed by the Asset Database.";
			materialValidation.pUnsupportedAssetTypeMessage = "A Terrain layer override is not a Material asset.";
			materialValidation.pLoadStateProblemPrefix = "Terrain material override is not ready";
			std::string strMaterialMessage{};
			if (TryGetInspectorAssetPathValidationMessage(
				materialValidation,
				refDeps.pAssetDatabaseService,
				strMaterialMessage))
			{
				bMaterialBlocksCommit = true;
			}
			if (!strMaterialMessage.empty())
			{
				refUi.text_colored(
					GetEditorWarningTextColor(refUi),
					"Layer %u: %s",
					uLayerIndex,
					strMaterialMessage.c_str());
			}
		}

		const bool bTransformValid = IsTerrainHierarchyTransformValid(entity);
		if (!bTransformValid)
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"Terrain requires zero rotation and finite positive scale on the full entity hierarchy.");
		}

		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Terrain",
				"Reset Terrain",
				"Restore visibility and shadow settings while preserving the selected Terrain asset.",
				"Restore##Terrain",
				"Restore Terrain",
				"Discard the local Terrain draft and reload the current Scene values.",
				"Terrain"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetTerrainDraftToDefaults(entity);
			refHost.CommitTerrainDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetTerrainDraftToLive(entity);
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftTerrain.optCurrentValue.reset();
			refHost.CommitTerrainDraft(entity);
			return;
		}

		if (bCommitRequested && !bAssetBlocksCommit && !bMaterialBlocksCommit && bTransformValid)
		{
			refHost.CommitTerrainDraft(entity);
		}
	}
}
