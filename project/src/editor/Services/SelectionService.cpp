#include "Services/SelectionService.h"

#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace AshEditor
{
	namespace
	{
		bool SameSelectionIdentity(const EditorSelection& refLeft, const EditorSelection& refRight)
		{
			return refLeft.eKind == refRight.eKind && refLeft.uId == refRight.uId;
		}

		void AppendSelectionIfMissing(
			std::vector<EditorSelection>& vecSelections,
			const EditorSelection& refSelection)
		{
			if (refSelection.IsEmpty())
			{
				return;
			}

			for (const EditorSelection& refExistingSelection : vecSelections)
			{
				if (SameSelectionIdentity(refExistingSelection, refSelection))
				{
					return;
				}
			}
			vecSelections.push_back(refSelection);
		}

		void RemoveSelectionByIdentity(
			std::vector<EditorSelection>& vecSelections,
			EditorSelectionKind eKind,
			uint64_t uId)
		{
			vecSelections.erase(
				std::remove_if(
					vecSelections.begin(),
					vecSelections.end(),
					[eKind, uId](const EditorSelection& refSelection)
					{
						return refSelection.eKind == eKind && refSelection.uId == uId;
					}),
				vecSelections.end());
		}

		bool ContainsSelectionIdentity(
			const std::vector<EditorSelection>& vecSelections,
			const EditorSelection& refSelection)
		{
			for (const EditorSelection& refExistingSelection : vecSelections)
			{
				if (SameSelectionIdentity(refExistingSelection, refSelection))
				{
					return true;
				}
			}
			return false;
		}

		std::vector<EditorSelection> BuildUniqueSelections(
			const std::vector<EditorSelection>& vecSelections)
		{
			std::vector<EditorSelection> vecResult{};
			vecResult.reserve(vecSelections.size());
			for (const EditorSelection& refSelection : vecSelections)
			{
				AppendSelectionIfMissing(vecResult, refSelection);
			}
			return vecResult;
		}

		bool SameSelectionList(
			const std::vector<EditorSelection>& refLeft,
			const std::vector<EditorSelection>& refRight)
		{
			if (refLeft.size() != refRight.size())
			{
				return false;
			}

			for (size_t uIndex = 0; uIndex < refLeft.size(); ++uIndex)
			{
				if (refLeft[uIndex] != refRight[uIndex])
				{
					return false;
				}
			}
			return true;
		}
	}

	const EditorSelection& SelectionService::GetSelection() const
	{
		return _selection;
	}

	const std::vector<EditorSelection>& SelectionService::GetSelections() const
	{
		return _vecSelections;
	}

	std::vector<uint64_t> SelectionService::GetSelectedIds(EditorSelectionKind eKind) const
	{
		std::vector<uint64_t> vecIds{};
		vecIds.reserve(_vecSelections.size());
		for (const EditorSelection& refSelection : _vecSelections)
		{
			if (refSelection.eKind == eKind && refSelection.uId != 0)
			{
				vecIds.push_back(refSelection.uId);
			}
		}
		return vecIds;
	}

	bool SelectionService::HasSelection() const
	{
		return !_selection.IsEmpty();
	}

	bool SelectionService::HasMultipleSelections() const
	{
		return _vecSelections.size() > 1;
	}

	bool SelectionService::IsSelected(EditorSelectionKind eKind, uint64_t uId) const
	{
		if (uId == 0)
		{
			return false;
		}

		for (const EditorSelection& refSelection : _vecSelections)
		{
			if (refSelection.eKind == eKind && refSelection.uId == uId)
			{
				return true;
			}
		}
		return false;
	}

	void SelectionService::SetEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	void SelectionService::Select(EditorSelection selection)
	{
		SelectSingle(std::move(selection));
	}

	void SelectionService::SelectSingle(EditorSelection selection)
	{
		// Preserve a monotonic revision so UI/event consumers can detect ordering and ignore stale updates.
		const EditorSelection previousSelection = _selection;
		const std::vector<EditorSelection> vecPreviousSelections = _vecSelections;
		_vecSelections.clear();
		if (!selection.IsEmpty())
		{
			_vecSelections.push_back(std::move(selection));
		}
		RefreshPrimarySelection();
		if (_selection != previousSelection || !SameSelectionList(_vecSelections, vecPreviousSelections))
		{
			++_uRevision;
			PublishSelectionChanged(previousSelection, vecPreviousSelections);
		}
	}

	void SelectionService::Add(EditorSelection selection)
	{
		if (selection.IsEmpty())
		{
			return;
		}

		const EditorSelection previousSelection = _selection;
		const std::vector<EditorSelection> vecPreviousSelections = _vecSelections;
		for (EditorSelection& refSelection : _vecSelections)
		{
			if (SameSelectionIdentity(refSelection, selection))
			{
				refSelection = std::move(selection);
				RefreshPrimarySelection();
				if (_selection != previousSelection || !SameSelectionList(_vecSelections, vecPreviousSelections))
				{
					++_uRevision;
					PublishSelectionChanged(previousSelection, vecPreviousSelections);
				}
				return;
			}
		}

		_vecSelections.push_back(std::move(selection));
		RefreshPrimarySelection();
		++_uRevision;
		PublishSelectionChanged(previousSelection, vecPreviousSelections);
	}

	void SelectionService::Remove(EditorSelectionKind eKind, uint64_t uId)
	{
		if (uId == 0)
		{
			return;
		}

		const EditorSelection previousSelection = _selection;
		const std::vector<EditorSelection> vecPreviousSelections = _vecSelections;
		RemoveSelectionByIdentity(_vecSelections, eKind, uId);
		RefreshPrimarySelection();
		if (_selection != previousSelection || !SameSelectionList(_vecSelections, vecPreviousSelections))
		{
			++_uRevision;
			PublishSelectionChanged(previousSelection, vecPreviousSelections);
		}
	}

	void SelectionService::Toggle(EditorSelection selection)
	{
		if (selection.IsEmpty())
		{
			return;
		}
		if (IsSelected(selection.eKind, selection.uId))
		{
			Remove(selection.eKind, selection.uId);
			return;
		}
		Add(std::move(selection));
	}

	void SelectionService::SelectRange(const std::vector<EditorSelection>& vecSelections)
	{
		ReplaceSelections(vecSelections);
	}

	std::vector<EditorSelection> SelectionService::BuildMergedSelections(
		const std::vector<EditorSelection>& vecIncomingSelections,
		const SelectionMergeMode eMergeMode) const
	{
		if (eMergeMode == SelectionMergeMode::Replace)
		{
			return BuildUniqueSelections(vecIncomingSelections);
		}

		std::vector<EditorSelection> vecResult = _vecSelections;
		for (const EditorSelection& refSelection : vecIncomingSelections)
		{
			switch (eMergeMode)
			{
			case SelectionMergeMode::Toggle:
				if (ContainsSelectionIdentity(vecResult, refSelection))
				{
					RemoveSelectionByIdentity(vecResult, refSelection.eKind, refSelection.uId);
				}
				else
				{
					AppendSelectionIfMissing(vecResult, refSelection);
				}
				break;
			case SelectionMergeMode::Add:
				AppendSelectionIfMissing(vecResult, refSelection);
				break;
			case SelectionMergeMode::Remove:
				RemoveSelectionByIdentity(vecResult, refSelection.eKind, refSelection.uId);
				break;
			case SelectionMergeMode::Replace:
			default:
				break;
			}
		}
		return vecResult;
	}

	void SelectionService::Clear()
	{
		if (_selection.IsEmpty() && _vecSelections.empty())
		{
			return;
		}

		// Clear always increments revision even if only the selection payload changes (e.g. label updates).
		const EditorSelection previousSelection = _selection;
		const std::vector<EditorSelection> vecPreviousSelections = _vecSelections;
		_selection.Clear();
		_vecSelections.clear();
		++_uRevision;
		PublishSelectionChanged(previousSelection, vecPreviousSelections);
	}

	void SelectionService::RefreshPrimarySelection()
	{
		if (_vecSelections.empty())
		{
			_selection.Clear();
			return;
		}
		_selection = _vecSelections.back();
	}

	bool SelectionService::ReplaceSelections(const std::vector<EditorSelection>& vecSelections)
	{
		const EditorSelection previousSelection = _selection;
		const std::vector<EditorSelection> vecPreviousSelections = _vecSelections;
		_vecSelections = BuildUniqueSelections(vecSelections);
		RefreshPrimarySelection();
		if (_selection == previousSelection && SameSelectionList(_vecSelections, vecPreviousSelections))
		{
			return false;
		}

		++_uRevision;
		PublishSelectionChanged(previousSelection, vecPreviousSelections);
		return true;
	}

	void SelectionService::PublishSelectionChanged(
		const EditorSelection& refPreviousSelection,
		const std::vector<EditorSelection>& refPreviousSelections) const
	{
		if (_pEventBus)
		{
			EditorSelectionChangedEvent event{};
			event.previousSelection = refPreviousSelection;
			event.currentSelection = _selection;
			event.vecPreviousSelections = refPreviousSelections;
			event.vecCurrentSelections = _vecSelections;
			event.uRevision = _uRevision;
			_pEventBus->Publish(event);
		}
	}
}
