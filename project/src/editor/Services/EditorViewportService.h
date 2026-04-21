#pragma once
#include "Core/EditorContext.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	class Scene;
	class ScenePresentationSubsystem;
}

namespace AshEditor
{
	enum class EditorViewportKind : uint8_t
	{
		Scene = 0,
		Game,
		Auxiliary
	};

	struct EditorViewportPresentation
	{
		EditorViewportKind kind = EditorViewportKind::Auxiliary;
		bool show_toolbar = true;
		bool preserve_aspect = false;
		bool accepts_input = false;
		bool show_stats = true;
		bool show_overlays = false;
		bool panel_open = true;
	};

	struct EditorViewportRenderState
	{
		uint32_t output_width = 0;
		uint32_t output_height = 0;
		bool pending_sync = true;
	};

	struct EditorViewportPersistenceState
	{
		std::string id{};
		bool panel_open = true;
		bool show_toolbar = true;
		bool preserve_aspect = false;
		bool accepts_input = false;
		bool show_stats = true;
		bool show_overlays = false;
	};

	class EditorViewportService
	{
	private:
		struct ViewportRecord
		{
			EditorViewportInstance instance{};
			EditorViewportPresentation presentation{};
			EditorViewportRenderState render_state{};
		};

		using ViewportStorage = std::unordered_map<std::string, std::unique_ptr<ViewportRecord>>;

	public:
		EditorViewportInstance& ensure_viewport(std::string id, std::string display_name = {});
		EditorViewportInstance* find_viewport(const std::string& id);
		const EditorViewportInstance* find_viewport(const std::string& id) const;
		std::vector<EditorViewportInstance*> get_viewports();
		std::vector<const EditorViewportInstance*> get_viewports() const;

		EditorViewportInstance* get_primary_viewport();
		const EditorViewportInstance* get_primary_viewport() const;
		const std::string& get_primary_viewport_id() const;
		bool is_primary_viewport(const std::string& id) const;
		void set_primary_viewport(const std::string& id);
		EditorViewportPresentation* get_presentation(const std::string& id);
		const EditorViewportPresentation* get_presentation(const std::string& id) const;
		EditorViewportRenderState* get_render_state(const std::string& id);
		const EditorViewportRenderState* get_render_state(const std::string& id) const;
		bool update_requested_size(const std::string& id, uint32_t width, uint32_t height);
		void set_panel_open(const std::string& id, bool open);
		bool sync_scene_presentations(
			AshEngine::ScenePresentationSubsystem& scene_presentation,
			AshEngine::Scene& scene);
		void destroy_scene_presentations(AshEngine::ScenePresentationSubsystem* scene_presentation);
		void reset_presentations();
		std::vector<EditorViewportPersistenceState> capture_persistence_state() const;
		void apply_persistence_state(
			const std::vector<EditorViewportPersistenceState>& states,
			const std::string& primary_viewport_id);
		void clear();

	private:
		ViewportRecord* find_record(const std::string& id);
		const ViewportRecord* find_record(const std::string& id) const;
		static EditorViewportPresentation build_default_presentation(const std::string& id);
		ViewportStorage m_viewports{};
		std::string m_primaryViewportId{};
	};
}
