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
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorButtonWidgets.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
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
	};

	namespace
	{
		constexpr AshEngine::UIColor kInspectorAccentColor{ 0.67f, 0.78f, 0.92f, 1.0f };
		constexpr AshEngine::UIColor kInspectorMutedColor{ 0.67f, 0.70f, 0.76f, 1.0f };
		constexpr AshEngine::UIColor kInspectorWarningColor{ 0.95f, 0.80f, 0.48f, 1.0f };

		// Keep the add/remove button flow identical across component sections.
		bool DrawAddComponentButton(AshEngine::UIContext& refUi, const char* pLabel)
		{
			return refUi.button(pLabel);
		}

		bool DrawRemoveComponentButton(AshEngine::UIContext& refUi, const char* pLabel)
		{
			return refUi.button(pLabel);
		}

		bool EditNameComponent(AshEngine::UIContext& refUi, const char* pLabel, AshEngine::NameComponent& refComponent)
		{
			return refUi.input_text(pLabel, refComponent.value);
		}

		bool EditMeshPath(AshEngine::UIContext& refUi, AshEngine::MeshComponent& refComponent)
		{
			return refUi.input_text("Asset Path", refComponent.asset_path);
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

		void DrawLabeledValue(AshEngine::UIContext& refUi, const char* pLabel, const std::string& strValue)
		{
			refUi.text_colored(kInspectorMutedColor, "%s", pLabel);
			refUi.same_line();
			refUi.text_wrapped("%s", strValue.empty() ? "-" : strValue.c_str());
		}

		void DrawLabeledBool(AshEngine::UIContext& refUi, const char* pLabel, bool bValue)
		{
			DrawLabeledValue(refUi, pLabel, bValue ? "Yes" : "No");
		}

		void DrawPanelIntro(AshEngine::UIContext& refUi, const char* pTitle, const char* pDescription)
		{
			refUi.text_colored(kInspectorAccentColor, "%s", pTitle);
			refUi.text_wrapped("%s", pDescription);
			refUi.separator();
		}

		void DrawSelectionSummary(AshEngine::UIContext& refUi, const EditorSelection& refSelection)
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

			refUi.text_colored(kInspectorAccentColor, "%s", refSelection.strLabel.c_str());
			refUi.text_colored(kInspectorMutedColor, "%s", pKindLabel);
			refUi.same_line();
			refUi.text_colored(kInspectorMutedColor, "| Id %llu", static_cast<unsigned long long>(refSelection.uId));
			if (!refSelection.strPath.empty())
			{
				DrawLabeledValue(refUi, "Path", refSelection.strPath);
			}
			refUi.separator();
		}

		void DrawEmptyState(AshEngine::UIContext& refUi)
		{
			DrawPanelIntro(refUi, "Inspector", "Select an entity or asset to inspect and edit its properties.");
			refUi.bullet_text("Entity selections show editable components and hierarchy data.");
			refUi.bullet_text("Asset selections show metadata from the asset database.");
		}

		void DrawHierarchySection(AshEngine::UIContext& refUi, const AshEngine::Entity& refEntity)
		{
			if (!refUi.collapsing_header("Hierarchy", AshEngine::UITreeNodeFlagBits::DefaultOpen))
			{
				return;
			}

			const AshEngine::Entity parent = refEntity.get_parent();
			DrawLabeledValue(refUi, "Parent", parent.is_valid() ? std::to_string(parent.get_id()) : std::string("<Root>"));
			DrawLabeledValue(refUi, "Children", std::to_string(refEntity.get_children().size()));
		}

		void DrawAssetInspector(
			AssetDatabaseService* pAssetDatabaseService,
			AshEngine::UIContext& refUi,
			const EditorSelection& refSelection)
		{
			if (!pAssetDatabaseService)
			{
				refUi.text_unformatted("Asset database service is not available.");
				return;
			}

			const AshEngine::AssetInfo* asset = pAssetDatabaseService->FindById(refSelection.uId);
			if (!asset)
			{
				refUi.text_unformatted("Selected asset no longer exists.");
				return;
			}

			DrawPanelIntro(refUi, "Asset Details", "Metadata comes from the editor asset database.");
			DrawLabeledValue(refUi, "Type", AssetDatabaseService::GetTypeLabel(asset->type));
			DrawLabeledValue(refUi, "Path", asset->relative_path.generic_string());
			DrawLabeledValue(refUi, "Parent", asset->parent_path.empty() ? std::string("<Root>") : asset->parent_path.generic_string());
			DrawLabeledBool(refUi, "Directory", asset->is_directory);
			DrawLabeledValue(refUi, "File Size", std::to_string(static_cast<unsigned long long>(asset->file_size)));
			DrawLabeledValue(refUi, "Load State", AssetDatabaseService::GetLoadStateLabel(pAssetDatabaseService->GetLoadState(asset->id)));
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
		DrawIdentitySection(refUi, entity);
		refUi.separator();
		DrawTransformSection(refUi, entity);
		refUi.separator();
		DrawCameraSection(refUi, entity);
		refUi.separator();
		DrawLightSection(refUi, entity);
		refUi.separator();
		DrawMeshSection(refUi, entity);
		refUi.separator();
	}

	void InspectorPanel::DrawPendingChangeHint(AshEngine::UIContext& refUi, const char* pLabel)
	{
		refUi.text_colored(kInspectorWarningColor, "%s", pLabel);
	}

	void InspectorPanel::DrawApplyRevertRow(
		AshEngine::UIContext& refUi,
		const char* pApplyLabel,
		const char* pRevertLabel,
		bool bCanApply,
		bool bHasPendingChanges,
		bool& bApplyClicked,
		bool& bRevertClicked)
	{
		// Inspector edits are collected into draft state and only committed on Apply.
		// This prevents immediate-mode widgets from generating one undo entry per keystroke or drag step.
		bApplyClicked = false;
		bRevertClicked = false;

		if (bCanApply)
		{
			PushEditorButtonVisuals(refUi);
		}
		refUi.begin_disabled(!bCanApply);
		bApplyClicked = refUi.button(pApplyLabel);
		refUi.end_disabled();
		if (bCanApply)
		{
			PopEditorButtonVisuals(refUi);
		}
		refUi.same_line();
		refUi.begin_disabled(!bHasPendingChanges);
		bRevertClicked = refUi.button(pRevertLabel);
		refUi.end_disabled();
	}

	void InspectorPanel::DrawIdentitySection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncEntityDrafts(entity);
		const bool bHasPendingChanges = HasPendingIdentityChanges();
		const char* pSectionLabel = bHasPendingChanges ? "Identity *" : "Identity";
		if (!refUi.collapsing_header(pSectionLabel, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		refUi.input_text("Name", state.draftIdentity.strCurrentName);

		if (bHasPendingChanges)
		{
			DrawPendingChangeHint(refUi, "Pending changes. Apply or revert to update the entity.");
		}

		const bool bCanApply = bHasPendingChanges && !state.draftIdentity.strCurrentName.empty();
		bool bApplyClicked = false;
		bool bRevertClicked = false;
		DrawApplyRevertRow(refUi, "Apply Name", "Revert Name", bCanApply, bHasPendingChanges, bApplyClicked, bRevertClicked);
		if (bApplyClicked)
		{
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
				// The editor still supports a "direct write" fallback so panels remain usable while services are
				// being wired up. This path does not produce undo entries.
				HLogWarning("InspectorPanel apply name clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.set_name(state.draftIdentity.strCurrentName);
			}

			if (bApplied)
			{
				state.draftIdentity.strOriginalName = state.draftIdentity.strCurrentName;
			}
			else
			{
				HLogWarning("InspectorPanel failed to apply Identity changes. Entity={}.", uEntityId);
			}
		}
		if (bRevertClicked)
		{
			state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
		}
	}

	void InspectorPanel::DrawTransformSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncEntityDrafts(entity);
		const bool bHasPendingChanges = HasPendingTransformChanges();
		const char* pSectionLabel = bHasPendingChanges ? "Transform *" : "Transform";
		if (!refUi.collapsing_header(pSectionLabel, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		AshEngine::TransformComponent& transform = state.draftTransform.currentValue;
		EditVec3(refUi, "Position", transform.position, 0.1f, 0.0f, 0.0f);
		EditVec3(refUi, "Rotation", transform.rotation_euler_degrees, 0.5f, 0.0f, 0.0f);
		EditVec3(refUi, "Scale", transform.scale, 0.05f, 0.0f, 0.0f);

		if (bHasPendingChanges)
		{
			DrawPendingChangeHint(refUi, "Pending changes. Apply or revert to update the entity.");
		}

		bool bApplyClicked = false;
		bool bRevertClicked = false;
		DrawApplyRevertRow(refUi, "Apply Transform", "Revert Transform", bHasPendingChanges, bHasPendingChanges, bApplyClicked, bRevertClicked);
		if (bApplyClicked)
		{
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
				HLogWarning("InspectorPanel apply transform clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.set_transform_component(state.draftTransform.currentValue);
			}

			if (bApplied)
			{
				state.draftTransform.originalValue = state.draftTransform.currentValue;
			}
			else
			{
				HLogWarning("InspectorPanel failed to apply Transform changes. Entity={}.", uEntityId);
			}
		}
		if (bRevertClicked)
		{
			state.draftTransform.currentValue = state.draftTransform.originalValue;
		}
	}

	void InspectorPanel::DrawCameraSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncCameraDraft(entity);
		const bool bHasPendingChanges = HasPendingCameraChanges();
		if (!state.draftCamera.optCurrentValue.has_value() && !bHasPendingChanges)
		{
			if (DrawAddComponentButton(refUi, "Add Camera"))
			{
				state.draftCamera.optCurrentValue = AshEngine::CameraComponent{};
			}
			return;
		}

		const char* pSectionLabel = bHasPendingChanges ? "Camera *" : "Camera";
		if (!refUi.collapsing_header(pSectionLabel, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (state.draftCamera.optCurrentValue.has_value())
		{
			AshEngine::CameraComponent& camera = *state.draftCamera.optCurrentValue;
			refUi.checkbox("Primary", camera.primary);

			int iProjection = static_cast<int>(camera.projection);
			const std::vector<const char*> vecProjectionLabels{ "Perspective", "Orthographic" };
			if (refUi.combo("Projection", iProjection, vecProjectionLabels))
			{
				camera.projection = static_cast<AshEngine::CameraProjectionType>(iProjection);
			}

			refUi.drag_float("FOV Y", camera.fov_y_degrees, 0.1f, 1.0f, 179.0f);
			refUi.drag_float("Near Plane", camera.near_plane, 0.01f, 0.001f, camera.far_plane);
			refUi.drag_float("Far Plane", camera.far_plane, 1.0f, camera.near_plane, 10000.0f);
			refUi.drag_float("Ortho Height", camera.orthographic_height, 0.1f, 0.1f, 1000.0f);

			if (DrawRemoveComponentButton(refUi, "Remove Camera"))
			{
				state.draftCamera.optCurrentValue.reset();
			}
		}
		else
		{
			refUi.text_unformatted("Camera component will be removed after Apply.");
		}

		if (bHasPendingChanges)
		{
			DrawPendingChangeHint(refUi, "Pending changes. Apply or revert to update the entity.");
		}

		bool bApplyClicked = false;
		bool bRevertClicked = false;
		DrawApplyRevertRow(refUi, "Apply Camera", "Revert Camera", bHasPendingChanges, bHasPendingChanges, bApplyClicked, bRevertClicked);
		if (bApplyClicked)
		{
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
			else if (state.draftCamera.optCurrentValue.has_value())
			{
				HLogWarning("InspectorPanel apply camera clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_camera_component()
					? entity.set_camera_component(*state.draftCamera.optCurrentValue)
					: entity.add_camera_component(*state.draftCamera.optCurrentValue);
			}
			else
			{
				HLogWarning("InspectorPanel apply camera clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_camera_component() && entity.remove_camera_component();
			}

			if (bApplied)
			{
				state.draftCamera.optOriginalValue = state.draftCamera.optCurrentValue;
			}
			else
			{
				HLogWarning("InspectorPanel failed to apply Camera changes. Entity={}.", uEntityId);
			}
		}
		if (bRevertClicked)
		{
			state.draftCamera.optCurrentValue = state.draftCamera.optOriginalValue;
		}
	}

	void InspectorPanel::DrawLightSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncLightDraft(entity);
		const bool bHasPendingChanges = HasPendingLightChanges();
		if (!state.draftLight.optCurrentValue.has_value() && !bHasPendingChanges)
		{
			if (DrawAddComponentButton(refUi, "Add Light"))
			{
				state.draftLight.optCurrentValue = AshEngine::LightComponent{};
			}
			return;
		}

		const char* pSectionLabel = bHasPendingChanges ? "Light *" : "Light";
		if (!refUi.collapsing_header(pSectionLabel, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (state.draftLight.optCurrentValue.has_value())
		{
			AshEngine::LightComponent& light = *state.draftLight.optCurrentValue;
			int iLightType = static_cast<int>(light.type);
			const std::vector<const char*> vecLightLabels{ "Directional", "Point", "Spot" };
			if (refUi.combo("Light Type", iLightType, vecLightLabels))
			{
				light.type = static_cast<AshEngine::LightType>(iLightType);
			}

			EditColor3(refUi, "Color", light.color);
			refUi.drag_float("Intensity", light.intensity, 0.05f, 0.0f, 100.0f);
			refUi.drag_float("Range", light.range, 0.1f, 0.0f, 1000.0f);
			refUi.drag_float("Inner Cone", light.inner_cone_angle_degrees, 0.1f, 0.0f, 180.0f);
			refUi.drag_float("Outer Cone", light.outer_cone_angle_degrees, 0.1f, 0.0f, 180.0f);

			if (DrawRemoveComponentButton(refUi, "Remove Light"))
			{
				state.draftLight.optCurrentValue.reset();
			}
		}
		else
		{
			refUi.text_unformatted("Light component will be removed after Apply.");
		}

		if (bHasPendingChanges)
		{
			DrawPendingChangeHint(refUi, "Pending changes. Apply or revert to update the entity.");
		}

		bool bApplyClicked = false;
		bool bRevertClicked = false;
		DrawApplyRevertRow(refUi, "Apply Light", "Revert Light", bHasPendingChanges, bHasPendingChanges, bApplyClicked, bRevertClicked);
		if (bApplyClicked)
		{
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
			else if (state.draftLight.optCurrentValue.has_value())
			{
				HLogWarning("InspectorPanel apply light clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_light_component()
					? entity.set_light_component(*state.draftLight.optCurrentValue)
					: entity.add_light_component(*state.draftLight.optCurrentValue);
			}
			else
			{
				HLogWarning("InspectorPanel apply light clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_light_component() && entity.remove_light_component();
			}

			if (bApplied)
			{
				state.draftLight.optOriginalValue = state.draftLight.optCurrentValue;
			}
			else
			{
				HLogWarning("InspectorPanel failed to apply Light changes. Entity={}.", uEntityId);
			}
		}
		if (bRevertClicked)
		{
			state.draftLight.optCurrentValue = state.draftLight.optOriginalValue;
		}
	}

	void InspectorPanel::DrawMeshSection(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		SyncMeshDraft(entity);
		const bool bHasPendingChanges = HasPendingMeshChanges();
		if (!state.draftMesh.optCurrentValue.has_value() && !bHasPendingChanges)
		{
			if (DrawAddComponentButton(refUi, "Add Mesh"))
			{
				state.draftMesh.optCurrentValue = AshEngine::MeshComponent{};
			}
			return;
		}

		const char* pSectionLabel = bHasPendingChanges ? "Mesh *" : "Mesh";
		if (!refUi.collapsing_header(pSectionLabel, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (state.draftMesh.optCurrentValue.has_value())
		{
			AshEngine::MeshComponent& mesh = *state.draftMesh.optCurrentValue;
			EditMeshPath(refUi, mesh);
			refUi.checkbox("Visible", mesh.visible);

			if (DrawRemoveComponentButton(refUi, "Remove Mesh"))
			{
				state.draftMesh.optCurrentValue.reset();
			}
		}
		else
		{
			refUi.text_unformatted("Mesh component will be removed after Apply.");
		}

		if (bHasPendingChanges)
		{
			DrawPendingChangeHint(refUi, "Pending changes. Apply or revert to update the entity.");
		}

		bool bApplyClicked = false;
		bool bRevertClicked = false;
		DrawApplyRevertRow(refUi, "Apply Mesh", "Revert Mesh", bHasPendingChanges, bHasPendingChanges, bApplyClicked, bRevertClicked);
		if (bApplyClicked)
		{
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
			else if (state.draftMesh.optCurrentValue.has_value())
			{
				HLogWarning("InspectorPanel apply mesh clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_mesh_component()
					? entity.set_mesh_component(*state.draftMesh.optCurrentValue)
					: entity.add_mesh_component(*state.draftMesh.optCurrentValue);
			}
			else
			{
				HLogWarning("InspectorPanel apply mesh clicked, but UndoRedoService is unavailable. Falling back to direct write (non-undoable). Entity={}.", uEntityId);
				bApplied = entity.has_mesh_component() && entity.remove_mesh_component();
			}

			if (bApplied)
			{
				state.draftMesh.optOriginalValue = state.draftMesh.optCurrentValue;
			}
			else
			{
				HLogWarning("InspectorPanel failed to apply Mesh changes. Entity={}.", uEntityId);
			}
		}
		if (bRevertClicked)
		{
			state.draftMesh.optCurrentValue = state.draftMesh.optOriginalValue;
		}
	}

	void InspectorPanel::DrawEntityInspector(AshEngine::UIContext& refUi, AshEngine::Entity entity)
	{
		if (!entity.is_valid())
		{
			DrawPanelIntro(refUi, "Entity Details", "The selected entity is no longer available in the active scene.");
			return;
		}

		DrawPanelIntro(refUi, "Entity Details", "Edit the selected entity and apply changes when you are ready.");
		DrawComponentSections(refUi, entity);
		DrawHierarchySection(refUi, entity);
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
		DrawSelectionSummary(refUi, refSelection);

		if (refSelection.eKind == EditorSelectionKind::Entity && _deps.pSceneService)
		{
			AshEngine::Entity entity = _deps.pSceneService->FindEntity(refSelection.uId);
			if (!entity.is_valid())
			{
				ResetEntityDrafts();
			}
			DrawEntityInspector(refUi, entity);
		}
		else if (refSelection.eKind == EditorSelectionKind::Asset && _deps.pAssetDatabaseService)
		{
			ResetEntityDrafts();
			DrawAssetInspector(_deps.pAssetDatabaseService, refUi, refSelection);
		}
		else
		{
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
