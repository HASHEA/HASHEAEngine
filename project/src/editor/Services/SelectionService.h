#pragma once
#include "Core/EditorSelection.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace AshEditor
{
	using SelectionListenerId = uint64_t;
	using SelectionChangedCallback = std::function<void(const EditorSelection& previous, const EditorSelection& current)>;

	class SelectionService
	{
	public:
		const EditorSelection& get_selection() const;
		bool has_selection() const;
		uint64_t get_revision() const;

		void select(EditorSelection selection);
		void clear();
		SelectionListenerId subscribe(SelectionChangedCallback callback);
		bool unsubscribe(SelectionListenerId listener_id);

	private:
		struct SelectionListener
		{
			SelectionListenerId id = 0;
			SelectionChangedCallback callback{};
		};

		void notify_selection_changed(const EditorSelection& previous);

	private:
		EditorSelection m_selection{};
		std::vector<SelectionListener> m_listeners{};
		SelectionListenerId m_nextListenerId = 1;
		uint64_t m_revision = 0;
	};
}
