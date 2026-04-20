#pragma once
#include "Core/EditorCommand.h"
#include "Function/Scene/Scene.h"
#include "Services/SceneService.h"
#include <optional>
#include <string>

namespace AshEditor
{
	class RenameEntityCommand final : public EditorCommand
	{
	public:
		RenameEntityCommand(AshEngine::EntityId entity_id, std::string new_name);
		RenameEntityCommand(AshEngine::EntityId entity_id, std::string before_name, std::string after_name);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		std::string m_newName{};
		std::string m_oldName{};
		bool m_hasCapturedOldName = false;
	};

	class TransformEntityCommand final : public EditorCommand
	{
	public:
		TransformEntityCommand(
			AshEngine::EntityId entity_id,
			AshEngine::TransformComponent before_value,
			AshEngine::TransformComponent after_value);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		AshEngine::TransformComponent m_beforeValue{};
		AshEngine::TransformComponent m_afterValue{};
	};

	class SetCameraComponentCommand final : public EditorCommand
	{
	public:
		SetCameraComponentCommand(
			AshEngine::EntityId entity_id,
			std::optional<AshEngine::CameraComponent> before_value,
			std::optional<AshEngine::CameraComponent> after_value);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		std::optional<AshEngine::CameraComponent> m_beforeValue{};
		std::optional<AshEngine::CameraComponent> m_afterValue{};
	};

	class SetLightComponentCommand final : public EditorCommand
	{
	public:
		SetLightComponentCommand(
			AshEngine::EntityId entity_id,
			std::optional<AshEngine::LightComponent> before_value,
			std::optional<AshEngine::LightComponent> after_value);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		std::optional<AshEngine::LightComponent> m_beforeValue{};
		std::optional<AshEngine::LightComponent> m_afterValue{};
	};

	class SetMeshComponentCommand final : public EditorCommand
	{
	public:
		SetMeshComponentCommand(
			AshEngine::EntityId entity_id,
			std::optional<AshEngine::MeshComponent> before_value,
			std::optional<AshEngine::MeshComponent> after_value);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		std::optional<AshEngine::MeshComponent> m_beforeValue{};
		std::optional<AshEngine::MeshComponent> m_afterValue{};
	};

	class CreateEntityCommand final : public EditorCommand
	{
	public:
		CreateEntityCommand(std::string entity_name, AshEngine::EntityId parent_id);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		std::string m_entityName{};
		AshEngine::EntityId m_parentId = 0;
		AshEngine::EntityId m_createdEntityId = 0;
	};

	class ReparentEntityCommand final : public EditorCommand
	{
	public:
		ReparentEntityCommand(AshEngine::EntityId entity_id, AshEngine::EntityId new_parent_id);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		AshEngine::EntityId m_newParentId = 0;
		AshEngine::EntityId m_previousParentId = 0;
		bool m_hasCapturedPreviousParent = false;
	};

	class DeleteEntityCommand final : public EditorCommand
	{
	public:
		explicit DeleteEntityCommand(AshEngine::EntityId entity_id);

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;

	private:
		AshEngine::EntityId m_entityId = 0;
		AshEngine::EntityId m_parentId = 0;
		std::optional<SceneEntitySnapshot> m_snapshot{};
	};
}
