#include "Panels/ConsolePanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorStringUtils.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorSettingsService.h"

#include <algorithm>
#include <array>
#include <utility>

namespace AshEditor
{
	namespace
	{
		struct SeverityFilterOption
		{
			const char* pLabel = "";
			ConsoleMessageSeverity eSeverity = ConsoleMessageSeverity::Info;
			bool bMatchAll = false;
		};

		constexpr std::array<SeverityFilterOption, 5> kSeverityFilters{ {
			{ "All", ConsoleMessageSeverity::Info, true },
			{ "Trace", ConsoleMessageSeverity::Trace, false },
			{ "Info", ConsoleMessageSeverity::Info, false },
			{ "Warning", ConsoleMessageSeverity::Warning, false },
			{ "Error", ConsoleMessageSeverity::Error, false },
		} };

		ConsoleMessageSeverity InferSeverityFromText(const std::string& strText)
		{
			const std::string strLowered = ToLowerCopy(strText);
			if (strLowered.find("error") != std::string::npos || strLowered.find("failed") != std::string::npos)
			{
				return ConsoleMessageSeverity::Error;
			}
			if (strLowered.find("warn") != std::string::npos || strLowered.find("unsafe") != std::string::npos)
			{
				return ConsoleMessageSeverity::Warning;
			}
			if (strLowered.find("trace") != std::string::npos)
			{
				return ConsoleMessageSeverity::Trace;
			}
			return ConsoleMessageSeverity::Info;
		}

		const char* GetSeverityLabel(ConsoleMessageSeverity eSeverity)
		{
			switch (eSeverity)
			{
			case ConsoleMessageSeverity::Trace:
				return "Trace";
			case ConsoleMessageSeverity::Info:
				return "Info";
			case ConsoleMessageSeverity::Warning:
				return "Warning";
			case ConsoleMessageSeverity::Error:
				return "Error";
			default:
				return "Info";
			}
		}

		AshEngine::UIColor GetSeverityColor(ConsoleMessageSeverity eSeverity)
		{
			switch (eSeverity)
			{
			case ConsoleMessageSeverity::Trace:
				return { 0.55f, 0.60f, 0.70f, 1.0f };
			case ConsoleMessageSeverity::Info:
				return { 0.80f, 0.82f, 0.86f, 1.0f };
			case ConsoleMessageSeverity::Warning:
				return { 0.95f, 0.75f, 0.25f, 1.0f };
			case ConsoleMessageSeverity::Error:
				return { 0.95f, 0.38f, 0.30f, 1.0f };
			default:
				return { 0.80f, 0.82f, 0.86f, 1.0f };
			}
		}

		bool MatchesConsoleFilter(
			const ConsoleMessage& refMessage,
			const std::string& strFilterText,
			const SeverityFilterOption& refSeverityFilter)
		{
			if (!refSeverityFilter.bMatchAll && refMessage.eSeverity != refSeverityFilter.eSeverity)
			{
				return false;
			}

			if (strFilterText.empty())
			{
				return true;
			}

			const std::string strLoweredFilter = ToLowerCopy(strFilterText);
			return
				ToLowerCopy(refMessage.strText).find(strLoweredFilter) != std::string::npos ||
				ToLowerCopy(refMessage.strSource).find(strLoweredFilter) != std::string::npos ||
				ToLowerCopy(GetSeverityLabel(refMessage.eSeverity)).find(strLoweredFilter) != std::string::npos;
		}

		struct ConsoleSeverityCounts
		{
			uint32_t uTraceCount = 0;
			uint32_t uInfoCount = 0;
			uint32_t uWarningCount = 0;
			uint32_t uErrorCount = 0;
		};

		ConsoleSeverityCounts CountConsoleSeverities(const std::vector<ConsoleMessage>& vecMessages)
		{
			ConsoleSeverityCounts countsSeverity{};
			for (const ConsoleMessage& refMessage : vecMessages)
			{
				switch (refMessage.eSeverity)
				{
				case ConsoleMessageSeverity::Trace:
					++countsSeverity.uTraceCount;
					break;
				case ConsoleMessageSeverity::Info:
					++countsSeverity.uInfoCount;
					break;
				case ConsoleMessageSeverity::Warning:
					++countsSeverity.uWarningCount;
					break;
				case ConsoleMessageSeverity::Error:
					++countsSeverity.uErrorCount;
					break;
				default:
					break;
				}
			}
			return countsSeverity;
		}
	}

	ConsolePanel::ConsolePanel(ConsolePanelDeps deps)
		: EditorPanel(EditorPanelIds::Console, EditorWindowTitles::Console)
		, _deps(deps)
	{
	}

	void ConsolePanel::BindEventBus(EditorEventBus* pEventBus)
	{
		if (_eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}

		_eventBindings.Bind(pEventBus);
		if (!pEventBus)
		{
			return;
		}

		_eventBindings.Subscribe<EditorNotificationEvent>(
			[this](const EditorNotificationEvent& refEvent)
			{
				AddMessage(refEvent.strMessage, ConsoleMessageSeverity::Info, refEvent.strSource);
			});
	}

	void ConsolePanel::ClearDeps()
	{
		_deps = {};
	}

	void ConsolePanel::AddMessage(std::string strMessage, ConsoleMessageSeverity eSeverity, std::string strSource)
	{
		if (eSeverity == ConsoleMessageSeverity::Info)
		{
			eSeverity = InferSeverityFromText(strMessage);
		}

		_vecMessages.push_back({ eSeverity, std::move(strSource), std::move(strMessage) });
	}

