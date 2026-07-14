#pragma once
#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/EditorIds.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Panels/ViewportPanelState.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace AshEditor
{
	struct ViewportOutputExtent
	{
		uint32_t uWidth = 0;
		uint32_t uHeight = 0;
	};

	inline constexpr auto ResolveViewportOutputExtent(
		std::string_view svViewportId,
		uint32_t uPanelWidth,
		uint32_t uPanelHeight,
		uint32_t uFixedWidth,
		uint32_t uFixedHeight) -> ViewportOutputExtent
	{
		return svViewportId == EditorViewportIds::Game && uFixedWidth > 0u && uFixedHeight > 0u
			? ViewportOutputExtent{ uFixedWidth, uFixedHeight }
			: ViewportOutputExtent{ uPanelWidth, uPanelHeight };
	}

	void ConfigurePerfGateViewportOutputExtent(uint32_t uWidth, uint32_t uHeight);
	auto GetPerfGateViewportOutputExtent() -> ViewportOutputExtent;

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

	private:
		ViewportPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::string _strViewportId{};
		ViewportPanelTerrainInteractionState _terrainInteraction{};
		ViewportPanelSceneSelectionState _sceneSelection{};
	};
}
