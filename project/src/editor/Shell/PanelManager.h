#pragma once

#include "Core/EditorPanel.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	class PanelManager final
	{
	public:
		template<typename TPanel, typename... TArgs>
		TPanel* CreatePanel(TArgs&&... args)
		{
			std::unique_ptr<TPanel> upPanel = std::make_unique<TPanel>(std::forward<TArgs>(args)...);
			TPanel* pPanel = upPanel.get();
			// Ownership stays with PanelManager. Returned pointer is an observer handle valid until Shutdown/ClearRuntimeState.
			pPanel->OnAttach();
			_mapLastKnownOpenStates[pPanel->GetId()] = pPanel->IsOpen();
			_vecPanels.push_back(std::move(upPanel));
			return pPanel;
		}

	public:
		// Optional event bus used to publish panel open/close state changes.
		void BindEventBus(EditorEventBus* pEventBus);

		// Detaches panels in reverse creation order and clears storage (invalidates any cached panel pointers).
		void Shutdown();

		// Updates open panels only (closed panels keep their state but do not receive updates).
		void Update();

		// Draws GUI for open panels only and then syncs any open state transitions to the event bus.
		void DrawGui(const EditorFrameContext& refFrameContext);

		// Clears runtime panel storage without calling OnDetach (used during hard reset paths).
		void ClearRuntimeState();
		EditorPanel* FindPanel(const std::string& strId);
		const EditorPanel* FindPanel(const std::string& strId) const;

		// Sets a panel open/close flag and publishes a corresponding open-state event.
		// Returns true only if an existing panel changed state.
		bool SetPanelOpen(const std::string& strId, bool bOpen);

		const std::vector<std::unique_ptr<EditorPanel>>& GetPanels() const;

	private:
		void SyncPanelOpenStateEvents();
		void PublishPanelOpenState(const EditorPanel& panel) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		std::vector<std::unique_ptr<EditorPanel>> _vecPanels{};
		std::unordered_map<std::string, bool> _mapLastKnownOpenStates{};
	};
}