	void ConsolePanel::ClearMessages()
	{
		_vecMessages.clear();
	}

	const std::vector<ConsoleMessage>& ConsolePanel::GetMessages() const
	{
		return _vecMessages;
	}

	void ConsolePanel::OnAttach()
	{
		if (_deps.pSettingsService)
		{
			const EditorSettings& settings = _deps.pSettingsService->GetSettings();
			_strFilterText = settings.strConsoleFilterText;
			_iSeverityFilterIndex = settings.iConsoleSeverityFilter;
		}
		HLogInfo("ConsolePanel attached.");
	}

	void ConsolePanel::OnDetach()
	{
		UnsubscribeEvents();
		ClearDeps();
	}

	void ConsolePanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	void ConsolePanel::SyncSettings() const
	{
		if (!_deps.pSettingsService)
		{
			return;
		}

		EditorSettings& settings = _deps.pSettingsService->GetSettings();
		settings.strConsoleFilterText = _strFilterText;
		settings.iConsoleSeverityFilter = _iSeverityFilterIndex;
	}

	bool ConsolePanel::HasAnyFilters() const
	{
		return !_strFilterText.empty() || _iSeverityFilterIndex != 0;
	}

	void ConsolePanel::ResetFilters()
	{
		_strFilterText.clear();
		_iSeverityFilterIndex = 0;
	}

	void ConsolePanel::OnGui(const EditorFrameContext& frameContext)
	{
		if (!BeginPanelWindow(frameContext))
		{
			EndPanelWindow(frameContext);
			return;
		}

		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		const SeverityFilterOption& refSeverityFilter =
			kSeverityFilters[std::clamp(_iSeverityFilterIndex, 0, static_cast<int32_t>(kSeverityFilters.size() - 1))];
		const std::vector<const char*> vecSeverityLabels{ "All", "Trace", "Info", "Warning", "Error" };
		const ConsoleSeverityCounts countsSeverity = CountConsoleSeverities(_vecMessages);
		uint32_t uVisibleMessageCount = 0;
		for (const ConsoleMessage& refMessage : _vecMessages)
		{
			if (MatchesConsoleFilter(refMessage, _strFilterText, refSeverityFilter))
			{
				++uVisibleMessageCount;
			}
		}

		refUi.text("Messages: %u / %u", uVisibleMessageCount, static_cast<uint32_t>(_vecMessages.size()));
		refUi.same_line();
		refUi.text(
			"T:%u I:%u W:%u E:%u",
			countsSeverity.uTraceCount,
			countsSeverity.uInfoCount,
			countsSeverity.uWarningCount,
			countsSeverity.uErrorCount);
		refUi.same_line();
		refUi.set_next_item_width(180.0f);
		refUi.input_text("Filter", _strFilterText);
		refUi.same_line();
		refUi.set_next_item_width(120.0f);
		refUi.combo("Severity", _iSeverityFilterIndex, vecSeverityLabels);
		refUi.same_line();
		if (refUi.button("Clear"))
		{
			ClearMessages();
		}
		refUi.same_line();
		refUi.begin_disabled(!HasAnyFilters());
		if (refUi.button("Reset Filters"))
		{
			ResetFilters();
		}
		refUi.end_disabled();
		refUi.separator();
		SyncSettings();

		const bool bShowMessages = refUi.begin_child("ConsoleMessages", {}, AshEngine::UIChildFlagBits::Border);
		if (bShowMessages)
		{
			if (_vecMessages.empty())
			{
				refUi.text_unformatted("No console messages yet.");
				refUi.text_unformatted("Runtime and editor logs will appear here when available.");
			}
			else if (uVisibleMessageCount == 0)
			{
				refUi.text_unformatted("No messages match the current filter.");
				refUi.text("Severity: %s", refSeverityFilter.pLabel);
				if (!_strFilterText.empty())
				{
					refUi.text("Filter: %s", _strFilterText.c_str());
				}
				refUi.begin_disabled(!HasAnyFilters());
				if (refUi.button("Clear Console Filters"))
				{
					ResetFilters();
				}
				refUi.end_disabled();
			}
			else if (refUi.begin_table(
				"ConsoleTable",
				3,
				AshEngine::UITableFlagBits::RowBg |
					AshEngine::UITableFlagBits::BordersInner |
					AshEngine::UITableFlagBits::Resizable |
					AshEngine::UITableFlagBits::SizingStretchProp |
					AshEngine::UITableFlagBits::ScrollY))
			{
				refUi.table_setup_column("Severity", AshEngine::UITableColumnFlagBits::WidthFixed, 80.0f);
				refUi.table_setup_column("Source", AshEngine::UITableColumnFlagBits::WidthFixed, 110.0f);
				refUi.table_setup_column("Message", AshEngine::UITableColumnFlagBits::WidthStretch);
				refUi.table_headers_row();

				for (const ConsoleMessage& refMessage : _vecMessages)
				{
					if (!MatchesConsoleFilter(refMessage, _strFilterText, refSeverityFilter))
					{
						continue;
					}

					refUi.table_next_row();
					refUi.table_next_column();
					refUi.text_colored(GetSeverityColor(refMessage.eSeverity), "%s", GetSeverityLabel(refMessage.eSeverity));
					refUi.table_next_column();
					refUi.text_unformatted(refMessage.strSource.c_str());
					refUi.table_next_column();
					refUi.text_wrapped("%s", refMessage.strText.c_str());
				}

				refUi.end_table();
			}
		}
		refUi.end_child();

		EndPanelWindow(frameContext);
	}
}
