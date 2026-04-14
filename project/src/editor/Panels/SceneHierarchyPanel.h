#pragma once
#include "Core/EditorPanel.h"

namespace AshEditor
{
	class SceneHierarchyPanel final : public EditorPanel
	{
	public:
		SceneHierarchyPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;
	};
}
