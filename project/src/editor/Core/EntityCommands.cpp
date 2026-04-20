#include "Core/EntityCommands.h"
#include "Core/EditorContext.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include <optional>
#include <utility>

namespace AshEditor
{
	namespace
	{
		void select_entity_if_available(EditorContext& context, AshEngine::EntityId entity_id)
		{
			if (!context.selection_service)
			{
				return;
			}

			if (entity_id == 0)
			{
				context.selection_service->clear();
				return;
			}

			const AshEngine::Entity entity = context.scene_service ? context.scene_service->find_entity(entity_id) : AshEngine::Entity{};
			if (entity.is_valid())
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
			}
			else
			{
				context.selection_service->clear();
			}
		}

		bool transform_components_equal(
			const AshEngine::TransformComponent& lhs,
			const AshEngine::TransformComponent& rhs)
		{
			return
				lhs.position == rhs.position &&
				lhs.rotation_euler_degrees == rhs.rotation_euler_degrees &&
				lhs.scale == rhs.scale;
		}

		bool camera_components_equal(
			const AshEngine::CameraComponent& lhs,
			const AshEngine::CameraComponent& rhs)
		{
			return
				lhs.primary == rhs.primary &&
				lhs.projection == rhs.projection &&
				lhs.fov_y_degrees == rhs.fov_y_degrees &&
				lhs.near_plane == rhs.near_plane &&
				lhs.far_plane == rhs.far_plane &&
				lhs.orthographic_height == rhs.orthographic_height;
		}

		bool light_components_equal(
			const AshEngine::LightComponent& lhs,
			const AshEngine::LightComponent& rhs)
		{
			return
				lhs.type == rhs.type &&
				lhs.color == rhs.color &&
				lhs.intensity == rhs.intensity &&
				lhs.range == rhs.range &&
				lhs.inner_cone_angle_degrees == rhs.inner_cone_angle_degrees &&
				lhs.outer_cone_angle_degrees == rhs.outer_cone_angle_degrees;
		}

		bool mesh_components_equal(
			const AshEngine::MeshComponent& lhs,
			const AshEngine::MeshComponent& rhs)
		{
			return
				lhs.asset_path == rhs.asset_path &&
				lhs.mesh_index == rhs.mesh_index &&
				lhs.visible == rhs.visible &&
				lhs.mobility == rhs.mobility &&
				lhs.layer_mask == rhs.layer_mask;
		}

		template<typename Component>
		bool optional_components_equal(
			const std::optional<Component>& lhs,
			const std::optional<Component>& rhs,
			bool (*equals_fn)(const Component&, const Component&))
		{
			if (lhs.has_value() != rhs.has_value())
			{
				return false;
			}

			if (!lhs.has_value())
			{
				return true;
			}

			return equals_fn(*lhs, *rhs);
		}

