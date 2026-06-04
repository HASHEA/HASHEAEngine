#pragma once
#include "Core/EditorEvents.h"
#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/ConsolePanelDeps.h"
#include "Core/EditorPanel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	using ConsoleMessageSeverity = EditorLogSeverity;

	struct ConsoleMessage
	{
		ConsoleMessageSeverity eSeverity = ConsoleMessageSeverity::Info;
		std::string strSource = "Editor";
		std::string strText{};
	};

	class ConsolePanel final : public EditorPanel
	{
	public:
		explicit ConsolePanel(ConsolePanelDeps deps = {});

	public:
		void AddMessage(std::string strMessage, ConsoleMessageSeverity eSeverity = ConsoleMessageSeverity::Info, std::string strSource = "Editor");
		void ClearMessages();
		const std::vector<ConsoleMessage>& GetMessages() const;
		void BindEventBus(EditorEventBus* pEventBus);

		void OnAttach() override;
		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;

	private:
		void ClearDeps();
		void UnsubscribeEvents();
		void ResetFilters();
		void SyncSettings() const;
		bool HasAnyFilters() const;
		bool ExportVisibleMessages(const std::vector<ConsoleMessage>& vecVisibleMessages) const;

	private:
		ConsolePanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::string _strFilterText{};
		std::string _strSourceFilter{};
		int32_t _iSeverityFilterIndex = 0;
		bool _bAutoScroll = true;
		size_t _uLastObservedMessageCount = 0;
		std::vector<ConsoleMessage> _vecMessages{};
	};
}
