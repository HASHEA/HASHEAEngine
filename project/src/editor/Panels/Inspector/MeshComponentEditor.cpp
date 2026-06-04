#include "Panels/Inspector/MeshComponentEditor.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Panels/Inspector/MeshMaterialOverrideEditor.h"
#include "Services/AssetDatabaseService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEditor
{
	AshEngine::SceneComponentType MeshComponentEditor::GetComponentType() const
	{
		return AshEngine::SceneComponentType::Mesh;
	}

	const char* MeshComponentEditor::GetDisplayName() const
	{
		return "Mesh";
	}

	bool MeshComponentEditor::CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const
	{
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		const bool bHasPendingMeshDraft =
			refState.draftMesh.uEntityId == entity.get_id() &&
			refState.draftMesh.optCurrentValue.has_value();
		return !bHasPendingMeshDraft && AshEngine::can_add_scene_component(entity, GetComponentType());
	}

	bool MeshComponentEditor::AddDefault(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		refHost.ResetMeshDraftToLive(entity);
		InspectorPanelState& refState = refHost.AccessInspectorState();
		refState.draftMesh.optCurrentValue = AshEngine::MeshComponent{};
		refState.strAssetPickerSearch.clear();
		refUi.open_popup("AssetPickerPopup");
		return true;
	}

	bool MeshComponentEditor::ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity)
	{
		refHost.SyncMeshDraft(entity);
		const InspectorPanelState& refState = refHost.AccessInspectorState();
		return
			refState.draftMesh.uEntityId == entity.get_id() &&
			refState.draftMesh.optCurrentValue.has_value();
	}

	void MeshComponentEditor::Draw(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::Entity entity)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
		if (!refState.draftMesh.optCurrentValue.has_value())
		{
			return;
		}

		const bool bOpen = refUi.collapsing_header("Mesh", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (refHost.DrawComponentHeaderContextMenu(refUi, kInspectorMeshComponentMenuId))
		{
			refState.draftMesh.optCurrentValue.reset();
			refHost.CommitMeshDraft(entity);
			return;
		}
		if (!bOpen)
		{
			return;
		}

		AshEngine::MeshComponent& mesh = *refState.draftMesh.optCurrentValue;
		bool bCommitRequested = false;
		bCommitRequested = refHost.DrawMeshAssetPathEditor(refUi, mesh) || bCommitRequested;

		std::string strMeshAssetValidationMessage{};
		const bool bMeshAssetBlocksCommit = TryGetMeshAssetValidationMessage(
			refState.draftMesh,
			refDeps.pAssetDatabaseService,
			strMeshAssetValidationMessage);
		if (!strMeshAssetValidationMessage.empty())
		{
			refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", strMeshAssetValidationMessage.c_str());
		}

		bCommitRequested = DrawInspectorSceneBoolField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Mesh,
				"visible",
				"Visible",
				"Toggles whether this mesh renderer should be submitted for scene rendering.",
				"Enabled",
				"On / Off",
				"Applies immediately."),
			"Visible",
			mesh.visible) || bCommitRequested;
		if (DrawInspectorSceneUIntField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Mesh,
				"mesh_index",
				"Mesh Index",
				"Zero-based sub-mesh index used when the asset contains multiple mesh entries.",
				"0",
				"[0, +inf)",
				"Negative values are clamped back to 0 before commit."),
			"Mesh Index",
			mesh.mesh_index))
		{
			bCommitRequested = true;
		}
		int32_t iMobility = static_cast<int32_t>(mesh.mobility);
		const std::vector<const char*> vecMobilityLabels{ "Static", "Stationary", "Movable" };
		if (DrawInspectorComboField(
			refUi,
			"Mobility",
			iMobility,
			vecMobilityLabels,
			MakeInspectorSceneFieldSpec(MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Mesh,
				"mobility",
				"Mobility",
				"Declares whether the renderer is expected to stay fixed, update occasionally, or move freely.",
				"Static",
				"Static / Stationary / Movable",
				"Used by scene systems to reason about update frequency and lighting behavior."))))
		{
			mesh.mobility = static_cast<AshEngine::SceneMobility>(iMobility);
			bCommitRequested = true;
		}
		if (DrawInspectorSceneUIntField(
			refUi,
			MakeInspectorSceneFieldDesc(
				AshEngine::SceneComponentType::Mesh,
				"layer_mask",
				"Layer Mask",
				"Bitmask that decides which scene layers can see or filter this renderer.",
				"1",
				"[1, 2147483647] in this editor view",
				"Zero is restored to the default scene layer automatically."),
			"Layer Mask",
			mesh.layer_mask))
		{
			bCommitRequested = true;
		}
		const MeshMaterialOverridesEditResult materialOverridesEditResult =
			DrawMeshMaterialOverridesEditor(refHost, refUi, mesh);
		bCommitRequested = materialOverridesEditResult.bCommitRequested || bCommitRequested;
		if (SanitizeMeshComponent(mesh))
		{
			LogInspectorDraftSanitized("Mesh", entity.get_id());
			bCommitRequested = true;
		}
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			refHost,
			{
				"Reset Mesh",
				"Reset Mesh",
				"Reset the editable mesh settings to defaults while preserving the selected asset reference.",
				"Restore##Mesh",
				"Restore Mesh",
				"Discard the local mesh draft and reload the current scene values without committing.",
				"Mesh"
			});
		if (actionRowResult.bResetRequested)
		{
			refHost.ResetMeshDraftToDefaults(entity);
			LogInspectorDraftReset("Mesh", "defaults", entity.get_id());
			if (MeshComponentHasValidAssetPath(refState.draftMesh.optCurrentValue) &&
				!bMeshAssetBlocksCommit &&
				!materialOverridesEditResult.bBlocksCommit)
			{
				refHost.CommitMeshDraft(entity);
			}
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			refHost.ResetMeshDraftToLive(entity);
			LogInspectorDraftReset("Mesh", "live scene state", entity.get_id());
			return;
		}
		if (actionRowResult.bRemoveRequested)
		{
			refState.draftMesh.optCurrentValue.reset();
			refHost.CommitMeshDraft(entity);
			return;
		}

		if (bCommitRequested)
		{
			if (MeshComponentHasValidAssetPath(refState.draftMesh.optCurrentValue) &&
				!bMeshAssetBlocksCommit &&
				!materialOverridesEditResult.bBlocksCommit)
			{
				refHost.CommitMeshDraft(entity);
			}
		}
	}
}
