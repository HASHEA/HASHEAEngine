#include "App/ViewportPanelStateBridge.h"

#include "Core/EditorEventBindings.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Services/EditorViewportService.h"
#include "Shell/PanelManager.h"

#include <string_view>

namespace AshEditor
{
	namespace
	{
		struct ViewportPanelLink
		{
			const char* pViewportId = nullptr;
			const char* pPanelId = nullptr;
		};

		constexpr ViewportPanelLink kViewportPanelLinks[]{
			{ EditorViewportIds::Scene, EditorPanelIds::SceneViewport },
			{ EditorViewportIds::Game, EditorPanelIds::GameViewport }
		};

		const char* FindViewportIdForPanel(std::string_view svPanelId)
		{
			for (const ViewportPanelLink& refLink : kViewportPanelLinks)
			{
				if (svPanelId == refLink.pPanelId)
				{
					return refLink.pViewportId;
				}
			}

			return nullptr;
		}
	}

	void ViewportPanelStateBridge::Bind(
		EditorEventBindings& refBindings,
		EditorEventBus& refEventBus,
		EditorViewportService& refViewportService)
	{
		if (refBindings.IsBoundTo(&refEventBus))
		{
			return;
		}

		refBindings.Bind(&refEventBus);
		refBindings.Subscribe<EditorPanelOpenStateChangedEvent>(
			[&refViewportService](const EditorPanelOpenStateChangedEvent& refEvent)
			{
				const char* pViewportId = FindViewportIdForPanel(refEvent.strPanelId);
				if (!pViewportId)
				{
					return;
				}

				refViewportService.SetPanelOpen(pViewportId, refEvent.bOpen);
			});
	}

	void ViewportPanelStateBridge::ApplyViewportOpenStateToPanels(
		const EditorViewportService& refViewportService,
		PanelManager& refPanelManager)
	{
		for (const ViewportPanelLink& refLink : kViewportPanelLinks)
		{
			if (const EditorViewportPresentation* pPresentation = refViewportService.GetPresentation(refLink.pViewportId))
			{
				refPanelManager.SetPanelOpen(refLink.pPanelId, pPresentation->bPanelOpen);
			}
		}
	}
}
