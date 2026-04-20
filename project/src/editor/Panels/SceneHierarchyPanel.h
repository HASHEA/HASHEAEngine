#pragma once
#include "Core/EditorPanel.h"
#include "Services/SceneService.h"
#include <cstdint>
#include <string>
#include <vector>

namespace AshEditor
{
	class SceneHierarchyPanel final : public EditorPanel
	{
	public:
		SceneHierarchyPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		void begin_rename_selected_entity(EditorContext& context);
		void begin_reparent_selected_entity(EditorContext& context);
		void begin_delete_selected_entity(EditorContext& context);
		void create_entity(EditorContext& context, EntityId parent_id);
		void destroy_selected_entity(EditorContext& context);
		void destroy_entity(EditorContext& context, EntityId entity_id);
		void draw_toolbar(EditorContext& context, EntityId selected_entity_id);
		void draw_rename_modal(EditorContext& context);
		void draw_reparent_modal(EditorContext& context);
		void draw_delete_modal(EditorContext& context);

	private:
		EntityId m_pendingRenameEntityId = 0;
		std::string m_pendingRenameValue{};
		EntityId m_pendingReparentEntityId = 0;
		int32_t m_pendingReparentIndex = 0;
		std::vector<EntityId> m_pendingReparentParentIds{};
		std::vector<std::string> m_pendingReparentParentLabels{};
		EntityId m_pendingDeleteEntityId = 0;
		std::string m_pendingDeleteEntityName{};
	};
}
