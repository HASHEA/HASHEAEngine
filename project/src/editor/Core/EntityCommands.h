#pragma once

#include "Core/EditorCommand.h"
#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <optional>
#include <string>
#include <vector>

namespace AshEditor
{
	class AssetDatabaseService;

	class RenameEntityCommand final : public EditorCommand
	{
	public:
		RenameEntityCommand(SceneEntityId uEntityId, std::string strNewName);
		RenameEntityCommand(SceneEntityId uEntityId, std::string strBeforeName, std::string strAfterName);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::string _strNewName{};
		std::string _strOldName{};
		bool _bHasCapturedOldName = false;
	};

	class TransformEntityCommand final : public EditorCommand
	{
	public:
		TransformEntityCommand(
			SceneEntityId uEntityId,
			AshEngine::TransformComponent beforeValue,
			AshEngine::TransformComponent afterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		AshEngine::TransformComponent _beforeValue{};
		AshEngine::TransformComponent _afterValue{};
	};

	class TransformEntitiesCommand final : public EditorCommand
	{
	public:
		TransformEntitiesCommand(
			std::vector<SceneEntityId> vecEntityIds,
			std::vector<AshEngine::TransformComponent> vecBeforeValues,
			std::vector<AshEngine::TransformComponent> vecAfterValues);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::vector<SceneEntityId> _vecEntityIds{};
		std::vector<AshEngine::TransformComponent> _vecBeforeValues{};
		std::vector<AshEngine::TransformComponent> _vecAfterValues{};
	};

	class SetCameraComponentCommand final : public EditorCommand
	{
	public:
		SetCameraComponentCommand(
			SceneEntityId uEntityId,
			std::optional<AshEngine::CameraComponent> optBeforeValue,
			std::optional<AshEngine::CameraComponent> optAfterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::optional<AshEngine::CameraComponent> _optBeforeValue{};
		std::optional<AshEngine::CameraComponent> _optAfterValue{};
	};

	class SetLightComponentCommand final : public EditorCommand
	{
	public:
		SetLightComponentCommand(
			SceneEntityId uEntityId,
			std::optional<AshEngine::LightComponent> optBeforeValue,
			std::optional<AshEngine::LightComponent> optAfterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::optional<AshEngine::LightComponent> _optBeforeValue{};
		std::optional<AshEngine::LightComponent> _optAfterValue{};
	};

	class SetMeshComponentCommand final : public EditorCommand
	{
	public:
		SetMeshComponentCommand(
			SceneEntityId uEntityId,
			std::optional<AshEngine::MeshComponent> optBeforeValue,
			std::optional<AshEngine::MeshComponent> optAfterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::optional<AshEngine::MeshComponent> _optBeforeValue{};
		std::optional<AshEngine::MeshComponent> _optAfterValue{};
	};

	class SetEnvironmentComponentCommand final : public EditorCommand
	{
	public:
		SetEnvironmentComponentCommand(
			SceneEntityId uEntityId,
			std::optional<AshEngine::EnvironmentComponent> optBeforeValue,
			std::optional<AshEngine::EnvironmentComponent> optAfterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::optional<AshEngine::EnvironmentComponent> _optBeforeValue{};
		std::optional<AshEngine::EnvironmentComponent> _optAfterValue{};
	};

	class SetParticleComponentCommand final : public EditorCommand
	{
	public:
		SetParticleComponentCommand(
			SceneEntityId uEntityId,
			std::optional<AshEngine::ParticleComponent> optBeforeValue,
			std::optional<AshEngine::ParticleComponent> optAfterValue);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		std::optional<AshEngine::ParticleComponent> _optBeforeValue{};
		std::optional<AshEngine::ParticleComponent> _optAfterValue{};
	};

	class CreateEntityCommand final : public EditorCommand
	{
	public:
		CreateEntityCommand(
			std::string strEntityName,
			SceneEntityId uParentId,
			uint32_t uSiblingIndex = kSceneAppendSiblingIndex);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::string _strEntityName{};
		SceneEntityId _uParentId = 0;
		uint32_t _uSiblingIndex = kSceneAppendSiblingIndex;
		SceneEntityId _uCreatedEntityId = 0;
	};

