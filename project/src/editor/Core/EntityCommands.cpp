#include "Core/EntityCommands.h"

#include "Base/hlog.h"
#include "Core/EditorComponentComparison.h"
#include "Core/EditorContext.h"
#include "Core/SceneSnapshotUtils.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"

#include <optional>
#include <utility>
#include <vector>

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

		SceneEntityId ResolvePreferredParentId(SceneService& refSceneService, SceneEntityId uPreferredParentId)
		{
			if (uPreferredParentId == 0)
			{
				return 0;
			}

			const AshEngine::Entity parentEntity = refSceneService.FindEntity(uPreferredParentId);
			return parentEntity.is_valid() ? uPreferredParentId : 0;
		}

		void DestroyEntityRoots(SceneService& refSceneService, const std::vector<SceneEntityId>& vecEntityIds)
		{
			for (
				std::vector<SceneEntityId>::const_reverse_iterator itEntityId = vecEntityIds.rbegin();
				itEntityId != vecEntityIds.rend();
				++itEntityId)
			{
				if (*itEntityId != 0)
				{
					refSceneService.DestroyEntity(*itEntityId);
				}
			}
		}

		void ClearCreatedEntityState(
			std::vector<SceneEntityId>& refCreatedRootEntityIds,
			std::vector<SceneEntityId>& refCreatedParentEntityIds,
			std::vector<SceneEntitySnapshot>& refCreatedSnapshots)
		{
			refCreatedRootEntityIds.clear();
			refCreatedParentEntityIds.clear();
			refCreatedSnapshots.clear();
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

	TransformEntitiesCommand::TransformEntitiesCommand(
		std::vector<SceneEntityId> vecEntityIds,
		std::vector<AshEngine::TransformComponent> vecBeforeValues,
		std::vector<AshEngine::TransformComponent> vecAfterValues)
		: _vecEntityIds(std::move(vecEntityIds))
		, _vecBeforeValues(std::move(vecBeforeValues))
		, _vecAfterValues(std::move(vecAfterValues))
	{
	}

	const char* TransformEntitiesCommand::GetLabel() const
	{
		return "Transform Entities";
	}

	bool TransformEntitiesCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService ||
			_vecEntityIds.empty() ||
			_vecEntityIds.size() != _vecBeforeValues.size() ||
			_vecEntityIds.size() != _vecAfterValues.size())
		{
			return false;
		}

		size_t uAppliedCount = 0;
		for (size_t uIndex = 0; uIndex < _vecEntityIds.size(); ++uIndex)
		{
			AshEngine::Entity entity = refContext.pSceneService->FindEntity(_vecEntityIds[uIndex]);
			if (!entity.is_valid() || !entity.set_transform_component(_vecAfterValues[uIndex]))
			{
				for (size_t uRollbackIndex = uAppliedCount; uRollbackIndex > 0; --uRollbackIndex)
				{
					AshEngine::Entity rollbackEntity =
						refContext.pSceneService->FindEntity(_vecEntityIds[uRollbackIndex - 1u]);
					if (rollbackEntity.is_valid())
					{
						rollbackEntity.set_transform_component(_vecBeforeValues[uRollbackIndex - 1u]);
					}
				}
				return false;
			}
			++uAppliedCount;
		}
		return true;
	}

	bool TransformEntitiesCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService ||
			_vecEntityIds.empty() ||
			_vecEntityIds.size() != _vecBeforeValues.size() ||
			_vecEntityIds.size() != _vecAfterValues.size())
		{
			return false;
		}

		size_t uAppliedCount = 0;
		for (size_t uIndex = 0; uIndex < _vecEntityIds.size(); ++uIndex)
		{
			AshEngine::Entity entity = refContext.pSceneService->FindEntity(_vecEntityIds[uIndex]);
			if (!entity.is_valid() || !entity.set_transform_component(_vecBeforeValues[uIndex]))
			{
				for (size_t uRollbackIndex = uAppliedCount; uRollbackIndex > 0; --uRollbackIndex)
				{
					AshEngine::Entity rollbackEntity =
						refContext.pSceneService->FindEntity(_vecEntityIds[uRollbackIndex - 1u]);
					if (rollbackEntity.is_valid())
					{
						rollbackEntity.set_transform_component(_vecAfterValues[uRollbackIndex - 1u]);
					}
				}
				return false;
			}
			++uAppliedCount;
		}
		return true;
	}

	bool TransformEntitiesCommand::TryMerge(const EditorCommand& refSubsequentCommand)
	{
		const TransformEntitiesCommand* pSubsequent =
			dynamic_cast<const TransformEntitiesCommand*>(&refSubsequentCommand);
		if (!pSubsequent || pSubsequent->_vecEntityIds != _vecEntityIds)
		{
			return false;
		}

		_vecAfterValues = pSubsequent->_vecAfterValues;
		return true;
	}

	EditorCommandSelection TransformEntitiesCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entities(_vecEntityIds);
	}

	EditorCommandSelection TransformEntitiesCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entities(_vecEntityIds);
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
			HLogWarning("CreateEntityCommand skipped because scene_service is unavailable.");
			return false;
		}

		AshEngine::Entity entity = _uCreatedEntityId != 0
			? refContext.pSceneService->CreateEntityWithId(_uCreatedEntityId, _strEntityName, _uParentId, _uSiblingIndex)
			: refContext.pSceneService->CreateEntity(_strEntityName, _uParentId, _uSiblingIndex);
		if (!entity.is_valid())
		{
			HLogWarning(
				"CreateEntityCommand failed for '{}' (entity_id={}, parent={}, sibling_index={}).",
				_strEntityName,
				static_cast<unsigned long long>(_uCreatedEntityId),
				static_cast<unsigned long long>(_uParentId),
				static_cast<unsigned int>(_uSiblingIndex));
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
		const bool bDestroyed = refContext.pSceneService && _uCreatedEntityId != 0 &&
			refContext.pSceneService->DestroyEntity(_uCreatedEntityId);
		if (!bDestroyed)
		{
			HLogWarning(
				"CreateEntityCommand undo failed to destroy entity id={}.",
				static_cast<unsigned long long>(_uCreatedEntityId));
		}
		return bDestroyed;
	}

	EditorCommandSelection CreateEntityCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uCreatedEntityId);
	}

	EditorCommandSelection CreateEntityCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uParentId);
	}

	CreateMeshEntityFromAssetCommand::CreateMeshEntityFromAssetCommand(
		std::string strEntityName,
		std::string strMeshAssetPath,
		SceneEntityId uParentId,
		uint32_t uSiblingIndex,
		bool bUseWorldTransform,
		AshEngine::TransformComponent worldTransform)
		: _strEntityName(std::move(strEntityName))
		, _uParentId(uParentId)
		, _uSiblingIndex(uSiblingIndex)
		, _bUseWorldTransform(bUseWorldTransform)
		, _worldTransform(std::move(worldTransform))
	{
		_meshComponent.asset_path = std::move(strMeshAssetPath);
	}

	const char* CreateMeshEntityFromAssetCommand::GetLabel() const
	{
		return "Create Mesh Entity";
	}

	bool CreateMeshEntityFromAssetCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _strEntityName.empty() || _meshComponent.asset_path.empty())
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

		if (!entity.set_mesh_component(_meshComponent))
		{
			refContext.pSceneService->DestroyEntity(entity.get_id());
			return false;
		}

		if (_bUseWorldTransform && !entity.set_transform_component(_worldTransform))
		{
			refContext.pSceneService->DestroyEntity(entity.get_id());
			return false;
		}

		_uCreatedEntityId = entity.get_id();
		if (_uSiblingIndex == kSceneAppendSiblingIndex)
		{
			_uSiblingIndex = refContext.pSceneService->GetEntitySiblingIndex(_uCreatedEntityId);
		}
		return true;
	}

	bool CreateMeshEntityFromAssetCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uCreatedEntityId != 0 &&
			refContext.pSceneService->DestroyEntity(_uCreatedEntityId);
	}

	EditorCommandSelection CreateMeshEntityFromAssetCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uCreatedEntityId);
	}

	EditorCommandSelection CreateMeshEntityFromAssetCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entity(_uParentId);
	}

	InstantiateSceneAssetCommand::InstantiateSceneAssetCommand(
		AssetDatabaseService* pAssetDatabaseService,
		const uint64_t uAssetId,
		const SceneEntityId uParentId,
		const bool bUseWorldTransform,
		AshEngine::TransformComponent worldTransform,
		std::string strRootNameOverride)
		: _pAssetDatabaseService(pAssetDatabaseService)
		, _uAssetId(uAssetId)
		, _uParentId(uParentId)
		, _bUseWorldTransform(bUseWorldTransform)
		, _worldTransform(std::move(worldTransform))
		, _strRootNameOverride(std::move(strRootNameOverride))
	{
	}

	const char* InstantiateSceneAssetCommand::GetLabel() const
	{
		return "Instantiate Scene Asset";
	}

	bool InstantiateSceneAssetCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || !_pAssetDatabaseService || _uAssetId == 0)
		{
			return false;
		}

		if (_optSnapshot.has_value())
		{
			AshEngine::Entity restored = refContext.pSceneService->RestoreEntitySnapshot(*_optSnapshot, _uParentId);
			if (!restored.is_valid())
			{
				return false;
			}

			_uCreatedEntityId = restored.get_id();
			return true;
		}

		AshEngine::SceneInstantiationDesc desc{};
		desc.parent = _uParentId != 0
			? refContext.pSceneService->FindEntity(_uParentId)
			: AshEngine::Entity{};
		desc.use_world_transform = _bUseWorldTransform;
		desc.world_position = _worldTransform.position;
		desc.world_rotation_euler_degrees = _worldTransform.rotation_euler_degrees;
		desc.world_scale = _worldTransform.scale;
		desc.root_name_override = _strRootNameOverride;

		AshEngine::Entity entity = AshEngine::instantiate_asset(
			refContext.pSceneService->GetActiveScene(),
			_pAssetDatabaseService->GetDatabase(),
			_uAssetId,
			desc);
		if (!entity.is_valid())
		{
			return false;
		}

		_uCreatedEntityId = entity.get_id();
		_optSnapshot = refContext.pSceneService->CaptureEntitySnapshot(_uCreatedEntityId);
		return _optSnapshot.has_value();
	}

	bool InstantiateSceneAssetCommand::Undo(EditorContext& refContext)
	{
		return refContext.pSceneService && _uCreatedEntityId != 0 &&
			refContext.pSceneService->DestroyEntity(_uCreatedEntityId);
	}

	EditorCommandSelection InstantiateSceneAssetCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entity(_uCreatedEntityId);
	}

	EditorCommandSelection InstantiateSceneAssetCommand::GetSelectionAfterUndo() const
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

	DuplicateEntitiesCommand::DuplicateEntitiesCommand(std::vector<SceneEntityId> vecSourceEntityIds)
		: _vecSourceEntityIds(std::move(vecSourceEntityIds))
	{
	}

	const char* DuplicateEntitiesCommand::GetLabel() const
	{
		return "Duplicate Entities";
	}

	bool DuplicateEntitiesCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _vecSourceEntityIds.empty())
		{
			return false;
		}

		if (!_vecCreatedSnapshots.empty())
		{
			_vecCreatedRootEntityIds.clear();
			for (size_t uIndex = 0; uIndex < _vecCreatedSnapshots.size(); ++uIndex)
			{
				const SceneEntityId uParentId =
					uIndex < _vecCreatedParentEntityIds.size()
					? _vecCreatedParentEntityIds[uIndex]
					: 0;
				AshEngine::Entity restored =
					refContext.pSceneService->RestoreEntitySnapshot(_vecCreatedSnapshots[uIndex], uParentId);
				if (!restored.is_valid())
				{
					DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
					return false;
				}
				_vecCreatedRootEntityIds.push_back(restored.get_id());
			}
			return true;
		}

		_vecCreatedRootEntityIds.clear();
		_vecCreatedParentEntityIds.clear();
		_vecCreatedSnapshots.clear();
		for (const SceneEntityId uSourceEntityId : _vecSourceEntityIds)
		{
			const AshEngine::Entity sourceEntity = refContext.pSceneService->FindEntity(uSourceEntityId);
			if (!sourceEntity.is_valid())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}

			const std::optional<SceneEntitySnapshot> optSourceSnapshot =
				refContext.pSceneService->CaptureEntitySnapshot(uSourceEntityId);
			if (!optSourceSnapshot.has_value())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}

			const SceneEntityId uParentId = GetEntityParentId(sourceEntity);
			const uint32_t uSiblingIndex = refContext.pSceneService->GetEntitySiblingIndex(uSourceEntityId) + 1u;
			AshEngine::Entity duplicated = SceneSnapshotUtils::RestoreEntitySnapshotAsCopy(
				refContext.pSceneService->GetActiveScene(),
				*optSourceSnapshot,
				uParentId,
				uSiblingIndex,
				nullptr,
				" Copy");
			if (!duplicated.is_valid())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}

			_vecCreatedRootEntityIds.push_back(duplicated.get_id());
			_vecCreatedParentEntityIds.push_back(uParentId);
			const std::optional<SceneEntitySnapshot> optCreatedSnapshot =
				refContext.pSceneService->CaptureEntitySnapshot(duplicated.get_id());
			if (!optCreatedSnapshot.has_value())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}
			_vecCreatedSnapshots.push_back(*optCreatedSnapshot);
		}

		return !_vecCreatedRootEntityIds.empty();
	}

	bool DuplicateEntitiesCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _vecCreatedRootEntityIds.empty())
		{
			return false;
		}

		DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
		return true;
	}

	EditorCommandSelection DuplicateEntitiesCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entities(_vecCreatedRootEntityIds);
	}

	EditorCommandSelection DuplicateEntitiesCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Entities(_vecSourceEntityIds);
	}

	PasteEntitySnapshotsCommand::PasteEntitySnapshotsCommand(
		std::vector<SceneEntitySnapshot> vecSnapshots,
		std::vector<SceneEntityId> vecPreferredParentEntityIds)
		: _vecSourceSnapshots(std::move(vecSnapshots))
		, _vecPreferredParentEntityIds(std::move(vecPreferredParentEntityIds))
	{
	}

	const char* PasteEntitySnapshotsCommand::GetLabel() const
	{
		return "Paste Entities";
	}

	bool PasteEntitySnapshotsCommand::Execute(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _vecSourceSnapshots.empty())
		{
			return false;
		}

		if (!_vecCreatedSnapshots.empty())
		{
			_vecCreatedRootEntityIds.clear();
			for (size_t uIndex = 0; uIndex < _vecCreatedSnapshots.size(); ++uIndex)
			{
				const SceneEntityId uParentId =
					uIndex < _vecCreatedParentEntityIds.size()
					? ResolvePreferredParentId(*refContext.pSceneService, _vecCreatedParentEntityIds[uIndex])
					: 0;
				AshEngine::Entity restored =
					refContext.pSceneService->RestoreEntitySnapshot(_vecCreatedSnapshots[uIndex], uParentId);
				if (!restored.is_valid())
				{
					DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
					return false;
				}
				_vecCreatedRootEntityIds.push_back(restored.get_id());
			}
			return true;
		}

		ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
		for (size_t uIndex = 0; uIndex < _vecSourceSnapshots.size(); ++uIndex)
		{
			const SceneEntitySnapshot& refSourceSnapshot = _vecSourceSnapshots[uIndex];
			const SceneEntityId uPreferredParentId =
				uIndex < _vecPreferredParentEntityIds.size()
				? _vecPreferredParentEntityIds[uIndex]
				: 0;
			const SceneEntityId uParentId = ResolvePreferredParentId(*refContext.pSceneService, uPreferredParentId);
			AshEngine::Entity pasted = SceneSnapshotUtils::RestoreEntitySnapshotAsCopy(
				refContext.pSceneService->GetActiveScene(),
				refSourceSnapshot,
				uParentId,
				kSceneAppendSiblingIndex,
				nullptr,
				" Copy");
			if (!pasted.is_valid())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}

			_vecCreatedRootEntityIds.push_back(pasted.get_id());
			_vecCreatedParentEntityIds.push_back(uParentId);
			const std::optional<SceneEntitySnapshot> optCreatedSnapshot =
				refContext.pSceneService->CaptureEntitySnapshot(pasted.get_id());
			if (!optCreatedSnapshot.has_value())
			{
				DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
				ClearCreatedEntityState(_vecCreatedRootEntityIds, _vecCreatedParentEntityIds, _vecCreatedSnapshots);
				return false;
			}
			_vecCreatedSnapshots.push_back(*optCreatedSnapshot);
		}

		return !_vecCreatedRootEntityIds.empty();
	}

	bool PasteEntitySnapshotsCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService || _vecCreatedRootEntityIds.empty())
		{
			return false;
		}

		DestroyEntityRoots(*refContext.pSceneService, _vecCreatedRootEntityIds);
		return true;
	}

	EditorCommandSelection PasteEntitySnapshotsCommand::GetSelectionAfterExecute() const
	{
		return EditorCommandSelection::Entities(_vecCreatedRootEntityIds);
	}

	EditorCommandSelection PasteEntitySnapshotsCommand::GetSelectionAfterUndo() const
	{
		return EditorCommandSelection::Clear();
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
			HLogWarning(
				"DeleteEntityCommand skipped (scene_service={}, entity_id={}).",
				refContext.pSceneService != nullptr,
				static_cast<unsigned long long>(_uEntityId));
			return false;
		}

		const AshEngine::Entity entity = refContext.pSceneService->FindEntity(_uEntityId);
		if (!entity.is_valid())
		{
			HLogWarning(
				"DeleteEntityCommand failed because entity id={} is invalid.",
				static_cast<unsigned long long>(_uEntityId));
			return false;
		}

		const std::optional<SceneEntitySnapshot> optSnapshot = refContext.pSceneService->CaptureEntitySnapshot(_uEntityId);
		if (!optSnapshot.has_value())
		{
			HLogWarning(
				"DeleteEntityCommand failed to capture snapshot for entity id={}.",
				static_cast<unsigned long long>(_uEntityId));
			return false;
		}

		_optSnapshot = optSnapshot;
		_uParentId = GetEntityParentId(entity);
		const bool bDestroyed = refContext.pSceneService->DestroyEntity(_uEntityId);
		if (!bDestroyed)
		{
			HLogWarning(
				"DeleteEntityCommand failed to destroy entity id={}.",
				static_cast<unsigned long long>(_uEntityId));
		}
		return bDestroyed;
	}

	bool DeleteEntityCommand::Undo(EditorContext& refContext)
	{
		if (!refContext.pSceneService || !_optSnapshot.has_value())
		{
			HLogWarning(
				"DeleteEntityCommand undo skipped (scene_service={}, has_snapshot={}).",
				refContext.pSceneService != nullptr,
				_optSnapshot.has_value());
			return false;
		}

		AshEngine::Entity restored = refContext.pSceneService->RestoreEntitySnapshot(*_optSnapshot, _uParentId);
		if (!restored.is_valid())
		{
			HLogWarning(
				"DeleteEntityCommand undo failed to restore entity id={} under parent={}.",
				static_cast<unsigned long long>(_uEntityId),
				static_cast<unsigned long long>(_uParentId));
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
