#include "Services/EditorViewportService.h"
#include "Base/hlog.h"
#include "Function/Render/ScenePresentationSubsystem.h"
#include <algorithm>
#include <limits>

namespace AshEditor
{
	namespace
	{
		uint32_t clamp_viewport_extent(uint32_t value)
		{
			return std::max<uint32_t>(1u, std::min<uint32_t>(value, std::numeric_limits<uint16_t>::max()));
		}

		int viewport_sort_priority(const AshEditor::EditorViewportInstance& viewport)
		{
			if (viewport.id == "scene")
			{
				return 0;
			}
			if (viewport.id == "game")
			{
				return 1;
			}
			return 2;
		}

		template<typename ViewportPtr>
		void sort_viewports_stably(std::vector<ViewportPtr>& viewports)
		{
			std::sort(viewports.begin(), viewports.end(), [](const ViewportPtr lhs, const ViewportPtr rhs) {
				if (lhs == rhs)
				{
					return false;
				}
				if (!lhs)
				{
					return false;
				}
				if (!rhs)
				{
					return true;
				}

				const int lhs_priority = viewport_sort_priority(*lhs);
				const int rhs_priority = viewport_sort_priority(*rhs);
				if (lhs_priority != rhs_priority)
				{
					return lhs_priority < rhs_priority;
				}

				if (lhs->id != rhs->id)
				{
					return lhs->id < rhs->id;
				}

				return lhs->display_name < rhs->display_name;
			});
		}

		uint32_t get_synced_output_extent(uint32_t requested_extent, uint32_t current_extent)
		{
			if (requested_extent > 0u)
			{
				return clamp_viewport_extent(requested_extent);
			}

			return current_extent > 0u ? clamp_viewport_extent(current_extent) : 1u;
		}

		std::string make_output_debug_name(const EditorViewportInstance& viewport)
		{
			return viewport.display_name.empty()
				? "EditorViewportOutput"
				: viewport.display_name + " Output";
		}

		std::string make_binding_debug_name(const EditorViewportInstance& viewport)
		{
			return viewport.display_name.empty()
				? "EditorViewportBinding"
				: viewport.display_name + " Binding";
		}
	}

	EditorViewportPresentation EditorViewportService::build_default_presentation(const std::string& id)
	{
		EditorViewportPresentation presentation{};
		if (id == "scene")
		{
			presentation.kind = EditorViewportKind::Scene;
			presentation.preserve_aspect = false;
			presentation.accepts_input = true;
			presentation.show_stats = true;
			presentation.show_overlays = true;
			return presentation;
		}

		if (id == "game")
		{
			presentation.kind = EditorViewportKind::Game;
			presentation.preserve_aspect = true;
			presentation.accepts_input = false;
			presentation.show_stats = true;
			presentation.show_overlays = false;
			return presentation;
		}

		presentation.kind = EditorViewportKind::Auxiliary;
		presentation.preserve_aspect = false;
		presentation.accepts_input = false;
		presentation.show_stats = true;
		presentation.show_overlays = false;
		return presentation;
	}

	EditorViewportService::ViewportRecord* EditorViewportService::find_record(const std::string& id)
	{
		const auto it = m_viewports.find(id);
		return it != m_viewports.end() ? it->second.get() : nullptr;
	}

	const EditorViewportService::ViewportRecord* EditorViewportService::find_record(const std::string& id) const
	{
		const auto it = m_viewports.find(id);
		return it != m_viewports.end() ? it->second.get() : nullptr;
	}

	EditorViewportInstance& EditorViewportService::ensure_viewport(std::string id, std::string display_name)
	{
		if (id.empty())
		{
			id = "viewport";
		}

		auto it = m_viewports.find(id);
		if (it == m_viewports.end())
		{
			auto record = std::make_unique<ViewportRecord>();
			record->instance.id = id;
			record->instance.display_name = display_name.empty() ? id : std::move(display_name);
			record->presentation = build_default_presentation(id);
			it = m_viewports.emplace(id, std::move(record)).first;
		}
		else if (!display_name.empty())
		{
			it->second->instance.display_name = std::move(display_name);
		}

		if (m_primaryViewportId.empty())
		{
			m_primaryViewportId = id;
		}

		return it->second->instance;
	}

	EditorViewportInstance* EditorViewportService::find_viewport(const std::string& id)
	{
		ViewportRecord* record = find_record(id);
		return record ? &record->instance : nullptr;
	}

	const EditorViewportInstance* EditorViewportService::find_viewport(const std::string& id) const
	{
		const ViewportRecord* record = find_record(id);
		return record ? &record->instance : nullptr;
	}

