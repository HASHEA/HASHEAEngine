#include "Panels/InspectorPanel.h"

#include "Base/hlog.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/InspectorComponentEditor.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorPanelSupport.h"
#include "Services/DragDropTransferService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <algorithm>
#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	void InspectorPanel::DrawComponentSections(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		// Resolve the visible component editors up front so the panel keeps ownership
		// while each component file owns only its local UI flow.
		std::vector<InspectorComponentEditor*> vecVisibleComponentEditors{};
		vecVisibleComponentEditors.reserve(_vecComponentEditors.size());
		for (const std::unique_ptr<InspectorComponentEditor>& upComponentEditor : _vecComponentEditors)
		{
			if (upComponentEditor && upComponentEditor->ShouldDraw(*this, entity))
			{
				vecVisibleComponentEditors.push_back(upComponentEditor.get());
			}
		}

		DrawAddComponentMenu(refUi, entity);
		refUi.separator();
		DrawIdentitySection(refUi, entity);
		refUi.separator();
		DrawTransformSection(refUi, entity);

		for (InspectorComponentEditor* pComponentEditor : vecVisibleComponentEditors)
		{
			refUi.separator();
			pComponentEditor->Draw(*this, refUi, entity);
		}
	}

	void InspectorPanel::DrawAddComponentMenu(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		const bool bCanAddCamera = CanAddCameraComponent(entity);
		const bool bCanAddLight = CanAddLightComponent(entity);
		const bool bHasPendingMeshDraft =
			state.draftMesh.uEntityId == entity.get_id() &&
			state.draftMesh.optCurrentValue.has_value();
		const bool bCanAddMesh = CanAddMeshComponent(entity) && !bHasPendingMeshDraft;
		const bool bHasAnyAddableComponent = bCanAddCamera || bCanAddLight || bCanAddMesh;

		refUi.text_colored(GetEditorMutedTextColor(refUi), "Components");
		refUi.same_line();
		refUi.begin_disabled(!bHasAnyAddableComponent);
		if (refUi.small_button("Add Component"))
		{
			refUi.open_popup("InspectorAddComponent");
		}
		refUi.end_disabled();

		if (!refUi.begin_popup("InspectorAddComponent"))
		{
			return;
		}

		if (bCanAddCamera && refUi.menu_item("Camera"))
		{
			GetState().draftCamera.optCurrentValue = AshEngine::CameraComponent{};
			CommitCameraDraft(entity);
			refUi.close_current_popup();
		}
		if (bCanAddLight && refUi.menu_item("Light"))
		{
			GetState().draftLight.optCurrentValue = AshEngine::LightComponent{};
			CommitLightDraft(entity);
			refUi.close_current_popup();
		}
		if (bCanAddMesh && refUi.menu_item("Mesh"))
		{
			state.draftMesh.uEntityId = entity.get_id();
			state.draftMesh.optOriginalValue = GetMeshComponentValue(entity);
			state.draftMesh.optCurrentValue = AshEngine::MeshComponent{};
			state.strAssetPickerSearch.clear();
			refUi.open_popup("AssetPickerPopup");
			refUi.close_current_popup();
		}
		if (!bHasAnyAddableComponent)
		{
			refUi.text_unformatted("All supported components are already present.");
		}

		refUi.end_popup();
	}

	void InspectorPanel::DrawIdentitySection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncEntityDrafts(entity);
		if (!refUi.collapsing_header("Identity", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		const bool bNameEdited = DrawInspectorTextField(
			refUi,
			"Name",
			state.draftIdentity.strCurrentName,
			{
				"Name",
				"Entity display name used across hierarchy, selection, and logs.",
				"Current entity name",
				"Non-empty text",
				"Applies immediately. Empty text is restored when editing ends."
			});
		const bool bRestoreEmptyName =
			state.draftIdentity.strCurrentName.empty() &&
			refUi.is_item_deactivated_after_edit();
		if (bRestoreEmptyName)
		{
			state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
		}
		refUi.same_line();
		if (DrawInspectorSmallActionButton(
			refUi,
			"Restore##Identity",
			{
				"Restore Identity",
				"Discard the in-progress name draft and reload the last committed scene value.",
				{},
				{},
				"Immediate action"
			}))
		{
			ResetIdentityDraftToLive(entity);
			LogInspectorDraftReset("Identity", "live scene state", entity.get_id());
			return;
		}
		if (bNameEdited && !state.draftIdentity.strCurrentName.empty())
		{
			CommitIdentityDraft(entity);
		}
	}

	void InspectorPanel::DrawTransformSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncEntityDrafts(entity);
		if (!refUi.collapsing_header("Transform", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		AshEngine::TransformComponent& transform = state.draftTransform.currentValue;
		bool bCommitRequested = false;
		bool bRestoreRequested = false;
		bCommitRequested = DrawInspectorDragVec3Field(
			refUi,
			"Position",
			transform.position,
			0.1f,
			0.0f,
			0.0f,
			{
				"Position",
				"Local-space translation relative to the entity parent.",
				"(0, 0, 0)",
				"Finite numbers",
				"Invalid numbers are restored to 0 immediately."
			}) || bCommitRequested;
		bCommitRequested = DrawInspectorDragVec3Field(
			refUi,
			"Rotation",
			transform.rotation_euler_degrees,
			0.5f,
			0.0f,
			0.0f,
			{
				"Rotation",
				"Euler rotation in degrees around the local X/Y/Z axes.",
				"(0, 0, 0)",
				"Finite numbers",
				"Invalid numbers are restored to 0 immediately."
			}) || bCommitRequested;
		bCommitRequested = DrawInspectorDragVec3Field(
			refUi,
			"Scale",
			transform.scale,
			0.05f,
			0.0f,
			0.0f,
			{
				"Scale",
				"Per-axis local scale. Zero and NaN values are not allowed because they destabilize transforms.",
				"(1, 1, 1)",
				"[0.0001, 100000] per axis",
				"Values are clamped into a safe editor range before commit."
			}) || bCommitRequested;
		if (SanitizeTransformComponent(transform))
		{
			LogInspectorDraftSanitized("Transform", entity.get_id());
			bCommitRequested = true;
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			"Reset Transform",
			{
				"Reset Transform",
				"Write the default transform values back to the entity and keep the change undoable.",
				{},
				{},
				"Immediate action"
			}))
		{
			ResetTransformDraftToDefaults(entity);
			LogInspectorDraftReset("Transform", "defaults", entity.get_id());
			bCommitRequested = true;
		}
		refUi.same_line();
		if (DrawInspectorSmallActionButton(
			refUi,
			"Restore##Transform",
			{
				"Restore Transform",
				"Discard the local transform draft and reload the current scene values without committing.",
				{},
				{},
				"Immediate action"
			}))
		{
			ResetTransformDraftToLive(entity);
			LogInspectorDraftReset("Transform", "live scene state", entity.get_id());
			bRestoreRequested = true;
		}
		if (HasTransformClampWarning(transform))
		{
			refUi.text_colored(kInspectorWarningColor, "Scale is currently pinned by the editor safety range.");
		}
		if (!bRestoreRequested && bCommitRequested)
		{
			CommitTransformDraft(entity);
		}
	}

	void InspectorPanel::DrawEntityInspector(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		if (!entity.is_valid())
		{
			refUi.text_colored(kInspectorWarningColor, "The selected entity is no longer available in the active scene.");
			return;
		}

		DrawComponentSections(refUi, entity);
		refUi.separator();
		DrawInspectorHierarchySection(refUi, entity);
	}

	bool InspectorPanel::DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& meshComponent)
	{
		bool bChanged = false;
		const float fAvail = refUi.get_content_region_avail().x;
		const float fButtonWidth = 60.0f;
		const float fSpacing = 4.0f;
		const float fInputWidth = std::max(60.0f, fAvail - fButtonWidth - fSpacing);
		refUi.set_next_item_width(fInputWidth);
		bChanged = DrawInspectorTextField(
			refUi,
			"##AssetPath",
			meshComponent.asset_path,
			{
				"Mesh Asset",
				"Relative asset path used to resolve the mesh or model resource for this renderer.",
				"Empty",
				"Valid mesh/model asset path",
				"Type a path, browse, or drag-drop from the Asset Browser."
			}) || bChanged;
		if (_deps.pDragDropTransferService && refUi.begin_drag_drop_target())
		{
			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::Asset);
			if (payload.is_delivery && payload.data && payload.data_size == sizeof(DragDropTransferId))
			{
				DragDropTransferId uTransferId = 0;
				std::memcpy(&uTransferId, payload.data, sizeof(DragDropTransferId));
				const DragDropTransferData* pData = _deps.pDragDropTransferService->Resolve(uTransferId);
				if (pData && pData->extraData.has_value())
				{
					try
					{
						meshComponent.asset_path = std::any_cast<std::string>(pData->extraData);
						GetState().strAssetPickerSearch.clear();
						bChanged = true;
					}
					catch (const std::bad_any_cast&)
					{
						HLogWarning("InspectorPanel rejected mesh asset field drop because the payload did not contain a string path.");
					}
				}
			}
			refUi.end_drag_drop_target();
		}
		refUi.same_line(0.0f, fSpacing);
		if (DrawInspectorActionButton(
			refUi,
			"Browse",
			{
				"Browse Mesh Assets",
				"Open the mesh/model picker and choose a resource from the indexed asset database.",
				{},
				{},
				"Immediate action"
			},
			{ fButtonWidth, 0.0f }))
		{
			refUi.open_popup("AssetPickerPopup");
		}
		return bChanged;
	}

	bool InspectorPanel::DrawComponentHeaderContextMenu(
		AshEngine::UIContext& refUi,
		const char* pPopupId)
	{
		bool bRemoveRequested = false;
		if (refUi.begin_popup_context_item(pPopupId))
		{
			bRemoveRequested = refUi.menu_item("Remove Component");
			refUi.end_popup();
		}
		return bRemoveRequested;
	}

	bool InspectorPanel::DrawComponentRemoveAction(
		AshEngine::UIContext& refUi,
		const char* pIdSuffix)
	{
		const std::string strLabel = std::string("Remove Component##") + (pIdSuffix ? pIdSuffix : "Component");
		return refUi.small_button(strLabel.c_str());
	}
}
