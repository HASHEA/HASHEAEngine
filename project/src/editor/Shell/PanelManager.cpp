#include "Shell/PanelManager.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"

namespace AshEditor
{
	void PanelManager::BindEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	void PanelManager::Shutdown()
	{
		for (
			std::vector<std::unique_ptr<EditorPanel>>::reverse_iterator itPanel = _vecPanels.rbegin();
			itPanel != _vecPanels.rend();
			++itPanel)
		{
			if (*itPanel)
			{
				(*itPanel)->OnDetach();
			}
		}

		_vecPanels.clear();
		_mapLastKnownOpenStates.clear();
	}

	void PanelManager::Update()
	{
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : _vecPanels)
		{
			EditorPanel* pPanel = refPanelOwner.get();
			if (pPanel && pPanel->IsOpen())
			{
				pPanel->OnUpdate();
			}
		}
	}

	void PanelManager::DrawGui(const EditorFrameContext& refFrameContext)
	{
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : _vecPanels)
		{
			EditorPanel* pPanel = refPanelOwner.get();
			if (pPanel && pPanel->IsOpen())
			{
				pPanel->OnGui(refFrameContext);
			}
		}

		SyncPanelOpenStateEvents();
	}

	void PanelManager::ClearRuntimeState()
	{
		_vecPanels.clear();
		_mapLastKnownOpenStates.clear();
	}

	EditorPanel* PanelManager::FindPanel(const std::string& strId)
	{
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : _vecPanels)
		{
			EditorPanel* pPanel = refPanelOwner.get();
			if (pPanel && pPanel->GetId() == strId)
			{
				return pPanel;
			}
		}

		return nullptr;
	}

	const EditorPanel* PanelManager::FindPanel(const std::string& strId) const
	{
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : _vecPanels)
		{
			const EditorPanel* pPanel = refPanelOwner.get();
			if (pPanel && pPanel->GetId() == strId)
			{
				return pPanel;
			}
		}

		return nullptr;
	}

	bool PanelManager::SetPanelOpen(const std::string& strId, bool bOpen)
	{
		EditorPanel* pPanel = FindPanel(strId);
		if (!pPanel)
		{
			return false;
		}

		if (pPanel->IsOpen() == bOpen)
		{
			return false;
		}

		pPanel->SetOpen(bOpen);
		_mapLastKnownOpenStates[strId] = bOpen;
		PublishPanelOpenState(*pPanel);
		return true;
	}

	const std::vector<std::unique_ptr<EditorPanel>>& PanelManager::GetPanels() const
	{
		return _vecPanels;
	}

	void PanelManager::SyncPanelOpenStateEvents()
	{
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : _vecPanels)
		{
			const EditorPanel* pPanel = refPanelOwner.get();
			if (!pPanel)
			{
				continue;
			}

			const bool bOpen = pPanel->IsOpen();
			std::unordered_map<std::string, bool>::iterator itOpenState = _mapLastKnownOpenStates.find(pPanel->GetId());
			if (itOpenState != _mapLastKnownOpenStates.end() && itOpenState->second == bOpen)
			{
				continue;
			}

			_mapLastKnownOpenStates[pPanel->GetId()] = bOpen;
			PublishPanelOpenState(*pPanel);
		}
	}

	void PanelManager::PublishPanelOpenState(const EditorPanel& refPanel) const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorPanelOpenStateChangedEvent event{};
		event.strPanelId = refPanel.GetId();
		event.bOpen = refPanel.IsOpen();
		_pEventBus->Publish(event);
	}
}
