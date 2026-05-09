#pragma once

#include "Core/EditorCommand.h"
#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <optional>
#include <string>

namespace AshEditor
{
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
}
