#include "Panels/InspectorPanel.h"

#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/InspectorComponentEditor.h"
#include "Panels/Inspector/InspectorComponentEditorRegistry.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorComponentMetadata.h"
#include "Panels/Inspector/InspectorPanelSupport.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorAssetPathWidgets.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <algorithm>
#include <string>
#include <vector>

namespace AshEditor
{
	void InspectorPanel::DrawComponentSections(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		// Resolve the visible component editors up front so the panel keeps ownership
		// while each component file owns only its local UI flow.
		const std::vector<InspectorComponentEditor*> vecVisibleComponentEditors =
			_upComponentEditorRegistry
				? _upComponentEditorRegistry->CollectVisibleEditors(*this, entity)
				: std::vector<InspectorComponentEditor*>{};

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
		const std::vector<InspectorComponentEditor*> vecAddableComponentEditors =
			_upComponentEditorRegistry
				? _upComponentEditorRegistry->CollectAddableEditors(*this, entity)
				: std::vector<InspectorComponentEditor*>{};
		const bool bHasAnyAddableComponent = !vecAddableComponentEditors.empty();

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

		for (InspectorComponentEditor* pComponentEditor : vecAddableComponentEditors)
		{
			if (pComponentEditor && refUi.menu_item(pComponentEditor->GetDisplayName()))
			{
				pComponentEditor->AddDefault(*this, refUi, entity);
				refUi.close_current_popup();
			}
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
		const InspectorComponentActionRowResult actionRowResult = DrawInspectorComponentActionRow(
			refUi,
			*this,
			{
				"Reset Transform",
				"Reset Transform",
				"Write the default transform values back to the entity and keep the change undoable.",
				"Restore##Transform",
				"Restore Transform",
				"Discard the local transform draft and reload the current scene values without committing.",
				nullptr
			});
		if (actionRowResult.bResetRequested)
		{
			ResetTransformDraftToDefaults(entity);
			LogInspectorDraftReset("Transform", "defaults", entity.get_id());
			CommitTransformDraft(entity);
			return;
		}
		if (actionRowResult.bRestoreRequested)
		{
			ResetTransformDraftToLive(entity);
			LogInspectorDraftReset("Transform", "live scene state", entity.get_id());
			return;
		}
		if (HasTransformClampWarning(transform))
		{
			refUi.text_colored(GetEditorWarningTextColor(refUi), "Scale is currently pinned by the editor safety range.");
		}
		if (bCommitRequested)
		{
			CommitTransformDraft(entity);
		}
	}

	void InspectorPanel::DrawEntityInspector(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		if (!entity.is_valid())
		{
			refUi.text_colored(
				GetEditorWarningTextColor(refUi),
				"The selected entity is no longer available in the active scene.");
			return;
		}

		DrawComponentSections(refUi, entity);
		refUi.separator();
		DrawInspectorHierarchySection(refUi, entity);
	}

	bool InspectorPanel::DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& meshComponent)
	{
		InspectorPanelState& state = GetState();
		InspectorAssetPathWidgetState widgetState{};
		widgetState.pVecRecentPaths = &state.vecRecentMeshPaths;
		widgetState.pStrSearch = &state.strAssetPickerSearch;

		InspectorAssetPathFieldDesc desc{};
		desc = MakeInspectorSceneAssetPathFieldDesc(
			{
				AshEngine::SceneComponentType::Mesh,
				"asset_path",
				"Mesh Asset",
				"Relative asset path used to resolve the mesh or model resource for this renderer.",
				"Empty",
				"Valid mesh/model asset path",
				"Type a path, browse, or drag-drop from the Asset Browser."
			},
			"##AssetPath",
			"AssetPickerPopup",
			"Select Mesh Asset");
		desc.pDropLabelEmpty = "Drop mesh/model asset here";
		desc.pDropLabelReplace = "Drop mesh/model asset here to replace";
		desc.browseSpec.pTitle = "Browse Mesh Assets";
		desc.dropZoneSpec = {
			"Mesh Drop Zone",
			"Drop a mesh or model asset here to replace the current renderer source.",
			"None",
			"Mesh / Model assets",
			"Accepts Asset Browser drag-drop payloads."
		};
		desc.bDrawDropZone = true;
		return DrawInspectorAssetPathField(
			refUi,
			meshComponent.asset_path,
			desc,
			widgetState,
			_deps.pAssetDatabaseService,
			_deps.pDragDropTransferService);
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
