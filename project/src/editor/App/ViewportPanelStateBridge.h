#pragma once

namespace AshEditor
{
	class EditorEventBindings;
	class EditorEventBus;
	class EditorViewportService;
	class PanelManager;

	class ViewportPanelStateBridge final
	{
	public:
		static void Bind(
			EditorEventBindings& refBindings,
			EditorEventBus& refEventBus,
			EditorViewportService& refViewportService);
		static void ApplyViewportOpenStateToPanels(
			const EditorViewportService& refViewportService,
			PanelManager& refPanelManager);
	};
}
