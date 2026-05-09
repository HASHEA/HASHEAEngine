#pragma once

#include "Core/EditorSelection.h"

#include <cstdint>

namespace AshEditor
{
	class EditorEventBus;

	class SelectionService
	{
	public:
		// Returns the current selection snapshot. Valid even when HasSelection() is false.
		const EditorSelection& GetSelection() const;
		bool HasSelection() const;

		// Optional event bus used to publish selection change events.
		void SetEventBus(EditorEventBus* pEventBus);

		// Sets the current selection and publishes a change event when it differs from the previous value.
		void Select(EditorSelection selection);

		// Clears the current selection and publishes a change event if needed.
		void Clear();

	private:
		void PublishSelectionChanged(const EditorSelection& refPreviousSelection) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		EditorSelection _selection{};
		uint64_t _uRevision = 0;
	};
}
