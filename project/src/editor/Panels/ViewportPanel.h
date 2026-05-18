#pragma once
#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/EditorIds.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Panels/ViewportPanelInteraction.h"

#include <string>

namespace AshEditor
{
	class EditorEventBus;
	struct EditorViewportInstance;

	class ViewportPanel final : public EditorPanel
	{
	public:
		ViewportPanel(
			std::string strViewportId = EditorViewportIds::Scene,
			std::string strPanelId = EditorPanelIds::Viewport,
			std::string strTitle = "Viewport",
			ViewportPanelDeps deps = {});

	public:
		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		void BindEventBus(EditorEventBus* pEventBus);

	private:
		void ClearDeps();
		void UnsubscribeEvents();
		void ResetRuntimeViewportState();
		EditorViewportInstance* ResolveViewport();
		const EditorViewportInstance* ResolveViewport() const;

	private:
		ViewportPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::string _strViewportId{};
		ViewportPanelSceneBoxSelectionState _sceneBoxSelection{};
	};
}