		bool apply_camera_component_state(AshEngine::Entity entity, const std::optional<AshEngine::CameraComponent>& value)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!value.has_value())
			{
				return entity.has_camera_component() && entity.remove_camera_component();
			}

			return entity.has_camera_component()
				? entity.set_camera_component(*value)
				: entity.add_camera_component(*value);
		}

		bool apply_light_component_state(AshEngine::Entity entity, const std::optional<AshEngine::LightComponent>& value)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!value.has_value())
			{
				return entity.has_light_component() && entity.remove_light_component();
			}

			return entity.has_light_component()
				? entity.set_light_component(*value)
				: entity.add_light_component(*value);
		}

		bool apply_mesh_component_state(AshEngine::Entity entity, const std::optional<AshEngine::MeshComponent>& value)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			if (!value.has_value())
			{
				return entity.has_mesh_component() && entity.remove_mesh_component();
			}

			return entity.has_mesh_component()
				? entity.set_mesh_component(*value)
				: entity.add_mesh_component(*value);
		}

		AshEngine::EntityId get_entity_parent_id(const AshEngine::Entity& entity)
		{
			const AshEngine::Entity parent = entity.get_parent();
			return parent.is_valid() ? parent.get_id() : 0;
		}
	}

	RenameEntityCommand::RenameEntityCommand(AshEngine::EntityId entity_id, std::string new_name)
		: m_entityId(entity_id)
		, m_newName(std::move(new_name))
	{
	}

	RenameEntityCommand::RenameEntityCommand(AshEngine::EntityId entity_id, std::string before_name, std::string after_name)
		: m_entityId(entity_id)
		, m_newName(std::move(after_name))
		, m_oldName(std::move(before_name))
		, m_hasCapturedOldName(true)
	{
	}

	const char* RenameEntityCommand::get_label() const
	{
		return "Rename Entity";
	}

	bool RenameEntityCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!entity.is_valid())
		{
			return false;
		}

		if (!m_hasCapturedOldName)
		{
			m_oldName = entity.get_name();
			m_hasCapturedOldName = true;
		}

		if (m_oldName == m_newName)
		{
			return false;
		}

		if (!context.scene_service->rename_entity(m_entityId, m_newName))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool RenameEntityCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || !m_hasCapturedOldName)
		{
			return false;
		}

		if (context.scene_service->rename_entity(m_entityId, m_oldName))
		{
			select_entity_if_available(context, m_entityId);
			return true;
		}
		return false;
	}

	TransformEntityCommand::TransformEntityCommand(
		AshEngine::EntityId entity_id,
		AshEngine::TransformComponent before_value,
		AshEngine::TransformComponent after_value)
		: m_entityId(entity_id)
		, m_beforeValue(before_value)
		, m_afterValue(after_value)
	{
	}

	const char* TransformEntityCommand::get_label() const
	{
		return "Transform Entity";
	}

	bool TransformEntityCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0 || transform_components_equal(m_beforeValue, m_afterValue))
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!entity.is_valid() || !entity.set_transform_component(m_afterValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool TransformEntityCommand::undo(EditorContext& context)
	{
		if (!context.scene_service)
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!entity.is_valid() || !entity.set_transform_component(m_beforeValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	SetCameraComponentCommand::SetCameraComponentCommand(
		AshEngine::EntityId entity_id,
		std::optional<AshEngine::CameraComponent> before_value,
		std::optional<AshEngine::CameraComponent> after_value)
		: m_entityId(entity_id)
		, m_beforeValue(std::move(before_value))
		, m_afterValue(std::move(after_value))
	{
	}

	const char* SetCameraComponentCommand::get_label() const
	{
		if (!m_beforeValue.has_value() && m_afterValue.has_value())
		{
			return "Add Camera Component";
		}
		if (m_beforeValue.has_value() && !m_afterValue.has_value())
		{
			return "Remove Camera Component";
		}
		return "Edit Camera Component";
	}

	bool SetCameraComponentCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0 ||
			optional_components_equal(m_beforeValue, m_afterValue, &camera_components_equal))
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_camera_component_state(entity, m_afterValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool SetCameraComponentCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_camera_component_state(entity, m_beforeValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	SetLightComponentCommand::SetLightComponentCommand(
		AshEngine::EntityId entity_id,
		std::optional<AshEngine::LightComponent> before_value,
		std::optional<AshEngine::LightComponent> after_value)
		: m_entityId(entity_id)
		, m_beforeValue(std::move(before_value))
		, m_afterValue(std::move(after_value))
	{
	}

	const char* SetLightComponentCommand::get_label() const
	{
		if (!m_beforeValue.has_value() && m_afterValue.has_value())
		{
			return "Add Light Component";
		}
		if (m_beforeValue.has_value() && !m_afterValue.has_value())
		{
			return "Remove Light Component";
		}
		return "Edit Light Component";
	}

	bool SetLightComponentCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0 ||
			optional_components_equal(m_beforeValue, m_afterValue, &light_components_equal))
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_light_component_state(entity, m_afterValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool SetLightComponentCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_light_component_state(entity, m_beforeValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	SetMeshComponentCommand::SetMeshComponentCommand(
		AshEngine::EntityId entity_id,
		std::optional<AshEngine::MeshComponent> before_value,
		std::optional<AshEngine::MeshComponent> after_value)
		: m_entityId(entity_id)
		, m_beforeValue(std::move(before_value))
		, m_afterValue(std::move(after_value))
	{
	}

	const char* SetMeshComponentCommand::get_label() const
	{
		if (!m_beforeValue.has_value() && m_afterValue.has_value())
		{
			return "Add Mesh Component";
		}
		if (m_beforeValue.has_value() && !m_afterValue.has_value())
		{
			return "Remove Mesh Component";
		}
		return "Edit Mesh Component";
	}

	bool SetMeshComponentCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0 ||
			optional_components_equal(m_beforeValue, m_afterValue, &mesh_components_equal))
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_mesh_component_state(entity, m_afterValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool SetMeshComponentCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!apply_mesh_component_state(entity, m_beforeValue))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	CreateEntityCommand::CreateEntityCommand(std::string entity_name, AshEngine::EntityId parent_id)
		: m_entityName(std::move(entity_name))
		, m_parentId(parent_id)
	{
	}

	const char* CreateEntityCommand::get_label() const
	{
		return "Create Entity";
	}

	bool CreateEntityCommand::execute(EditorContext& context)
	{
		if (!context.scene_service)
		{
			return false;
		}

		AshEngine::Entity entity = context.scene_service->create_entity(m_entityName, m_parentId);
		if (!entity.is_valid())
		{
			return false;
		}

		m_createdEntityId = entity.get_id();
		select_entity_if_available(context, m_createdEntityId);
		return true;
	}

	bool CreateEntityCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || m_createdEntityId == 0)
		{
			return false;
		}

		if (!context.scene_service->destroy_entity(m_createdEntityId))
		{
			return false;
		}

		select_entity_if_available(context, m_parentId);
		return true;
	}

	ReparentEntityCommand::ReparentEntityCommand(AshEngine::EntityId entity_id, AshEngine::EntityId new_parent_id)
		: m_entityId(entity_id)
		, m_newParentId(new_parent_id)
	{
	}

	const char* ReparentEntityCommand::get_label() const
	{
		return "Reparent Entity";
	}

	bool ReparentEntityCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!entity.is_valid())
		{
			return false;
		}

		if (!m_hasCapturedPreviousParent)
		{
			m_previousParentId = get_entity_parent_id(entity);
			m_hasCapturedPreviousParent = true;
		}

		if (m_previousParentId == m_newParentId)
		{
			return false;
		}

		if (!context.scene_service->reparent_entity(m_entityId, m_newParentId))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	bool ReparentEntityCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || !m_hasCapturedPreviousParent)
		{
			return false;
		}

		if (!context.scene_service->reparent_entity(m_entityId, m_previousParentId))
		{
			return false;
		}

		select_entity_if_available(context, m_entityId);
		return true;
	}

	DeleteEntityCommand::DeleteEntityCommand(AshEngine::EntityId entity_id)
		: m_entityId(entity_id)
	{
	}

	const char* DeleteEntityCommand::get_label() const
	{
		return "Delete Entity";
	}

	bool DeleteEntityCommand::execute(EditorContext& context)
	{
		if (!context.scene_service || m_entityId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(m_entityId);
		if (!entity.is_valid())
		{
			return false;
		}

		const std::optional<SceneEntitySnapshot> snapshot = context.scene_service->capture_entity_snapshot(m_entityId);
		if (!snapshot.has_value())
		{
			return false;
		}

		m_snapshot = snapshot;
		m_parentId = get_entity_parent_id(entity);
		if (!context.scene_service->destroy_entity(m_entityId))
		{
			return false;
		}

		select_entity_if_available(context, m_parentId);
		return true;
	}

	bool DeleteEntityCommand::undo(EditorContext& context)
	{
		if (!context.scene_service || !m_snapshot.has_value())
		{
			return false;
		}

		AshEngine::Entity restored = context.scene_service->restore_entity_snapshot(*m_snapshot, m_parentId);
		if (!restored.is_valid())
		{
			return false;
		}

		m_entityId = restored.get_id();
		select_entity_if_available(context, m_entityId);
		return true;
	}
}
