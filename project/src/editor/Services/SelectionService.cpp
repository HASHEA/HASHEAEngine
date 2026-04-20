#include "Services/SelectionService.h"
#include <utility>

namespace AshEditor
{
	const EditorSelection& SelectionService::get_selection() const
	{
		return m_selection;
	}

	bool SelectionService::has_selection() const
	{
		return !m_selection.is_empty();
	}

	uint64_t SelectionService::get_revision() const
	{
		return m_revision;
	}

	void SelectionService::select(EditorSelection selection)
	{
		const EditorSelection previous = m_selection;
		m_selection = std::move(selection);
		if (m_selection != previous)
		{
			++m_revision;
			notify_selection_changed(previous);
		}
	}

	void SelectionService::clear()
	{
		if (m_selection.is_empty())
		{
			return;
		}

		const EditorSelection previous = m_selection;
		m_selection.clear();
		++m_revision;
		notify_selection_changed(previous);
	}

	SelectionListenerId SelectionService::subscribe(SelectionChangedCallback callback)
	{
		if (!callback)
		{
			return 0;
		}

		const SelectionListenerId listener_id = m_nextListenerId++;
		m_listeners.push_back({ listener_id, std::move(callback) });
		return listener_id;
	}

	bool SelectionService::unsubscribe(SelectionListenerId listener_id)
	{
		for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
		{
			if (it->id == listener_id)
			{
				m_listeners.erase(it);
				return true;
			}
		}
		return false;
	}

	void SelectionService::notify_selection_changed(const EditorSelection& previous)
	{
		std::vector<SelectionListenerId> listener_ids{};
		listener_ids.reserve(m_listeners.size());
		for (const SelectionListener& listener : m_listeners)
		{
			listener_ids.push_back(listener.id);
		}

		for (SelectionListenerId listener_id : listener_ids)
		{
			for (const SelectionListener& listener : m_listeners)
			{
				if (listener.id == listener_id)
				{
					if (listener.callback)
					{
						listener.callback(previous, m_selection);
					}
					break;
				}
			}
		}
	}
}
