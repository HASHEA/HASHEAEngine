#include "Panels/InspectorPanel.h"
#include "Base/hlog.h"
#include "Core/EditorComponentComparison.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EntityCommands.h"
#include "Core/EditorIds.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorTooltipWidgets.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	struct InspectorPanelState
	{
		struct IdentityDraft
		{
			SceneEntityId uEntityId = 0;
			std::string strOriginalName{};
			std::string strCurrentName{};
		};

		struct TransformDraft
		{
			SceneEntityId uEntityId = 0;
			AshEngine::TransformComponent originalValue{};
			AshEngine::TransformComponent currentValue{};
		};

		struct CameraDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::CameraComponent> optOriginalValue{};
			std::optional<AshEngine::CameraComponent> optCurrentValue{};
		};

		struct LightDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::LightComponent> optOriginalValue{};
			std::optional<AshEngine::LightComponent> optCurrentValue{};
		};

		struct MeshDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::MeshComponent> optOriginalValue{};
			std::optional<AshEngine::MeshComponent> optCurrentValue{};
		};

		IdentityDraft draftIdentity{};
		TransformDraft draftTransform{};
		CameraDraft draftCamera{};
		LightDraft draftLight{};
		MeshDraft draftMesh{};

		// Asset picker state.
		std::vector<std::string> vecRecentMeshPaths{};
		std::string strAssetPickerSearch{};

		void PushRecentMeshPath(const std::string& strPath)
		{
			if (strPath.empty())
			{
				return;
			}
			// Remove duplicate if already in the list.
			vecRecentMeshPaths.erase(
				std::remove(vecRecentMeshPaths.begin(), vecRecentMeshPaths.end(), strPath),
				vecRecentMeshPaths.end());
			vecRecentMeshPaths.insert(vecRecentMeshPaths.begin(), strPath);
			if (vecRecentMeshPaths.size() > 10)
			{
				vecRecentMeshPaths.resize(10);
			}
		}
	};

	namespace
	{
		constexpr AshEngine::UIColor kInspectorAccentColor{ 0.67f, 0.78f, 0.92f, 1.0f };
		constexpr AshEngine::UIColor kInspectorMutedColor{ 0.67f, 0.70f, 0.76f, 1.0f };
		constexpr AshEngine::UIColor kInspectorWarningColor{ 0.95f, 0.80f, 0.48f, 1.0f };
		constexpr AshEngine::UITooltipConfig kInspectorCompactTooltipConfig{
			{ 360.0f, 0.0f },
			{ 220.0f, 0.0f },
			{ 520.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr AshEngine::UITooltipConfig kInspectorDetailTooltipConfig{
			{ 520.0f, 0.0f },
			{ 320.0f, 0.0f },
			{ 720.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr float kInspectorSummaryLabelWidth = 112.0f;
		constexpr AshEngine::UITableFlags kInspectorSummaryTableFlags =
			AshEngine::UITableFlagBits::SizingStretchProp |
			AshEngine::UITableFlagBits::BordersInner;
		constexpr AshEngine::UIColor kInspectorDropZoneFillColor{ 0.24f, 0.31f, 0.39f, 0.38f };
		constexpr AshEngine::UIColor kInspectorDropZoneHoverColor{ 0.30f, 0.42f, 0.54f, 0.52f };
		constexpr AshEngine::UIColor kInspectorDropZoneActiveColor{ 0.36f, 0.50f, 0.64f, 0.68f };
		constexpr AshEngine::UIColor kInspectorDropZoneBorderColor{ 0.47f, 0.60f, 0.75f, 0.62f };
		constexpr const char* kInspectorCameraComponentMenuId = "InspectorCameraComponentMenu";
		constexpr const char* kInspectorLightComponentMenuId = "InspectorLightComponentMenu";
		constexpr const char* kInspectorMeshComponentMenuId = "InspectorMeshComponentMenu";

		bool DrawRemoveComponentButton(AshEngine::UIContext& refUi, const char* pLabel)
		{
			return refUi.small_button(pLabel);
		}

		bool DrawComponentHeaderContextMenu(
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

		bool DrawComponentRemoveAction(AshEngine::UIContext& refUi, const char* pIdSuffix)
		{
			const std::string strLabel = std::string("Remove Component##") + (pIdSuffix ? pIdSuffix : "Component");
			return DrawRemoveComponentButton(refUi, strLabel.c_str());
		}

		bool EditNameComponent(AshEngine::UIContext& refUi, const char* pLabel, AshEngine::NameComponent& refComponent)
		{
			return refUi.input_text(pLabel, refComponent.value);
		}

		bool EditVec3(
			AshEngine::UIContext& refUi,
			const char* pLabel,
			glm::vec3& refValue,
			float fSpeed,
			float fMinValue,
			float fMaxValue,
			const char* pFormat = "%.3f")
		{
			return refUi.drag_float3(pLabel, &refValue.x, fSpeed, fMinValue, fMaxValue, pFormat);
		}

		bool EditColor3(AshEngine::UIContext& refUi, const char* pLabel, glm::vec3& refValue)
		{
			return refUi.color_edit3(pLabel, &refValue.x);
		}

		std::optional<AshEngine::CameraComponent> GetCameraComponentValue(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_camera_component()
				? std::optional<AshEngine::CameraComponent>{ refEntity.get_camera_component() }
				: std::nullopt;
		}

		std::optional<AshEngine::LightComponent> GetLightComponentValue(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_light_component()
				? std::optional<AshEngine::LightComponent>{ refEntity.get_light_component() }
				: std::nullopt;
		}

		std::optional<AshEngine::MeshComponent> GetMeshComponentValue(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_mesh_component()
				? std::optional<AshEngine::MeshComponent>{ refEntity.get_mesh_component() }
				: std::nullopt;
		}

		bool MeshComponentHasValidAssetPath(const std::optional<AshEngine::MeshComponent>& optValue)
		{
			return optValue.has_value() && !optValue->asset_path.empty();
		}

		bool BeginSummaryTable(AshEngine::UIContext& refUi, const char* pTableId)
		{
			if (!refUi.begin_table(pTableId, 2, kInspectorSummaryTableFlags))
			{
				return false;
			}

			refUi.table_setup_column("Label", AshEngine::UITableColumnFlagBits::WidthFixed, kInspectorSummaryLabelWidth);
			refUi.table_setup_column("Value", AshEngine::UITableColumnFlagBits::WidthStretch);
			return true;
		}

		void DrawSummaryRow(
			AshEngine::UIContext& refUi,
			const char* pLabel,
			std::string_view svValue)
		{
			refUi.table_next_row();
			refUi.table_next_column();
			refUi.text_colored(kInspectorMutedColor, "%s", pLabel);
			refUi.table_next_column();
			const std::string_view svDisplayValue = svValue.empty() ? std::string_view("-") : svValue;
			const std::string strDisplayValue(svDisplayValue);
			refUi.text_wrapped("%s", strDisplayValue.c_str());
		}

		void DrawSummaryBoolRow(AshEngine::UIContext& refUi, const char* pLabel, bool bValue)
		{
			DrawSummaryRow(refUi, pLabel, bValue ? "Yes" : "No");
		}

		void DrawPanelIntro(AshEngine::UIContext& refUi, const char* pTitle, const char* pDescription)
		{
			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored(kInspectorAccentColor, "%s", pTitle);
			refUi.pop_font();
			refUi.text_colored(kInspectorMutedColor, "%s", pDescription);
			refUi.separator();
		}

		void DrawSelectionSummary(
			AshEngine::UIContext& refUi,
			const EditorSelection& refSelection,
			std::string_view svTooltipDescription = {})
		{
			const char* pKindLabel = "Selection";
			switch (refSelection.eKind)
			{
			case EditorSelectionKind::Entity:
				pKindLabel = "Entity";
				break;
			case EditorSelectionKind::Asset:
				pKindLabel = "Asset";
				break;
			default:
				break;
			}

			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored(kInspectorAccentColor, "%s", refSelection.strLabel.c_str());
			refUi.pop_font();
			if (refUi.is_item_hovered())
			{
				const bool bUseDetailTooltip = !refSelection.strPath.empty();
				refUi.begin_tooltip(bUseDetailTooltip ? kInspectorDetailTooltipConfig : kInspectorCompactTooltipConfig);
				if (bUseDetailTooltip)
				{
					DrawEditorTooltipTitle(refUi, refSelection.strLabel, "Selection Details");
					if (BeginEditorTooltipTable(refUi, "InspectorSelectionTooltip", 72.0f))
					{
						DrawEditorTooltipRow(refUi, "Kind", pKindLabel);
						DrawEditorTooltipRow(refUi, "Id", std::to_string(static_cast<unsigned long long>(refSelection.uId)));
						DrawEditorTooltipRow(refUi, "Path", refSelection.strPath);
						refUi.end_table();
					}
				}
				else
				{
					DrawEditorTooltipCompactTitle(refUi, refSelection.strLabel, pKindLabel);
					DrawEditorTooltipCompactRow(
						refUi,
						"Id",
						std::to_string(static_cast<unsigned long long>(refSelection.uId)));
					if (!svTooltipDescription.empty())
					{
						refUi.separator();
						DrawEditorTooltipCaption(refUi, svTooltipDescription);
					}
				}
				if (bUseDetailTooltip && !svTooltipDescription.empty())
				{
					refUi.separator();
					DrawEditorTooltipDescription(refUi, svTooltipDescription);
				}
				refUi.end_tooltip();
			}
			refUi.separator();
		}

		void DrawAssetInspector(
			AshEngine::UIContext& refUi,
			const EditorSelection& refSelection)
		{
			(void)refUi;
			(void)refSelection;
		}

		void DrawEmptyState(AshEngine::UIContext& refUi)
		{
			DrawPanelIntro(refUi, "Inspector", "Select an entity or asset to inspect and edit its properties.");
			refUi.bullet_text("Entity selections show editable components and hierarchy data.");
			refUi.bullet_text("Asset selections can be previewed from Asset Browser with the Preview context action.");
		}

		void DrawHierarchySection(AshEngine::UIContext& refUi, const AshEngine::Entity& refEntity)
		{
			if (!refUi.collapsing_header("Hierarchy", AshEngine::UITreeNodeFlagBits::DefaultOpen))
			{
				return;
			}

			const AshEngine::Entity parent = refEntity.get_parent();
			if (BeginSummaryTable(refUi, "InspectorHierarchySummary"))
			{
				DrawSummaryRow(
					refUi,
					"Parent",
					parent.is_valid() ? std::to_string(parent.get_id()) : std::string("<Root>"));
				DrawSummaryRow(refUi, "Children", std::to_string(refEntity.get_children().size()));
				refUi.end_table();
			}
		}

		bool CanAddCameraComponent(const AshEngine::Entity& refEntity)
		{
			return !refEntity.has_camera_component();
		}

		bool CanAddLightComponent(const AshEngine::Entity& refEntity)
		{
			return !refEntity.has_light_component();
		}

		bool CanAddMeshComponent(const AshEngine::Entity& refEntity)
		{
			return !refEntity.has_mesh_component();
		}

		bool ApplyCameraComponentValue(
			AshEngine::Entity entity,
			const std::optional<AshEngine::CameraComponent>& optValue)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!optValue.has_value())
			{
				return entity.has_camera_component() && entity.remove_camera_component();
			}

			return entity.has_camera_component()
				? entity.set_camera_component(*optValue)
				: entity.add_camera_component(*optValue);
		}

		bool ApplyLightComponentValue(
			AshEngine::Entity entity,
			const std::optional<AshEngine::LightComponent>& optValue)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!optValue.has_value())
			{
				return entity.has_light_component() && entity.remove_light_component();
			}

			return entity.has_light_component()
				? entity.set_light_component(*optValue)
				: entity.add_light_component(*optValue);
		}

		bool ApplyMeshComponentValue(
			AshEngine::Entity entity,
			const std::optional<AshEngine::MeshComponent>& optValue)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!optValue.has_value())
			{
				return entity.has_mesh_component() && entity.remove_mesh_component();
			}

			return entity.has_mesh_component()
				? entity.set_mesh_component(*optValue)
				: entity.add_mesh_component(*optValue);
		}
	}

	InspectorPanel::InspectorPanel(InspectorPanelDeps deps)
		: EditorPanel(EditorPanelIds::Inspector, EditorWindowTitles::Inspector)
		, _deps(deps)
		, _upState(std::make_unique<InspectorPanelState>())
	{
	}

	InspectorPanel::~InspectorPanel() = default;

	void InspectorPanel::BindEventBus(EditorEventBus* pEventBus)
	{
		if (_eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}

		_eventBindings.Bind(pEventBus);
		if (!pEventBus)
		{
			return;
		}

		_eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				if (
					refEvent.currentSelection.eKind != EditorSelectionKind::Entity ||
					refEvent.currentSelection.uId != refEvent.previousSelection.uId)
				{
					ResetEntityDrafts();
				}
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				ResetEntityDrafts();
			});
	}

	void InspectorPanel::ClearDeps()
	{
		_deps = {};
	}

	void InspectorPanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	void InspectorPanel::ResetEntityDrafts()
	{
		InspectorPanelState& state = GetState();
		state.draftIdentity = {};
		state.draftTransform = {};
		state.draftCamera = {};
		state.draftLight = {};
		state.draftMesh = {};
	}

	void InspectorPanel::SyncEntityDrafts(const AshEngine::Entity& refEntity)
	{
		InspectorPanelState& state = GetState();
		if (state.draftIdentity.uEntityId != refEntity.get_id())
		{
			state.draftIdentity.uEntityId = refEntity.get_id();
			state.draftIdentity.strOriginalName = refEntity.get_name();
			state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
		}
		else if (state.draftIdentity.strCurrentName == state.draftIdentity.strOriginalName)
		{
			state.draftIdentity.strOriginalName = refEntity.get_name();
			state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
		}

		const AshEngine::TransformComponent liveTransform = refEntity.get_transform_component();
		if (state.draftTransform.uEntityId != refEntity.get_id())
		{
			state.draftTransform.uEntityId = refEntity.get_id();
			state.draftTransform.originalValue = liveTransform;
			state.draftTransform.currentValue = liveTransform;
		}
		else if (state.draftTransform.currentValue.position == state.draftTransform.originalValue.position &&
			state.draftTransform.currentValue.rotation_euler_degrees == state.draftTransform.originalValue.rotation_euler_degrees &&
			state.draftTransform.currentValue.scale == state.draftTransform.originalValue.scale)
		{
			state.draftTransform.originalValue = liveTransform;
			state.draftTransform.currentValue = liveTransform;
		}
	}

	void InspectorPanel::SyncCameraDraft(const AshEngine::Entity& refEntity)
	{
		InspectorPanelState& state = GetState();
		const std::optional<AshEngine::CameraComponent> optLiveValue = GetCameraComponentValue(refEntity);
		if (state.draftCamera.uEntityId != refEntity.get_id())
		{
			state.draftCamera.uEntityId = refEntity.get_id();
			state.draftCamera.optOriginalValue = optLiveValue;
			state.draftCamera.optCurrentValue = optLiveValue;
		}
		else if (OptionalComponentsEqual(state.draftCamera.optCurrentValue, state.draftCamera.optOriginalValue, &CameraComponentsEqual))
		{
			state.draftCamera.optOriginalValue = optLiveValue;
			state.draftCamera.optCurrentValue = optLiveValue;
		}
	}

	void InspectorPanel::SyncLightDraft(const AshEngine::Entity& refEntity)
	{
		InspectorPanelState& state = GetState();
		const std::optional<AshEngine::LightComponent> optLiveValue = GetLightComponentValue(refEntity);
		if (state.draftLight.uEntityId != refEntity.get_id())
		{
			state.draftLight.uEntityId = refEntity.get_id();
			state.draftLight.optOriginalValue = optLiveValue;
			state.draftLight.optCurrentValue = optLiveValue;
		}
		else if (OptionalComponentsEqual(state.draftLight.optCurrentValue, state.draftLight.optOriginalValue, &LightComponentsEqual))
		{
			state.draftLight.optOriginalValue = optLiveValue;
			state.draftLight.optCurrentValue = optLiveValue;
		}
	}

	void InspectorPanel::SyncMeshDraft(const AshEngine::Entity& refEntity)
	{
		InspectorPanelState& state = GetState();
		const std::optional<AshEngine::MeshComponent> optLiveValue = GetMeshComponentValue(refEntity);
		if (state.draftMesh.uEntityId != refEntity.get_id())
		{
			state.draftMesh.uEntityId = refEntity.get_id();
			state.draftMesh.optOriginalValue = optLiveValue;
			state.draftMesh.optCurrentValue = optLiveValue;
		}
		else if (OptionalComponentsEqual(state.draftMesh.optCurrentValue, state.draftMesh.optOriginalValue, &MeshComponentsEqual))
		{
			state.draftMesh.optOriginalValue = optLiveValue;
			state.draftMesh.optCurrentValue = optLiveValue;
		}
	}

	bool InspectorPanel::HasPendingIdentityChanges() const
	{
		const InspectorPanelState& state = GetState();
		return state.draftIdentity.strCurrentName != state.draftIdentity.strOriginalName;
	}

	bool InspectorPanel::HasPendingTransformChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !TransformComponentsEqual(state.draftTransform.currentValue, state.draftTransform.originalValue);
	}

	bool InspectorPanel::HasPendingCameraChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(state.draftCamera.optCurrentValue, state.draftCamera.optOriginalValue, &CameraComponentsEqual);
	}

	bool InspectorPanel::HasPendingLightChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(state.draftLight.optCurrentValue, state.draftLight.optOriginalValue, &LightComponentsEqual);
	}

	bool InspectorPanel::HasPendingMeshChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(state.draftMesh.optCurrentValue, state.draftMesh.optOriginalValue, &MeshComponentsEqual);
	}

	void InspectorPanel::DrawComponentSections(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncCameraDraft(entity);
		SyncLightDraft(entity);
		SyncMeshDraft(entity);

		const bool bShowCamera = state.draftCamera.uEntityId == entity.get_id() && state.draftCamera.optCurrentValue.has_value();
		const bool bShowLight = state.draftLight.uEntityId == entity.get_id() && state.draftLight.optCurrentValue.has_value();
		const bool bShowMesh = state.draftMesh.uEntityId == entity.get_id() && state.draftMesh.optCurrentValue.has_value();

		DrawAddComponentMenu(refUi, entity);
		refUi.separator();
		DrawIdentitySection(refUi, entity);
		refUi.separator();
		DrawTransformSection(refUi, entity);

		if (bShowCamera)
		{
			refUi.separator();
			DrawCameraSection(refUi, entity);
		}
		if (bShowLight)
		{
			refUi.separator();
			DrawLightSection(refUi, entity);
		}
		if (bShowMesh)
		{
			refUi.separator();
			DrawMeshSection(refUi, entity);
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

		refUi.text_colored(kInspectorMutedColor, "Components");
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

		if (refUi.input_text("Name", state.draftIdentity.strCurrentName))
		{
			if (!state.draftIdentity.strCurrentName.empty())
			{
				CommitIdentityDraft(entity);
			}
		}
		if (state.draftIdentity.strCurrentName.empty() && refUi.is_item_deactivated_after_edit())
		{
			state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
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
		bool bChanged = false;
		bChanged = EditVec3(refUi, "Position", transform.position, 0.1f, 0.0f, 0.0f) || bChanged;
		bChanged = EditVec3(refUi, "Rotation", transform.rotation_euler_degrees, 0.5f, 0.0f, 0.0f) || bChanged;
		bChanged = EditVec3(refUi, "Scale", transform.scale, 0.05f, 0.0f, 0.0f) || bChanged;
		if (bChanged)
		{
			CommitTransformDraft(entity);
		}
	}

	bool InspectorPanel::DrawCameraSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncCameraDraft(entity);
		if (!state.draftCamera.optCurrentValue.has_value())
		{
			return false;
		}

		const bool bOpen = refUi.collapsing_header("Camera", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (DrawComponentHeaderContextMenu(refUi, kInspectorCameraComponentMenuId))
		{
			state.draftCamera.optCurrentValue.reset();
			CommitCameraDraft(entity);
			return false;
		}
		if (!bOpen)
		{
			return true;
		}

		AshEngine::CameraComponent& camera = *state.draftCamera.optCurrentValue;
		bool bChanged = false;
		bChanged = refUi.checkbox("Primary", camera.primary) || bChanged;

		int iProjection = static_cast<int>(camera.projection);
		const std::vector<const char*> vecProjectionLabels{ "Perspective", "Orthographic" };
		if (refUi.combo("Projection", iProjection, vecProjectionLabels))
		{
			camera.projection = static_cast<AshEngine::CameraProjectionType>(iProjection);
			bChanged = true;
		}

		bChanged = refUi.drag_float("FOV Y", camera.fov_y_degrees, 0.1f, 1.0f, 179.0f) || bChanged;
		bChanged = refUi.drag_float("Near Plane", camera.near_plane, 0.01f, 0.001f, camera.far_plane) || bChanged;
		bChanged = refUi.drag_float("Far Plane", camera.far_plane, 1.0f, camera.near_plane, 10000.0f) || bChanged;
		bChanged = refUi.drag_float("Ortho Height", camera.orthographic_height, 0.1f, 0.1f, 1000.0f) || bChanged;

		if (bChanged)
		{
			CommitCameraDraft(entity);
		}
		if (DrawComponentRemoveAction(refUi, "Camera"))
		{
			state.draftCamera.optCurrentValue.reset();
			CommitCameraDraft(entity);
			return false;
		}

		return true;
	}

	bool InspectorPanel::DrawLightSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncLightDraft(entity);
		if (!state.draftLight.optCurrentValue.has_value())
		{
			return false;
		}

		const bool bOpen = refUi.collapsing_header("Light", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (DrawComponentHeaderContextMenu(refUi, kInspectorLightComponentMenuId))
		{
			state.draftLight.optCurrentValue.reset();
			CommitLightDraft(entity);
			return false;
		}
		if (!bOpen)
		{
			return true;
		}

		AshEngine::LightComponent& light = *state.draftLight.optCurrentValue;
		bool bChanged = false;
		int iLightType = static_cast<int>(light.type);
		const std::vector<const char*> vecLightLabels{ "Directional", "Point", "Spot" };
		if (refUi.combo("Light Type", iLightType, vecLightLabels))
		{
			light.type = static_cast<AshEngine::LightType>(iLightType);
			bChanged = true;
		}

		bChanged = EditColor3(refUi, "Color", light.color) || bChanged;
		bChanged = refUi.drag_float("Intensity", light.intensity, 0.05f, 0.0f, 100.0f) || bChanged;
		bChanged = refUi.drag_float("Range", light.range, 0.1f, 0.0f, 1000.0f) || bChanged;
		bChanged = refUi.drag_float("Inner Cone", light.inner_cone_angle_degrees, 0.1f, 0.0f, 180.0f) || bChanged;
		bChanged = refUi.drag_float("Outer Cone", light.outer_cone_angle_degrees, 0.1f, 0.0f, 180.0f) || bChanged;

		if (bChanged)
		{
			CommitLightDraft(entity);
		}
		if (DrawComponentRemoveAction(refUi, "Light"))
		{
			state.draftLight.optCurrentValue.reset();
			CommitLightDraft(entity);
			return false;
		}

		return true;
	}

	bool InspectorPanel::DrawMeshSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncMeshDraft(entity);
		if (!state.draftMesh.optCurrentValue.has_value())
		{
			return false;
		}

		const bool bOpen = refUi.collapsing_header("Mesh", AshEngine::UITreeNodeFlagBits::DefaultOpen);
		if (DrawComponentHeaderContextMenu(refUi, kInspectorMeshComponentMenuId))
		{
			state.draftMesh.optCurrentValue.reset();
			CommitMeshDraft(entity);
			return false;
		}
		if (!bOpen)
		{
			return true;
		}

		AshEngine::MeshComponent& mesh = *state.draftMesh.optCurrentValue;
		bool bChanged = false;
		bChanged = DrawMeshAssetPathEditor(refUi, mesh) || bChanged;
		if (mesh.asset_path.empty())
		{
			refUi.text_colored(kInspectorWarningColor, "Choose a mesh asset before this component is committed to the scene.");
		}
		refUi.push_style_color(AshEngine::UIStyleColorKind::Button, kInspectorDropZoneFillColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, kInspectorDropZoneHoverColor);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, kInspectorDropZoneActiveColor);
		if (refUi.button(
			mesh.asset_path.empty() ? "Drop mesh/model asset here" : "Drop mesh/model asset here to replace",
			{ refUi.get_content_region_avail().x, 24.0f }))
		{
			refUi.open_popup("AssetPickerPopup");
		}
		refUi.pop_style_color(3);
		const AshEngine::UIRect rectDropHint = refUi.get_item_rect();
		refUi.draw_window_rect(rectDropHint, kInspectorDropZoneBorderColor, 4.0f, 1.0f);
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
						mesh.asset_path = std::any_cast<std::string>(pData->extraData);
						state.strAssetPickerSearch.clear();
						bChanged = true;
					}
					catch (const std::bad_any_cast&) {}
				}
			}
			refUi.end_drag_drop_target();
		}

		// Asset picker popup.
		if (refUi.begin_popup("AssetPickerPopup"))
		{
			refUi.text_unformatted("Select Mesh Asset");
			refUi.separator();
			refUi.set_next_item_width(280.0f);
			refUi.input_text("##PickerSearch", state.strAssetPickerSearch);

			// Recent paths section.
			if (!state.vecRecentMeshPaths.empty())
			{
				refUi.text_colored(kInspectorMutedColor, "Recent");
				for (const std::string& strRecent : state.vecRecentMeshPaths)
				{
					if (refUi.selectable(strRecent.c_str()))
					{
						mesh.asset_path = strRecent;
						state.PushRecentMeshPath(strRecent);
						bChanged = true;
						refUi.close_current_popup();
					}
				}
				refUi.separator();
			}

			// Filtered asset list from database.
			refUi.text_colored(kInspectorMutedColor, "Assets");
			if (_deps.pAssetDatabaseService)
			{
				if (refUi.begin_child("AssetPickerList", { 300.0f, 250.0f }))
				{
					const std::vector<AshEngine::AssetInfo>& vecAssets = _deps.pAssetDatabaseService->GetItems();
					for (const AshEngine::AssetInfo& refAsset : vecAssets)
					{
						if (refAsset.type != AshEngine::AssetType::Mesh &&
							refAsset.type != AshEngine::AssetType::Model)
						{
							continue;
						}
						const std::string strRelPath = refAsset.relative_path.generic_string();
						if (!state.strAssetPickerSearch.empty())
						{
							if (strRelPath.find(state.strAssetPickerSearch) == std::string::npos &&
								refAsset.name.find(state.strAssetPickerSearch) == std::string::npos)
							{
								continue;
							}
						}
						if (refUi.selectable(strRelPath.c_str()))
						{
							mesh.asset_path = strRelPath;
							state.PushRecentMeshPath(strRelPath);
							state.strAssetPickerSearch.clear();
							bChanged = true;
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

		bChanged = refUi.checkbox("Visible", mesh.visible) || bChanged;
		refUi.same_line();
		if (DrawComponentRemoveAction(refUi, "Mesh"))
		{
			state.draftMesh.optCurrentValue.reset();
			CommitMeshDraft(entity);
			return false;
		}

		if (bChanged)
		{
			if (MeshComponentHasValidAssetPath(state.draftMesh.optCurrentValue))
			{
				CommitMeshDraft(entity);
			}
		}
		return true;
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
		DrawHierarchySection(refUi, entity);
	}

	bool InspectorPanel::DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& meshComponent)
	{
		bool bChanged = false;
		const float fAvail = refUi.get_content_region_avail().x;
		const float fButtonWidth = 60.0f;
		const float fSpacing = 4.0f;
		const float fInputWidth = std::max(60.0f, fAvail - fButtonWidth - fSpacing);
		refUi.set_next_item_width(fInputWidth);
		bChanged = refUi.input_text("##AssetPath", meshComponent.asset_path) || bChanged;
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
					catch (const std::bad_any_cast&) {}
				}
			}
			refUi.end_drag_drop_target();
		}
		refUi.same_line(0.0f, fSpacing);
		if (refUi.button("Browse", { fButtonWidth, 0.0f }))
		{
			refUi.open_popup("AssetPickerPopup");
		}
		return bChanged;
	}

	bool InspectorPanel::CommitIdentityDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() ||
			state.draftIdentity.strCurrentName.empty() ||
			!HasPendingIdentityChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<RenameEntityCommand>(
					entity.get_id(),
					state.draftIdentity.strOriginalName,
					state.draftIdentity.strCurrentName));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate name edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = entity.set_name(state.draftIdentity.strCurrentName);
		}

		if (bApplied)
		{
			state.draftIdentity.strOriginalName = state.draftIdentity.strCurrentName;
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Identity changes. Entity={}.", uEntityId);
		state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
		return false;
	}

	bool InspectorPanel::CommitTransformDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() || !HasPendingTransformChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<TransformEntityCommand>(
					entity.get_id(),
					state.draftTransform.originalValue,
					state.draftTransform.currentValue));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate transform edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = entity.set_transform_component(state.draftTransform.currentValue);
		}

		if (bApplied)
		{
			state.draftTransform.originalValue = state.draftTransform.currentValue;
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Transform changes. Entity={}.", uEntityId);
		state.draftTransform.currentValue = state.draftTransform.originalValue;
		return false;
	}

	bool InspectorPanel::CommitCameraDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() || !HasPendingCameraChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetCameraComponentCommand>(
					entity.get_id(),
					state.draftCamera.optOriginalValue,
					state.draftCamera.optCurrentValue));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate camera edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = ApplyCameraComponentValue(entity, state.draftCamera.optCurrentValue);
		}

		if (bApplied)
		{
			state.draftCamera.optOriginalValue = state.draftCamera.optCurrentValue;
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Camera changes. Entity={}.", uEntityId);
		state.draftCamera.optCurrentValue = state.draftCamera.optOriginalValue;
		return false;
	}

	bool InspectorPanel::CommitLightDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() || !HasPendingLightChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetLightComponentCommand>(
					entity.get_id(),
					state.draftLight.optOriginalValue,
					state.draftLight.optCurrentValue));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate light edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = ApplyLightComponentValue(entity, state.draftLight.optCurrentValue);
		}

		if (bApplied)
		{
			state.draftLight.optOriginalValue = state.draftLight.optCurrentValue;
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Light changes. Entity={}.", uEntityId);
		state.draftLight.optCurrentValue = state.draftLight.optOriginalValue;
		return false;
	}

	bool InspectorPanel::CommitMeshDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() || !HasPendingMeshChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetMeshComponentCommand>(
					entity.get_id(),
					state.draftMesh.optOriginalValue,
					state.draftMesh.optCurrentValue));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate mesh edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = ApplyMeshComponentValue(entity, state.draftMesh.optCurrentValue);
		}

		if (bApplied)
		{
			state.draftMesh.optOriginalValue = state.draftMesh.optCurrentValue;
			if (state.draftMesh.optCurrentValue.has_value())
			{
				state.PushRecentMeshPath(state.draftMesh.optCurrentValue->asset_path);
			}
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Mesh changes. Entity={}.", uEntityId);
		state.draftMesh.optCurrentValue = state.draftMesh.optOriginalValue;
		return false;
	}

	void InspectorPanel::OnAttach()
	{
		HLogInfo("InspectorPanel attached.");
	}

	void InspectorPanel::OnDetach()
	{
		UnsubscribeEvents();
		ClearDeps();
	}

	void InspectorPanel::OnGui(const EditorFrameContext& frameContext)
	{
		if (!BeginPanelWindow(frameContext))
		{
			EndPanelWindow(frameContext);
			return;
		}
		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (!_deps.pSelectionService || !_deps.pSelectionService->HasSelection())
		{
			ResetEntityDrafts();
			DrawEmptyState(refUi);
			EndPanelWindow(frameContext);
			return;
		}

		const EditorSelection& refSelection = _deps.pSelectionService->GetSelection();

		if (refSelection.eKind == EditorSelectionKind::Entity && _deps.pSceneService)
		{
			DrawSelectionSummary(
				refUi,
				refSelection,
				"Edit the selected entity. Property changes are applied immediately and can be undone with Ctrl+Z.");
			AshEngine::Entity entity = _deps.pSceneService->FindEntity(refSelection.uId);
			if (!entity.is_valid())
			{
				ResetEntityDrafts();
			}
			DrawEntityInspector(refUi, entity);
		}
		else if (refSelection.eKind == EditorSelectionKind::Asset)
		{
			DrawSelectionSummary(
				refUi,
				refSelection,
				"Basic asset metadata is folded into this tooltip. Use Asset Browser Preview when you need a richer inspection view.");
			ResetEntityDrafts();
			DrawAssetInspector(refUi, refSelection);
		}
		else
		{
			DrawSelectionSummary(refUi, refSelection);
			ResetEntityDrafts();
			DrawPanelIntro(refUi, "Inspector", "The current selection type does not have an inspector adapter yet.");
		}

		EndPanelWindow(frameContext);
	}

	InspectorPanelState& InspectorPanel::GetState()
	{
		return *_upState;
	}

	const InspectorPanelState& InspectorPanel::GetState() const
	{
		return *_upState;
	}
}
