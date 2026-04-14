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

	void SelectionService::select(EditorSelection selection)
	{
		m_selection = std::move(selection);
	}

	void SelectionService::clear()
	{
		m_selection.clear();
	}
}
