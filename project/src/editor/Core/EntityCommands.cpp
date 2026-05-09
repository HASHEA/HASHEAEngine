#include "Core/EntityCommands.h"

#include "Core/EditorComponentComparison.h"
#include "Core/EditorContext.h"
#include "Services/SceneService.h"

#include <optional>
#include <utility>

namespace AshEditor
{
	namespace
	{
		bool ApplyCameraComponentState(AshEngine::Entity entity, const std::optional<AshEngine::CameraComponent>& optValue)
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

		bool ApplyLightComponentState(AshEngine::Entity entity, const std::optional<AshEngine::LightComponent>& optValue)
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

		bool ApplyMeshComponentState(AshEngine::Entity entity, const std::optional<AshEngine::MeshComponent>& optValue)
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

		SceneEntityId GetEntityParentId(const AshEngine::Entity& refEntity)
		{
			const AshEngine::Entity parentEntity = refEntity.get_parent();
			return parentEntity.is_valid() ? parentEntity.get_id() : 0;
		}
	}

	RenameEntityCommand::RenameEntityCommand(SceneEntityId uEntityId, std::string strNewName)
		: _uEntityId(uEntityId)
		, _strNewName(std::move(strNewName))
	{
	}

	RenameEntityCommand::RenameEntityCommand(SceneEntityId uEntityId, std::string strBeforeName, std::string strAfterName)
		: _uEntityId(uEntityId)
		, _strNewName(std::move(strAfterName))
		, _strOldName(std::move(strBeforeName))
		, _bHasCapturedOldName(true)
	{
	}

	const char* RenameEntityCommand::GetLabel() const
	{
		return "Rename Entity";
	}

	bool RenameEntityCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		if (!_bHasCapturedOldName)
		{
			_strOldName = entity.get_name();
			_bHasCapturedOldName = true;
		}

		if (_strOldName == _strNewName)
		{
			return false;
		}

