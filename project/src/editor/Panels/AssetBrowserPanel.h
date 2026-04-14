#pragma once
#include "Core/EditorPanel.h"

namespace AshEditor
{
	class AssetBrowserPanel final : public EditorPanel
	{
	public:
		AssetBrowserPanel();

	public:
		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;
	};
}