	std::vector<EditorViewportInstance*> EditorViewportService::get_viewports()
	{
		std::vector<EditorViewportInstance*> viewports{};
		viewports.reserve(m_viewports.size());
		for (auto& entry : m_viewports)
		{
			if (entry.second)
			{
				viewports.push_back(&entry.second->instance);
			}
		}
		sort_viewports_stably(viewports);
		return viewports;
	}

	std::vector<const EditorViewportInstance*> EditorViewportService::get_viewports() const
	{
		std::vector<const EditorViewportInstance*> viewports{};
		viewports.reserve(m_viewports.size());
		for (const auto& entry : m_viewports)
		{
			if (entry.second)
			{
				viewports.push_back(&entry.second->instance);
			}
		}
		sort_viewports_stably(viewports);
		return viewports;
	}

	EditorViewportInstance* EditorViewportService::get_primary_viewport()
	{
		return find_viewport(m_primaryViewportId);
	}

	const EditorViewportInstance* EditorViewportService::get_primary_viewport() const
	{
		return find_viewport(m_primaryViewportId);
	}

	const std::string& EditorViewportService::get_primary_viewport_id() const
	{
		return m_primaryViewportId;
	}

	bool EditorViewportService::is_primary_viewport(const std::string& id) const
	{
		return !id.empty() && id == m_primaryViewportId;
	}

	void EditorViewportService::set_primary_viewport(const std::string& id)
	{
		if (!id.empty() && find_viewport(id))
		{
			m_primaryViewportId = id;
		}
	}

	EditorViewportPresentation* EditorViewportService::get_presentation(const std::string& id)
	{
		ViewportRecord* record = find_record(id);
		return record ? &record->presentation : nullptr;
	}

	const EditorViewportPresentation* EditorViewportService::get_presentation(const std::string& id) const
	{
		const ViewportRecord* record = find_record(id);
		return record ? &record->presentation : nullptr;
	}

	EditorViewportRenderState* EditorViewportService::get_render_state(const std::string& id)
	{
		ViewportRecord* record = find_record(id);
		return record ? &record->render_state : nullptr;
	}

	const EditorViewportRenderState* EditorViewportService::get_render_state(const std::string& id) const
	{
		const ViewportRecord* record = find_record(id);
		return record ? &record->render_state : nullptr;
	}

	bool EditorViewportService::update_requested_size(const std::string& id, uint32_t width, uint32_t height)
	{
		ViewportRecord* record = find_record(id);
		if (!record)
		{
			return false;
		}

		const uint32_t clamped_width = width > 0u ? clamp_viewport_extent(width) : 0u;
		const uint32_t clamped_height = height > 0u ? clamp_viewport_extent(height) : 0u;
		if (record->instance.state.requested_width == clamped_width &&
			record->instance.state.requested_height == clamped_height)
		{
			return false;
		}

		record->instance.state.requested_width = clamped_width;
		record->instance.state.requested_height = clamped_height;
		record->render_state.pending_sync = true;
		return true;
	}

	void EditorViewportService::set_panel_open(const std::string& id, bool open)
	{
		ViewportRecord* record = find_record(id);
		if (record)
		{
			if (record->presentation.panel_open != open)
			{
				record->render_state.pending_sync = true;
			}
			record->presentation.panel_open = open;
		}
	}

