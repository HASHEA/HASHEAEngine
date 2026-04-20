#include "Services/EditorViewportService.h"
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
		if (clamped_width > 0u && clamped_height > 0u)
		{
			record->render_state.pending_rebuild =
				record->render_state.allocated_width != clamped_width ||
				record->render_state.allocated_height != clamped_height;
		}
		return true;
	}

	EditorViewportRenderRequest EditorViewportService::get_render_request(
		const std::string& id,
		const std::shared_ptr<AshEngine::RenderTarget>& back_buffer) const
	{
		EditorViewportRenderRequest request{};
		const ViewportRecord* record = find_record(id);
		if (!record)
		{
			return request;
		}

		uint32_t desired_width = record->instance.state.requested_width;
		uint32_t desired_height = record->instance.state.requested_height;
		if ((desired_width == 0u || desired_height == 0u) && back_buffer)
		{
			desired_width = back_buffer->get_width();
			desired_height = back_buffer->get_height();
		}

		request.width = clamp_viewport_extent(desired_width == 0u ? 1u : desired_width);
		request.height = clamp_viewport_extent(desired_height == 0u ? 1u : desired_height);
		request.format = back_buffer ? back_buffer->get_format() : AshEngine::RenderTextureFormat::BGRA8_SRGB;
		request.rebuild_required =
			record->render_state.pending_rebuild ||
			record->render_state.allocated_width != request.width ||
			record->render_state.allocated_height != request.height ||
			record->render_state.allocated_format != request.format ||
			!record->instance.render_target;
		return request;
	}

	void EditorViewportService::notify_render_target_updated(
		const std::string& id,
		const std::shared_ptr<AshEngine::RenderTarget>& render_target)
	{
		ViewportRecord* record = find_record(id);
		if (!record)
		{
			return;
		}

		record->instance.render_target = render_target;
		if (!render_target)
		{
			record->render_state.allocated_width = 0u;
			record->render_state.allocated_height = 0u;
			record->render_state.allocated_format = AshEngine::RenderTextureFormat::Unknown;
			record->render_state.pending_rebuild = true;
			record->instance.state.width = 0u;
			record->instance.state.height = 0u;
			return;
		}

		record->render_state.allocated_width = render_target->get_width();
		record->render_state.allocated_height = render_target->get_height();
		record->render_state.allocated_format = render_target->get_format();
		record->render_state.pending_rebuild = false;
		record->instance.state.width = render_target->get_width();
		record->instance.state.height = render_target->get_height();
	}

	void EditorViewportService::set_panel_open(const std::string& id, bool open)
	{
		ViewportRecord* record = find_record(id);
		if (record)
		{
			record->presentation.panel_open = open;
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
			entry.second->render_state.pending_rebuild = true;
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
			record->render_state.pending_rebuild = true;
		}

		set_primary_viewport(primary_viewport_id);
	}

	void EditorViewportService::clear()
	{
		m_viewports.clear();
		m_primaryViewportId.clear();
	}
}
