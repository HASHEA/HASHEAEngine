#include "Panels/Inspector/MeshComponentEditor.h"

#include "Base/hlog.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <algorithm>
#include <any>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace AshEditor
{
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
		bool bRestoreRequested = false;
		bCommitRequested = refHost.DrawMeshAssetPathEditor(refUi, mesh) || bCommitRequested;

		refUi.push_style_color(AshEngine::UIStyleColorKind::Button, kInspectorDropZoneFillColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, kInspectorDropZoneHoverColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, kInspectorDropZoneActiveColor);
		if (refUi.button(
			mesh.asset_path.empty() ? "Drop mesh/model asset here" : "Drop mesh/model asset here to replace",
			{ refUi.get_content_region_avail().x, 24.0f }))
		{
			refUi.open_popup("AssetPickerPopup");
		}
		DrawInspectorFieldTooltip(
			refUi,
			{
				"Mesh Drop Zone",
				"Drop a mesh or model asset here to replace the current renderer source.",
				"None",
				"Mesh / Model assets",
				"Accepts Asset Browser drag-drop payloads."
			});
		refUi.pop_style_color(3);
		const AshEngine::UIRect rectDropHint = refUi.get_item_rect();
		refUi.draw_window_rect(rectDropHint, kInspectorDropZoneBorderColor, 4.0f, 1.0f);
		if (refDeps.pDragDropTransferService && refUi.begin_drag_drop_target())
		{
			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::Asset);
			if (payload.is_delivery && payload.data && payload.data_size == sizeof(DragDropTransferId))
			{
				DragDropTransferId uTransferId = 0;
				std::memcpy(&uTransferId, payload.data, sizeof(DragDropTransferId));
				const DragDropTransferData* pData = refDeps.pDragDropTransferService->Resolve(uTransferId);
				if (pData && pData->extraData.has_value())
				{
					try
					{
						mesh.asset_path = std::any_cast<std::string>(pData->extraData);
						refState.strAssetPickerSearch.clear();
						bCommitRequested = true;
					}
					catch (const std::bad_any_cast&)
					{
						HLogWarning("InspectorPanel rejected mesh drag-drop payload because the asset path type was invalid.");
					}
				}
			}
			refUi.end_drag_drop_target();
		}

		if (refUi.begin_popup("AssetPickerPopup"))
		{
			refUi.text_unformatted("Select Mesh Asset");
			refUi.separator();
			refUi.set_next_item_width(280.0f);
			refUi.input_text("##PickerSearch", refState.strAssetPickerSearch);

			if (!refState.vecRecentMeshPaths.empty())
			{
				refUi.text_colored(kInspectorMutedColor, "Recent");
				for (const std::string& strRecent : refState.vecRecentMeshPaths)
				{
					if (refUi.selectable(strRecent.c_str()))
					{
						mesh.asset_path = strRecent;
						refState.PushRecentMeshPath(strRecent);
						bCommitRequested = true;
						refUi.close_current_popup();
					}
				}
				refUi.separator();
			}

			refUi.text_colored(kInspectorMutedColor, "Assets");
			if (refDeps.pAssetDatabaseService)
			{
				if (refUi.begin_child("AssetPickerList", { 300.0f, 250.0f }))
				{
					const std::vector<AshEngine::AssetInfo>& vecAssets = refDeps.pAssetDatabaseService->GetItems();
					for (const AshEngine::AssetInfo& refAsset : vecAssets)
					{
						if (refAsset.type != AshEngine::AssetType::Mesh &&
							refAsset.type != AshEngine::AssetType::Model)
						{
							continue;
						}

						const std::string strRelPath = refAsset.relative_path.generic_string();
						if (!refState.strAssetPickerSearch.empty())
						{
							if (strRelPath.find(refState.strAssetPickerSearch) == std::string::npos &&
								refAsset.name.find(refState.strAssetPickerSearch) == std::string::npos)
							{
								continue;
							}
						}
						if (refUi.selectable(strRelPath.c_str()))
						{
							mesh.asset_path = strRelPath;
							refState.PushRecentMeshPath(strRelPath);
							refState.strAssetPickerSearch.clear();
							bCommitRequested = true;
							refUi.close_current_popup();
						}
					}
				}
				refUi.end_child();
			}
			else
			{
				refUi.text_colored(kInspectorWarningColor, "Asset database not available.");
			}

			refUi.end_popup();
		}

		std::string strMeshAssetValidationMessage{};
		const bool bMeshAssetBlocksCommit = TryGetMeshAssetValidationMessage(
			refState.draftMesh,
			refDeps.pAssetDatabaseService,
			strMeshAssetValidationMessage);
		if (!strMeshAssetValidationMessage.empty())
		{
			refUi.text_colored(kInspectorWarningColor, "%s", strMeshAssetValidationMessage.c_str());
		}

		bCommitRequested = DrawInspectorCheckboxField(
			refUi,
			"Visible",
			mesh.visible,
			{
				"Visible",
				"Toggles whether this mesh renderer should be submitted for scene rendering.",
				"Enabled",
				"On / Off",
				"Applies immediately."
			}) || bCommitRequested;
		int iMeshIndex = static_cast<int>(mesh.mesh_index);
		if (DrawInspectorInputIntField(
			refUi,
			"Mesh Index",
			iMeshIndex,
			{
				"Mesh Index",
				"Zero-based sub-mesh index used when the asset contains multiple mesh entries.",
				"0",
				"[0, +inf)",
				"Negative values are clamped back to 0 before commit."
			}))
		{
			mesh.mesh_index = static_cast<uint32_t>(std::max(0, iMeshIndex));
			bCommitRequested = true;
		}
		int iMobility = static_cast<int>(mesh.mobility);
		const std::vector<const char*> vecMobilityLabels{ "Static", "Stationary", "Movable" };
		if (DrawInspectorComboField(
			refUi,
			"Mobility",
			iMobility,
			vecMobilityLabels,
			{
				"Mobility",
				"Declares whether the renderer is expected to stay fixed, update occasionally, or move freely.",
				"Static",
				"Static / Stationary / Movable",
				"Used by scene systems to reason about update frequency and lighting behavior."
			}))
		{
			mesh.mobility = static_cast<AshEngine::SceneMobility>(iMobility);
			bCommitRequested = true;
		}
		int iLayerMask = static_cast<int>(std::min<uint32_t>(
			mesh.layer_mask,
			static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
		if (DrawInspectorInputIntField(
			refUi,
			"Layer Mask",
			iLayerMask,
			{
				"Layer Mask",
				"Bitmask that decides which scene layers can see or filter this renderer.",
				"1",
				"[1, 2147483647] in this editor view",
				"Zero is restored to the default scene layer automatically."
			}))
		{
			mesh.layer_mask = static_cast<uint32_t>(std::max(0, iLayerMask));
			bCommitRequested = true;
		}
		if (SanitizeMeshComponent(mesh))
		{
			LogInspectorDraftSanitized("Mesh", entity.get_id());
			bCommitRequested = true;
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			"Reset Mesh",
			{
				"Reset Mesh",
				"Reset the editable mesh settings to defaults while preserving the selected asset reference.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetMeshDraftToDefaults(entity);
			LogInspectorDraftReset("Mesh", "defaults", entity.get_id());
			bCommitRequested = true;
		}
		refUi.same_line();
		if (DrawInspectorSmallActionButton(
			refUi,
			"Restore##Mesh",
			{
				"Restore Mesh",
				"Discard the local mesh draft and reload the current scene values without committing.",
				{},
				{},
				"Immediate action"
			}))
		{
			refHost.ResetMeshDraftToLive(entity);
			LogInspectorDraftReset("Mesh", "live scene state", entity.get_id());
			bRestoreRequested = true;
		}
		refUi.same_line();
		if (refHost.DrawComponentRemoveAction(refUi, "Mesh"))
		{
			refState.draftMesh.optCurrentValue.reset();
			refHost.CommitMeshDraft(entity);
			return;
		}

		if (!bRestoreRequested && bCommitRequested)
		{
			if (MeshComponentHasValidAssetPath(refState.draftMesh.optCurrentValue) && !bMeshAssetBlocksCommit)
			{
				refHost.CommitMeshDraft(entity);
			}
		}
	}
}
