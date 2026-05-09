#include "Services/SelectionService.h"

#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include <utility>

namespace AshEditor
{
	const EditorSelection& SelectionService::GetSelection() const
	{
		return _selection;
	}

	bool SelectionService::HasSelection() const
	{
		return !_selection.IsEmpty();
	}

	void SelectionService::SetEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	void SelectionService::Select(EditorSelection selection)
	{
		// Preserve a monotonic revision so UI/event consumers can detect ordering and ignore stale updates.
		const EditorSelection previousSelection = _selection;
		_selection = std::move(selection);
		if (_selection != previousSelection)
		{
			++_uRevision;
			PublishSelectionChanged(previousSelection);
		}
	}

	void SelectionService::Clear()
	{
		if (_selection.IsEmpty())
		{
			return;
		}

		// Clear always increments revision even if only the selection payload changes (e.g. label updates).
		const EditorSelection previousSelection = _selection;
		_selection.Clear();
		++_uRevision;
		PublishSelectionChanged(previousSelection);
	}

	void SelectionService::PublishSelectionChanged(const EditorSelection& refPreviousSelection) const
	{
		if (_pEventBus)
		{
			EditorSelectionChangedEvent event{};
			event.previousSelection = refPreviousSelection;
			event.currentSelection = _selection;
			event.uRevision = _uRevision;
			_pEventBus->Publish(event);
		}
	}
}