	bool EditorViewportService::sync_scene_presentations(
		AshEngine::ScenePresentationSubsystem& scene_presentation,
		AshEngine::Scene& scene)
	{
		bool all_synced = true;
		for (EditorViewportInstance* viewport : get_viewports())
		{
			if (!viewport)
			{
				continue;
			}

			ViewportRecord* record = find_record(viewport->id);
			if (!record)
			{
				continue;
			}

			const bool has_requested_size =
				record->instance.state.requested_width > 0u &&
				record->instance.state.requested_height > 0u;
			const uint32_t output_width = get_synced_output_extent(
				record->instance.state.requested_width,
				record->render_state.output_width);
			const uint32_t output_height = get_synced_output_extent(
				record->instance.state.requested_height,
				record->render_state.output_height);

			const std::string output_debug_name = make_output_debug_name(record->instance);
			bool output_synced = true;
			if (!record->instance.output.is_valid())
			{
				AshEngine::SceneOutputDesc output_desc{};
				output_desc.debug_name = output_debug_name.c_str();
				output_desc.kind = AshEngine::SceneOutputKind::Offscreen;
				output_desc.width = output_width;
				output_desc.height = output_height;
				record->instance.output = scene_presentation.create_output(output_desc);
				record->render_state.pending_sync = true;
				output_synced = record->instance.output.is_valid();
			}
			else if (
				record->render_state.output_width != output_width ||
				record->render_state.output_height != output_height)
			{
				AshEngine::SceneOutputDesc output_desc{};
				output_desc.debug_name = output_debug_name.c_str();
				output_desc.kind = AshEngine::SceneOutputKind::Offscreen;
				output_desc.width = output_width;
				output_desc.height = output_height;
				output_synced = scene_presentation.update_output(record->instance.output, output_desc);
			}

			if (!output_synced)
			{
				HLogError("Editor viewport '{}' failed to sync output presentation state.", record->instance.id);
				record->render_state.pending_sync = true;
				record->instance.surface = {};
				record->instance.state.width = 0u;
				record->instance.state.height = 0u;
				all_synced = false;
				continue;
			}

			record->instance.surface = scene_presentation.get_ui_surface(record->instance.output);

			const std::string binding_debug_name = make_binding_debug_name(record->instance);
			AshEngine::SceneViewBindingDesc binding_desc{};
			binding_desc.debug_name = binding_debug_name.c_str();
			binding_desc.scene = &scene;
			binding_desc.camera.source = AshEngine::SceneCameraSource::PrimaryCamera;
			binding_desc.output = record->instance.output;
			binding_desc.enabled = record->presentation.panel_open && has_requested_size;
			binding_desc.sort_order = viewport_sort_priority(record->instance);

			bool binding_synced = true;
			if (!record->instance.binding.is_valid())
			{
				record->instance.binding = scene_presentation.create_view_binding(binding_desc);
				binding_synced = record->instance.binding.is_valid();
			}
			else if (record->render_state.pending_sync)
			{
				binding_synced = scene_presentation.update_view_binding(record->instance.binding, binding_desc);
			}

			if (!binding_synced)
			{
				HLogError("Editor viewport '{}' failed to sync scene binding state.", record->instance.id);
				record->render_state.pending_sync = true;
				record->instance.state.width = 0u;
				record->instance.state.height = 0u;
				all_synced = false;
				continue;
			}

			record->render_state.output_width = output_width;
			record->render_state.output_height = output_height;
			record->render_state.pending_sync = false;
			record->instance.state.width = has_requested_size ? output_width : 0u;
			record->instance.state.height = has_requested_size ? output_height : 0u;
		}

		return all_synced;
	}

	void EditorViewportService::destroy_scene_presentations(AshEngine::ScenePresentationSubsystem* scene_presentation)
	{
		for (auto& entry : m_viewports)
		{
			if (!entry.second)
			{
				continue;
			}

			EditorViewportInstance& instance = entry.second->instance;
			if (scene_presentation)
			{
				if (instance.binding.is_valid())
				{
					scene_presentation->destroy_view_binding(instance.binding);
				}
				if (instance.output.is_valid())
				{
					scene_presentation->destroy_output(instance.output);
				}
			}

			instance.binding = {};
			instance.output = {};
			instance.surface = {};
			instance.state.width = 0u;
			instance.state.height = 0u;
			entry.second->render_state.output_width = 0u;
			entry.second->render_state.output_height = 0u;
			entry.second->render_state.pending_sync = true;
		}
	}

	void EditorViewportService::reset_presentations()
	{
		for (auto& entry : m_viewports)
		{
			if (!entry.second)
			{
				continue;
			}

			entry.second->presentation = build_default_presentation(entry.second->instance.id);
			entry.second->render_state.pending_sync = true;
		}

		if (find_viewport("scene"))
		{
			m_primaryViewportId = "scene";
		}
	}

	std::vector<EditorViewportPersistenceState> EditorViewportService::capture_persistence_state() const
	{
		std::vector<EditorViewportPersistenceState> states{};
		const std::vector<const EditorViewportInstance*> viewports = get_viewports();
		states.reserve(viewports.size());
		for (const EditorViewportInstance* viewport : viewports)
		{
			if (!viewport)
			{
				continue;
			}

			const ViewportRecord* record = find_record(viewport->id);
			if (!record)
			{
				continue;
			}
			states.push_back({
				record->instance.id,
				record->presentation.panel_open,
				record->presentation.show_toolbar,
				record->presentation.preserve_aspect,
				record->presentation.accepts_input,
				record->presentation.show_stats,
				record->presentation.show_overlays
			});
		}
		return states;
	}

	void EditorViewportService::apply_persistence_state(
		const std::vector<EditorViewportPersistenceState>& states,
		const std::string& primary_viewport_id)
	{
		for (const EditorViewportPersistenceState& state : states)
		{
			ViewportRecord* record = find_record(state.id);
			if (!record)
			{
				continue;
			}

			record->presentation.panel_open = state.panel_open;
			record->presentation.show_toolbar = state.show_toolbar;
			record->presentation.preserve_aspect = state.preserve_aspect;
			record->presentation.accepts_input = state.accepts_input;
			record->presentation.show_stats = state.show_stats;
			record->presentation.show_overlays = state.show_overlays;
			record->render_state.pending_sync = true;
		}

		set_primary_viewport(primary_viewport_id);
	}

	void EditorViewportService::clear()
	{
		m_viewports.clear();
		m_primaryViewportId.clear();
	}
}
