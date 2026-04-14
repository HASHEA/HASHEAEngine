#pragma once
#include "Core/EditorSelection.h"

namespace AshEditor
{
	class SelectionService
	{
	public:
		const EditorSelection& get_selection() const;
		bool has_selection() const;

		void select(EditorSelection selection);
		void clear();

	private:
		EditorSelection m_selection{};
	};
}
