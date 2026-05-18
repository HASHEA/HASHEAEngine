#pragma once

#include "Core/EditorSelection.h"

#include <cstdint>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	class SelectionService
	{
	public:
		enum class SelectionMergeMode : uint8_t
		{
			Replace = 0,
			Toggle,
			Add,
			Remove
		};

	public:
		// Returns the current selection snapshot. Valid even when HasSelection() is false.
		const EditorSelection& GetSelection() const;
		const std::vector<EditorSelection>& GetSelections() const;
		std::vector<uint64_t> GetSelectedIds(EditorSelectionKind eKind) const;
		bool HasSelection() const;
		bool HasMultipleSelections() const;
		bool IsSelected(EditorSelectionKind eKind, uint64_t uId) const;

		// Optional event bus used to publish selection change events.
		void SetEventBus(EditorEventBus* pEventBus);

		// Sets the current selection and publishes a change event when it differs from the previous value.
		void Select(EditorSelection selection);
		void SelectSingle(EditorSelection selection);
		void Add(EditorSelection selection);
		void Remove(EditorSelectionKind eKind, uint64_t uId);
		void Toggle(EditorSelection selection);
		void SelectRange(const std::vector<EditorSelection>& vecSelections);
		std::vector<EditorSelection> BuildMergedSelections(
			const std::vector<EditorSelection>& vecIncomingSelections,
			SelectionMergeMode eMergeMode) const;

		// Clears the current selection and publishes a change event if needed.
		void Clear();

	private:
		void RefreshPrimarySelection();
		bool ReplaceSelections(const std::vector<EditorSelection>& vecSelections);
		void PublishSelectionChanged(
			const EditorSelection& refPreviousSelection,
			const std::vector<EditorSelection>& refPreviousSelections) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		EditorSelection _selection{};
		std::vector<EditorSelection> _vecSelections{};
		uint64_t _uRevision = 0;
	};
}