	class CreateMeshEntityFromAssetCommand final : public EditorCommand
	{
	public:
		CreateMeshEntityFromAssetCommand(
			std::string strEntityName,
			std::string strMeshAssetPath,
			SceneEntityId uParentId = 0,
			uint32_t uSiblingIndex = kSceneAppendSiblingIndex,
			bool bUseWorldTransform = false,
			AshEngine::TransformComponent worldTransform = {});

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::string _strEntityName{};
		AshEngine::MeshComponent _meshComponent{};
		SceneEntityId _uParentId = 0;
		uint32_t _uSiblingIndex = kSceneAppendSiblingIndex;
		bool _bUseWorldTransform = false;
		AshEngine::TransformComponent _worldTransform{};
		SceneEntityId _uCreatedEntityId = 0;
	};

	class InstantiateSceneAssetCommand final : public EditorCommand
	{
	public:
		InstantiateSceneAssetCommand(
			AssetDatabaseService* pAssetDatabaseService,
			uint64_t uAssetId,
			SceneEntityId uParentId = 0,
			bool bUseWorldTransform = false,
			AshEngine::TransformComponent worldTransform = {},
			std::string strRootNameOverride = {});

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		AssetDatabaseService* _pAssetDatabaseService = nullptr;
		uint64_t _uAssetId = 0;
		SceneEntityId _uParentId = 0;
		bool _bUseWorldTransform = false;
		AshEngine::TransformComponent _worldTransform{};
		std::string _strRootNameOverride{};
		SceneEntityId _uCreatedEntityId = 0;
		std::optional<SceneEntitySnapshot> _optSnapshot{};
	};

	class ReparentEntityCommand final : public EditorCommand
	{
	public:
		ReparentEntityCommand(
			SceneEntityId uEntityId,
			SceneEntityId uNewParentId,
			uint32_t uNewSiblingIndex = kSceneAppendSiblingIndex);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		bool TryMerge(const EditorCommand& refSubsequentCommand) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		SceneEntityId _uNewParentId = 0;
		uint32_t _uNewSiblingIndex = kSceneAppendSiblingIndex;
		SceneEntityId _uPreviousParentId = 0;
		uint32_t _uPreviousSiblingIndex = 0;
		bool _bHasCapturedPreviousParent = false;
	};

	class ReparentEntitiesCommand final : public EditorCommand
	{
	public:
		ReparentEntitiesCommand(
			std::vector<SceneEntityId> vecEntityIds,
			std::vector<SceneEntityId> vecNewParentIds,
			std::vector<uint32_t> vecNewSiblingIndices);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::vector<SceneEntityId> _vecEntityIds{};
		std::vector<SceneEntityId> _vecNewParentIds{};
		std::vector<uint32_t> _vecNewSiblingIndices{};
		std::vector<SceneEntityId> _vecPreviousParentIds{};
		std::vector<uint32_t> _vecPreviousSiblingIndices{};
		bool _bHasCapturedPreviousParents = false;
	};

	class DuplicateEntitiesCommand final : public EditorCommand
	{
	public:
		explicit DuplicateEntitiesCommand(std::vector<SceneEntityId> vecSourceEntityIds);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::vector<SceneEntityId> _vecSourceEntityIds{};
		std::vector<SceneEntityId> _vecCreatedRootEntityIds{};
		std::vector<SceneEntityId> _vecCreatedParentEntityIds{};
		std::vector<SceneEntitySnapshot> _vecCreatedSnapshots{};
	};

	class PasteEntitySnapshotsCommand final : public EditorCommand
	{
	public:
		PasteEntitySnapshotsCommand(
			std::vector<SceneEntitySnapshot> vecSnapshots,
			std::vector<SceneEntityId> vecPreferredParentEntityIds);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::vector<SceneEntitySnapshot> _vecSourceSnapshots{};
		std::vector<SceneEntityId> _vecPreferredParentEntityIds{};
		std::vector<SceneEntityId> _vecCreatedRootEntityIds{};
		std::vector<SceneEntityId> _vecCreatedParentEntityIds{};
		std::vector<SceneEntitySnapshot> _vecCreatedSnapshots{};
	};

	class DeleteEntityCommand final : public EditorCommand
	{
	public:
		explicit DeleteEntityCommand(SceneEntityId uEntityId);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		SceneEntityId _uEntityId = 0;
		SceneEntityId _uParentId = 0;
		std::optional<SceneEntitySnapshot> _optSnapshot{};
	};

	class DeleteEntitiesCommand final : public EditorCommand
	{
	public:
		explicit DeleteEntitiesCommand(std::vector<SceneEntityId> vecEntityIds);

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::vector<SceneEntityId> _vecEntityIds{};
		std::vector<SceneEntityId> _vecParentIds{};
		std::vector<SceneEntitySnapshot> _vecSnapshots{};
	};
}
