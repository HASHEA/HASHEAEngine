#include "Panels/InspectorPanel.h"

#include "Base/hlog.h"
#include "Core/EditorComponentComparison.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Panels/Inspector/InspectorPanelSupport.h"

#include <memory>
#include <optional>

namespace AshEditor
{
	void InspectorPanel::ResetEntityDrafts()
	{
		InspectorPanelState& state = GetState();
		state.draftIdentity = {};
		state.draftTransform = {};
		state.draftCamera = {};
		state.draftLight = {};
		state.draftMesh = {};
		state.draftEnvironment = {};
	}

	void InspectorPanel::ResetIdentityDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftIdentity.uEntityId = entity.get_id();
		state.draftIdentity.strOriginalName = entity.get_name();
		state.draftIdentity.strCurrentName = state.draftIdentity.strOriginalName;
	}

	void InspectorPanel::ResetTransformDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		const AshEngine::TransformComponent liveTransform = entity.get_transform_component();
		state.draftTransform.uEntityId = entity.get_id();
		state.draftTransform.originalValue = liveTransform;
		state.draftTransform.currentValue = liveTransform;
	}

	void InspectorPanel::ResetTransformDraftToDefaults(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftTransform.uEntityId = entity.get_id();
		state.draftTransform.currentValue = AshEngine::TransformComponent{};
		SanitizeTransformComponent(state.draftTransform.currentValue);
	}

	void InspectorPanel::ResetCameraDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftCamera.uEntityId = entity.get_id();
		state.draftCamera.optOriginalValue = GetCameraComponentValue(entity);
		state.draftCamera.optCurrentValue = state.draftCamera.optOriginalValue;
	}

	void InspectorPanel::ResetCameraDraftToDefaults(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftCamera.uEntityId = entity.get_id();
		state.draftCamera.optCurrentValue = AshEngine::CameraComponent{};
		SanitizeOptionalCameraComponent(state.draftCamera.optCurrentValue);
	}

	void InspectorPanel::ResetLightDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftLight.uEntityId = entity.get_id();
		state.draftLight.optOriginalValue = GetLightComponentValue(entity);
		state.draftLight.optCurrentValue = state.draftLight.optOriginalValue;
	}

	void InspectorPanel::ResetLightDraftToDefaults(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftLight.uEntityId = entity.get_id();
		state.draftLight.optCurrentValue = AshEngine::LightComponent{};
		SanitizeOptionalLightComponent(state.draftLight.optCurrentValue);
	}

	void InspectorPanel::ResetMeshDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftMesh.uEntityId = entity.get_id();
		state.draftMesh.optOriginalValue = GetMeshComponentValue(entity);
		state.draftMesh.optCurrentValue = state.draftMesh.optOriginalValue;
	}

	void InspectorPanel::ResetMeshDraftToDefaults(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftMesh.uEntityId = entity.get_id();
		AshEngine::MeshComponent meshDefaults{};
		AshEngine::MeshComponent meshValue = state.draftMesh.optCurrentValue.value_or(meshDefaults);
		meshValue.visible = meshDefaults.visible;
		meshValue.mesh_index = meshDefaults.mesh_index;
		meshValue.mobility = meshDefaults.mobility;
		meshValue.layer_mask = meshDefaults.layer_mask;
		state.draftMesh.optCurrentValue = meshValue;
		SanitizeOptionalMeshComponent(state.draftMesh.optCurrentValue);
	}

	void InspectorPanel::ResetEnvironmentDraftToLive(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftEnvironment.uEntityId = entity.get_id();
		state.draftEnvironment.optOriginalValue = GetEnvironmentComponentValue(entity);
		state.draftEnvironment.optCurrentValue = state.draftEnvironment.optOriginalValue;
	}

	void InspectorPanel::ResetEnvironmentDraftToDefaults(const AshEngine::Entity& entity)
	{
		if (!entity.is_valid())
		{
			return;
		}

		InspectorPanelState& state = GetState();
		state.draftEnvironment.uEntityId = entity.get_id();
		state.draftEnvironment.optCurrentValue = AshEngine::EnvironmentComponent{};
		SanitizeOptionalEnvironmentComponent(state.draftEnvironment.optCurrentValue);
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
		else if (
			state.draftTransform.currentValue.position == state.draftTransform.originalValue.position &&
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
		else if (OptionalComponentsEqual(
			state.draftCamera.optCurrentValue,
			state.draftCamera.optOriginalValue,
			&CameraComponentsEqual))
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
		else if (OptionalComponentsEqual(
			state.draftLight.optCurrentValue,
			state.draftLight.optOriginalValue,
			&LightComponentsEqual))
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
		else if (OptionalComponentsEqual(
			state.draftMesh.optCurrentValue,
			state.draftMesh.optOriginalValue,
			&MeshComponentsEqual))
		{
			state.draftMesh.optOriginalValue = optLiveValue;
			state.draftMesh.optCurrentValue = optLiveValue;
		}
	}

	void InspectorPanel::SyncEnvironmentDraft(const AshEngine::Entity& refEntity)
	{
		InspectorPanelState& state = GetState();
		const std::optional<AshEngine::EnvironmentComponent> optLiveValue = GetEnvironmentComponentValue(refEntity);
		if (state.draftEnvironment.uEntityId != refEntity.get_id())
		{
			state.draftEnvironment.uEntityId = refEntity.get_id();
			state.draftEnvironment.optOriginalValue = optLiveValue;
			state.draftEnvironment.optCurrentValue = optLiveValue;
		}
		else if (OptionalComponentsEqual(
			state.draftEnvironment.optCurrentValue,
			state.draftEnvironment.optOriginalValue,
			&EnvironmentComponentsEqual))
		{
			state.draftEnvironment.optOriginalValue = optLiveValue;
			state.draftEnvironment.optCurrentValue = optLiveValue;
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
		return !OptionalComponentsEqual(
			state.draftCamera.optCurrentValue,
			state.draftCamera.optOriginalValue,
			&CameraComponentsEqual);
	}

	bool InspectorPanel::HasPendingLightChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(
			state.draftLight.optCurrentValue,
			state.draftLight.optOriginalValue,
			&LightComponentsEqual);
	}

	bool InspectorPanel::HasPendingMeshChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(
			state.draftMesh.optCurrentValue,
			state.draftMesh.optOriginalValue,
			&MeshComponentsEqual);
	}

	bool InspectorPanel::HasPendingEnvironmentChanges() const
	{
		const InspectorPanelState& state = GetState();
		return !OptionalComponentsEqual(
			state.draftEnvironment.optCurrentValue,
			state.draftEnvironment.optOriginalValue,
			&EnvironmentComponentsEqual);
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

		if (state.draftMesh.optCurrentValue.has_value() &&
			!state.draftMesh.optCurrentValue->asset_path.empty() &&
			(!state.draftMesh.optOriginalValue.has_value() || state.draftMesh.optOriginalValue->asset_path.empty()))
		{
			// Promote placeholder mesh components to visible once the first valid asset path is assigned.
			state.draftMesh.optCurrentValue->visible = true;
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

	bool InspectorPanel::CommitEnvironmentDraft(AshEngine::Entity entity)
	{
		InspectorPanelState& state = GetState();
		if (!entity.is_valid() || !HasPendingEnvironmentChanges())
		{
			return false;
		}

		const unsigned long long uEntityId = static_cast<unsigned long long>(entity.get_id());
		bool bApplied = false;
		if (_deps.pCommandExecutor)
		{
			bApplied = _deps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetEnvironmentComponentCommand>(
					entity.get_id(),
					state.draftEnvironment.optOriginalValue,
					state.draftEnvironment.optCurrentValue));
		}
		else
		{
			HLogWarning(
				"InspectorPanel immediate environment edit has no command executor. Falling back to direct write (non-undoable). Entity={}.",
				uEntityId);
			bApplied = ApplyEnvironmentComponentValue(entity, state.draftEnvironment.optCurrentValue);
		}

		if (bApplied)
		{
			state.draftEnvironment.optOriginalValue = state.draftEnvironment.optCurrentValue;
			return true;
		}

		HLogWarning("InspectorPanel failed to commit Environment changes. Entity={}.", uEntityId);
		state.draftEnvironment.optCurrentValue = state.draftEnvironment.optOriginalValue;
		return false;
	}
}
