#pragma once
#include "Core/EditorPanel.h"

namespace AshEditor
{
	class InspectorPanel final : public EditorPanel
	{
	public:
		InspectorPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;
	};
}