		return refContext.pSceneService->RenameEntity(_uEntityId, _strNewName);
	}

	bool RenameEntityCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _bHasCapturedOldName &&
			refContext.pSceneService->RenameEntity(_uEntityId, _strOldName);
	}

	bool RenameEntityCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const RenameEntityCommand* pSubsequent = dynamic_cast<const RenameEntityCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId || !_bHasCapturedOldName || !pSubsequent->_bHasCapturedOldName)
		{
			return false;
		}

		_strNewName = pSubsequent->_strNewName;
		return true;
	}

	EditorCommandSelection RenameEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection RenameEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	TransformEntityCommand::TransformEntityCommand(
		SceneEntityId uEntityId,
		AshEngine::TransformComponent beforeValue,
		AshEngine::TransformComponent afterValue)
		: _uEntityId(uEntityId)
		, _beforeValue(beforeValue)
		, _afterValue(afterValue)
	{
	}

	const char* TransformEntityCommand::GetLabel() const
	{
		return "Transform Entity";
	}

	bool TransformEntityCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0 || TransformComponentsEqual(_beforeValue, _afterValue))
		{
			return false;
		}

		AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		return entity.is_valid() && entity.set_transform_component(_afterValue);
	}

	bool TransformEntityCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService)
		{
			return false;
		}

		AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		return entity.is_valid() && entity.set_transform_component(_beforeValue);
	}

	bool TransformEntityCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const TransformEntityCommand* pSubsequent = dynamic_cast<const TransformEntityCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId)
		{
			return false;
		}

		_afterValue = pSubsequent->_afterValue;
		return true;
	}

	EditorCommandSelection TransformEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection TransformEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	SetCameraComponentCommand::SetCameraComponentCommand(
		SceneEntityId uEntityId,
		std::optional<AshEngine::CameraComponent> optBeforeValue,
		std::optional<AshEngine::CameraComponent> optAfterValue)
		: _uEntityId(uEntityId)
		, _optBeforeValue(std::move(optBeforeValue))
		, _optAfterValue(std::move(optAfterValue))
	{
	}

	const char* SetCameraComponentCommand::GetLabel() const
	{
		if (!_optBeforeValue.has_value() && _optAfterValue.has_value())
		{
			return "Add Camera Component";
		}
		if (_optBeforeValue.has_value() && !_optAfterValue.has_value())
		{
			return "Remove Camera Component";
		}
		return "Edit Camera Component";
	}

	bool SetCameraComponentCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0 ||
			OptionalComponentsEqual(_optBeforeValue, _optAfterValue, &CameraComponentsEqual))
		{
			return false;
		}

		return ApplyCameraComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optAfterValue);
	}

	bool SetCameraComponentCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uEntityId != 0 &&
			ApplyCameraComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optBeforeValue);
	}

	bool SetCameraComponentCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const SetCameraComponentCommand* pSubsequent =
			dynamic_cast<const SetCameraComponentCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId)
		{
			return false;
		}

		_optAfterValue = pSubsequent->_optAfterValue;
		return true;
	}

	EditorCommandSelection SetCameraComponentCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection SetCameraComponentCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	SetLightComponentCommand::SetLightComponentCommand(
		SceneEntityId uEntityId,
		std::optional<AshEngine::LightComponent> optBeforeValue,
		std::optional<AshEngine::LightComponent> optAfterValue)
		: _uEntityId(uEntityId)
		, _optBeforeValue(std::move(optBeforeValue))
		, _optAfterValue(std::move(optAfterValue))
	{
	}

	const char* SetLightComponentCommand::GetLabel() const
	{
		if (!_optBeforeValue.has_value() && _optAfterValue.has_value())
		{
			return "Add Light Component";
		}
		if (_optBeforeValue.has_value() && !_optAfterValue.has_value())
		{
			return "Remove Light Component";
		}
		return "Edit Light Component";
	}

	bool SetLightComponentCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0 ||
			OptionalComponentsEqual(_optBeforeValue, _optAfterValue, &LightComponentsEqual))
		{
			return false;
		}

		return ApplyLightComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optAfterValue);
	}

	bool SetLightComponentCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uEntityId != 0 &&
			ApplyLightComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optBeforeValue);
	}

	bool SetLightComponentCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const SetLightComponentCommand* pSubsequent =
			dynamic_cast<const SetLightComponentCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId)
		{
			return false;
		}

		_optAfterValue = pSubsequent->_optAfterValue;
		return true;
	}

	EditorCommandSelection SetLightComponentCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection SetLightComponentCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	SetMeshComponentCommand::SetMeshComponentCommand(
		SceneEntityId uEntityId,
		std::optional<AshEngine::MeshComponent> optBeforeValue,
		std::optional<AshEngine::MeshComponent> optAfterValue)
		: _uEntityId(uEntityId)
		, _optBeforeValue(std::move(optBeforeValue))
		, _optAfterValue(std::move(optAfterValue))
	{
	}

	const char* SetMeshComponentCommand::GetLabel() const
	{
		if (!_optBeforeValue.has_value() && _optAfterValue.has_value())
		{
			return "Add Mesh Component";
		}
		if (_optBeforeValue.has_value() && !_optAfterValue.has_value())
		{
			return "Remove Mesh Component";
		}
		return "Edit Mesh Component";
	}

	bool SetMeshComponentCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0 ||
			OptionalComponentsEqual(_optBeforeValue, _optAfterValue, &MeshComponentsEqual))
		{
			return false;
		}

		return ApplyMeshComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optAfterValue);
	}

	bool SetMeshComponentCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uEntityId != 0 &&
			ApplyMeshComponentState(refContext.pSceneService->FindEntity(_uEntityId), _optBeforeValue);
	}

	bool SetMeshComponentCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const SetMeshComponentCommand* pSubsequent =
			dynamic_cast<const SetMeshComponentCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId)
		{
			return false;
		}

		_optAfterValue = pSubsequent->_optAfterValue;
		return true;
	}

	EditorCommandSelection SetMeshComponentCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection SetMeshComponentCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	CreateEntityCommand::CreateEntityCommand(
		std::string strEntityName,
		SceneEntityId uParentId,
		uint32_t uSiblingIndex)
		: _strEntityName(std::move(strEntityName))
		, _uParentId(uParentId)
		, _uSiblingIndex(uSiblingIndex)
	{
	}

	const char* CreateEntityCommand::GetLabel() const
	{
		return "Create Entity";
	}

	bool CreateEntityCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService)
		{
			return false;
		}

		AshEngine::Entity entity = _uCreatedEntityId != 0
			? refContext.pSceneService->CreateEntityWithId(_uCreatedEntityId, _strEntityName, _uParentId, _uSiblingIndex)
			: refContext.pSceneService->CreateEntity(_strEntityName, _uParentId, _uSiblingIndex);
		if (!entity.is_valid())
		{
			return false;
		}

		_uCreatedEntityId = entity.get_id();
		if (_uSiblingIndex == kSceneAppendSiblingIndex)
		{
			_uSiblingIndex = refContext.pSceneService->GetEntitySiblingIndex(_uCreatedEntityId);
		}
		return true;
	}

	bool CreateEntityCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uCreatedEntityId != 0 &&
			refContext.pSceneService->DestroyEntity(_uCreatedEntityId);
	}

	EditorCommandSelection CreateEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uCreatedEntityId);
	}

	EditorCommandSelection CreateEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uParentId);
	}

	ReparentEntityCommand::ReparentEntityCommand(
		SceneEntityId uEntityId,
		SceneEntityId uNewParentId,
		uint32_t uNewSiblingIndex)
		: _uEntityId(uEntityId)
		, _uNewParentId(uNewParentId)
		, _uNewSiblingIndex(uNewSiblingIndex)
	{
	}

	const char* ReparentEntityCommand::GetLabel() const
	{
		return "Reparent Entity";
	}

	bool ReparentEntityCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		if (!_bHasCapturedPreviousParent)
		{
			_uPreviousParentId = GetEntityParentId(entity);
			_uPreviousSiblingIndex = refContext.pSceneService->GetEntitySiblingIndex(_uEntityId);
			_bHasCapturedPreviousParent = true;
		}

		if (_uPreviousParentId == _uNewParentId &&
			_uNewSiblingIndex != kSceneAppendSiblingIndex &&
			_uPreviousSiblingIndex == _uNewSiblingIndex)
		{
			return false;
		}

		if (_uPreviousParentId == _uNewParentId &&
			_uNewSiblingIndex == kSceneAppendSiblingIndex)
		{
			return false;
		}

		const uint32_t uExecuteSiblingIndex =
			_uNewSiblingIndex == kSceneAppendSiblingIndex && _uPreviousParentId != _uNewParentId
			? kSceneAppendSiblingIndex
			: _uNewSiblingIndex;
		if (!refContext.pSceneService->ReparentEntity(_uEntityId, _uNewParentId, uExecuteSiblingIndex))
		{
			return false;
		}

		if (_uNewSiblingIndex == kSceneAppendSiblingIndex)
		{
			_uNewSiblingIndex = refContext.pSceneService->GetEntitySiblingIndex(_uEntityId);
		}
		return true;
	}

	bool ReparentEntityCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _bHasCapturedPreviousParent &&
			refContext.pSceneService->ReparentEntity(_uEntityId, _uPreviousParentId, _uPreviousSiblingIndex);
	}

	bool ReparentEntityCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const ReparentEntityCommand* pSubsequent =
			dynamic_cast<const ReparentEntityCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_uEntityId != _uEntityId || !_bHasCapturedPreviousParent || !pSubsequent->_bHasCapturedPreviousParent)
		{
			return false;
		}

		_uNewParentId = pSubsequent->_uNewParentId;
		_uNewSiblingIndex = pSubsequent->_uNewSiblingIndex;
		return true;
	}

	EditorCommandSelection ReparentEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	EditorCommandSelection ReparentEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}

	DeleteEntityCommand::DeleteEntityCommand(SceneEntityId uEntityId)
		: _uEntityId(uEntityId)
	{
	}

	const char* DeleteEntityCommand::GetLabel() const
	{
		return "Delete Entity";
	}

	bool DeleteEntityCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _uEntityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		const std::optional<SceneEntitySnapshot> optSnapshot = refContext.pSceneService->CaptureEntitySnapshot(_uEntityId);
		if (!optSnapshot.has_value())
		{
			return false;
		}

		_optSnapshot = optSnapshot;
		_uParentId = GetEntityParentId(entity);
		return refContext.pSceneService->DestroyEntity(_uEntityId);
	}

	bool DeleteEntityCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService || !_optSnapshot.has_value())
		{
			return false;
		}

		AshEngine::Entity restored = refContext.pSceneService->RestoreEntitySnapshot(*_optSnapshot, _uParentId);
		if (!restored.is_valid())
		{
			return false;
		}

		_uEntityId = restored.get_id();
		return true;
	}

	EditorCommandSelection DeleteEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uParentId);
	}

	EditorCommandSelection DeleteEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uEntityId);
	}
}
