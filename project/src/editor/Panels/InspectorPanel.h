#pragma once
#include "Core/EditorPanel.h"
#include "Function/Scene/Scene.h"
#include "Function/Scene/SceneComponents.h"
#include <optional>
#include <string>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class InspectorPanel final : public EditorPanel
	{
	public:
		InspectorPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		void draw_entity_inspector(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_component_sections(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_pending_change_hint(AshEngine::UIContext& ui, const char* label);
		void draw_apply_revert_row(
			AshEngine::UIContext& ui,
			const char* apply_label,
			const char* revert_label,
			bool can_apply,
			bool has_pending_changes,
			bool& apply_clicked,
			bool& revert_clicked);
		void draw_identity_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_transform_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_camera_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_light_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		void draw_mesh_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity);
		bool has_pending_identity_changes() const;
		bool has_pending_transform_changes() const;
		bool has_pending_camera_changes() const;
		bool has_pending_light_changes() const;
		bool has_pending_mesh_changes() const;
		struct IdentityDraft
		{
			AshEngine::EntityId entity_id = 0;
			std::string original_name{};
			std::string current_name{};
		};

		struct TransformDraft
		{
			AshEngine::EntityId entity_id = 0;
			AshEngine::TransformComponent original_value{};
			AshEngine::TransformComponent current_value{};
		};

		struct CameraDraft
		{
			AshEngine::EntityId entity_id = 0;
			std::optional<AshEngine::CameraComponent> original_value{};
			std::optional<AshEngine::CameraComponent> current_value{};
		};

		struct LightDraft
		{
			AshEngine::EntityId entity_id = 0;
			std::optional<AshEngine::LightComponent> original_value{};
			std::optional<AshEngine::LightComponent> current_value{};
		};

		struct MeshDraft
		{
			AshEngine::EntityId entity_id = 0;
			std::optional<AshEngine::MeshComponent> original_value{};
			std::optional<AshEngine::MeshComponent> current_value{};
		};

		void reset_entity_drafts();
		void sync_entity_drafts(const AshEngine::Entity& entity);
		void sync_camera_draft(const AshEngine::Entity& entity);
		void sync_light_draft(const AshEngine::Entity& entity);
		void sync_mesh_draft(const AshEngine::Entity& entity);

	private:
		IdentityDraft m_identityDraft{};
		TransformDraft m_transformDraft{};
		CameraDraft m_cameraDraft{};
		LightDraft m_lightDraft{};
		MeshDraft m_meshDraft{};
	};
}
