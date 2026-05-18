#include "Panels/ConsolePanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorStringUtils.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorSettingsService.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

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

		AshEngine::UIColor GetSeverityColor(AshEngine::UIContext& refUi, ConsoleMessageSeverity eSeverity)
		{
			switch (eSeverity)
			{
			case ConsoleMessageSeverity::Trace:
				return GetEditorSubtleTextColor(refUi);
			case ConsoleMessageSeverity::Info:
				return GetEditorTextColor(refUi);
			case ConsoleMessageSeverity::Warning:
				return GetEditorWarningTextColor(refUi);
			case ConsoleMessageSeverity::Error:
				return GetEditorErrorTextColor(refUi);
			default:
				return GetEditorTextColor(refUi);
			}
		}

		bool MatchesConsoleFilter(
			const ConsoleMessage& refMessage,
			const std::string& strLoweredFilterText,
			std::string_view svSourceFilter,
			const SeverityFilterOption& refSeverityFilter)
		{
			if (!refSeverityFilter.bMatchAll && refMessage.eSeverity != refSeverityFilter.eSeverity)
			{
				return false;
			}

			if (!svSourceFilter.empty() && refMessage.strSource != svSourceFilter)
			{
				return false;
			}

			if (strLoweredFilterText.empty())
			{
				return true;
			}

			return
				ToLowerCopy(refMessage.strText).find(strLoweredFilterText) != std::string::npos ||
				ToLowerCopy(refMessage.strSource).find(strLoweredFilterText) != std::string::npos ||
				ToLowerCopy(GetSeverityLabel(refMessage.eSeverity)).find(strLoweredFilterText) != std::string::npos;
		}

		const char* GetSeverityShortLabel(ConsoleMessageSeverity eSeverity)
		{
			switch (eSeverity)
			{
			case ConsoleMessageSeverity::Trace:
				return "Trace";
			case ConsoleMessageSeverity::Info:
				return "Info";
			case ConsoleMessageSeverity::Warning:
				return "Warn";
			case ConsoleMessageSeverity::Error:
				return "Error";
			default:
				return "Info";
			}
		}

		struct ConsoleSeverityCounts
		{
			uint32_t uTraceCount = 0;
			uint32_t uInfoCount = 0;
			uint32_t uWarningCount = 0;
			uint32_t uErrorCount = 0;
		};

		struct ConsoleMessageContextAction
		{
			bool bApplySourceFilter = false;
			bool bApplySeverityFilter = false;
			bool bClearSourceFilter = false;
			bool bClearSeverityFilter = false;
			bool bClearTextFilter = false;
			bool bResetAllFilters = false;
			bool bClearConsole = false;
			std::string strSourceFilter{};
			int32_t iSeverityFilterIndex = 0;
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

		std::vector<std::string> BuildConsoleSourceOptions(
			const std::vector<ConsoleMessage>& vecMessages,
			std::string_view svSelectedSource)
		{
			std::vector<std::string> vecSources{};
			vecSources.reserve(vecMessages.size() + 2);
			vecSources.emplace_back("All");

			std::unordered_set<std::string> setSeenSources{};
			setSeenSources.reserve(vecMessages.size());
			for (const ConsoleMessage& refMessage : vecMessages)
			{
				if (refMessage.strSource.empty())
				{
					continue;
				}
				if (setSeenSources.insert(refMessage.strSource).second)
				{
					vecSources.push_back(refMessage.strSource);
				}
			}

			std::sort(vecSources.begin() + 1, vecSources.end());
			if (!svSelectedSource.empty() &&
				setSeenSources.find(std::string(svSelectedSource)) == setSeenSources.end())
			{
				vecSources.push_back(std::string(svSelectedSource));
			}

			return vecSources;
		}

		int32_t FindConsoleSourceFilterIndex(
			const std::vector<std::string>& vecSourceOptions,
			std::string_view svSelectedSource)
		{
			if (svSelectedSource.empty())
			{
				return 0;
			}

			for (size_t uIndex = 1; uIndex < vecSourceOptions.size(); ++uIndex)
			{
				if (vecSourceOptions[uIndex] == svSelectedSource)
				{
					return static_cast<int32_t>(uIndex);
				}
			}

			return 0;
		}

		int32_t GetSeverityFilterIndex(ConsoleMessageSeverity eSeverity)
		{
			for (size_t uIndex = 0; uIndex < kSeverityFilters.size(); ++uIndex)
			{
				if (!kSeverityFilters[uIndex].bMatchAll && kSeverityFilters[uIndex].eSeverity == eSeverity)
				{
					return static_cast<int32_t>(uIndex);
				}
			}

			return 0;
		}

		uint32_t GetSeverityCount(const ConsoleSeverityCounts& refCounts, ConsoleMessageSeverity eSeverity)
		{
			switch (eSeverity)
			{
			case ConsoleMessageSeverity::Trace:
				return refCounts.uTraceCount;
			case ConsoleMessageSeverity::Info:
				return refCounts.uInfoCount;
			case ConsoleMessageSeverity::Warning:
				return refCounts.uWarningCount;
			case ConsoleMessageSeverity::Error:
				return refCounts.uErrorCount;
			default:
				return 0;
			}
		}

		bool DrawSeverityFilterChip(
			AshEngine::UIContext& refUi,
			const ConsoleSeverityCounts& refCounts,
			ConsoleMessageSeverity eSeverity,
			int32_t& iSeverityFilterIndex)
		{
			const int32_t iTargetFilterIndex = GetSeverityFilterIndex(eSeverity);
			const bool bSelected = iSeverityFilterIndex == iTargetFilterIndex;
			const std::string strLabel =
				std::string(GetSeverityShortLabel(eSeverity)) + " " + std::to_string(GetSeverityCount(refCounts, eSeverity));

			if (bSelected)
			{
				PushEditorSelectedButtonStyle(refUi);
			}

			const bool bClicked = refUi.small_button(strLabel.c_str());

			if (bSelected)
			{
				PopEditorSelectedButtonStyle(refUi);
			}

			if (!bClicked)
			{
				return false;
			}

			iSeverityFilterIndex = bSelected ? 0 : iTargetFilterIndex;
			return true;
		}

		void DrawConsoleFilterSummary(
			AshEngine::UIContext& refUi,
			const SeverityFilterOption& refSeverityFilter,
			std::string_view svSourceFilter,
			std::string_view svTextFilter)
		{
			bool bHasSummary = false;

			if (!refSeverityFilter.bMatchAll)
			{
				refUi.text("Active:");
				refUi.same_line();
				refUi.text_colored(
					GetSeverityColor(refUi, refSeverityFilter.eSeverity),
					"Severity %s",
					refSeverityFilter.pLabel);
				bHasSummary = true;
			}

			if (!svSourceFilter.empty())
			{
				if (!bHasSummary)
				{
					refUi.text("Active:");
					bHasSummary = true;
				}
				refUi.same_line();
				refUi.text("Source %s", std::string(svSourceFilter).c_str());
			}

			if (!svTextFilter.empty())
			{
				if (!bHasSummary)
				{
					refUi.text("Active:");
					bHasSummary = true;
				}
				refUi.same_line();
				refUi.text("Search \"%s\"", std::string(svTextFilter).c_str());
			}
		}

		void DrawConsoleMessageContextMenu(
			AshEngine::UIContext& refUi,
			const ConsoleMessage& refMessage,
			ConsoleMessageContextAction& refAction)
		{
			if (!refUi.begin_popup_context_item("ConsoleMessageContextMenu"))
			{
				return;
			}

			const bool bHasSource = !refMessage.strSource.empty();
			if (refUi.menu_item("Filter This Source", nullptr, false, bHasSource))
			{
				refAction.bApplySourceFilter = true;
				refAction.strSourceFilter = refMessage.strSource;
				refUi.close_current_popup();
			}
			if (refUi.menu_item("Only This Severity"))
			{
				refAction.bApplySeverityFilter = true;
				refAction.iSeverityFilterIndex = GetSeverityFilterIndex(refMessage.eSeverity);
				refUi.close_current_popup();
			}
			refUi.separator();
			if (refUi.menu_item("Clear Source Filter"))
			{
				refAction.bClearSourceFilter = true;
				refUi.close_current_popup();
			}
			if (refUi.menu_item("Clear Severity Filter"))
			{
				refAction.bClearSeverityFilter = true;
				refUi.close_current_popup();
			}
			if (refUi.menu_item("Clear Text Filter"))
			{
				refAction.bClearTextFilter = true;
				refUi.close_current_popup();
			}
			refUi.separator();
			if (refUi.menu_item("Reset All Filters"))
			{
				refAction.bResetAllFilters = true;
				refUi.close_current_popup();
			}
			if (refUi.menu_item("Clear Console"))
			{
				refAction.bClearConsole = true;
				refUi.close_current_popup();
			}

			refUi.end_popup();
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
			_strSourceFilter = settings.strConsoleSourceFilter;
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
		settings.strConsoleSourceFilter = _strSourceFilter;
		settings.iConsoleSeverityFilter = _iSeverityFilterIndex;
	}

	bool ConsolePanel::HasAnyFilters() const
	{
		return !_strFilterText.empty() || !_strSourceFilter.empty() || _iSeverityFilterIndex != 0;
	}

	void ConsolePanel::ResetFilters()
	{
		_strFilterText.clear();
		_strSourceFilter.clear();
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
		const std::vector<std::string> vecSourceOptions = BuildConsoleSourceOptions(_vecMessages, _strSourceFilter);
		std::vector<const char*> vecSourceLabels{};
		vecSourceLabels.reserve(vecSourceOptions.size());
		for (const std::string& strSourceOption : vecSourceOptions)
		{
			vecSourceLabels.push_back(strSourceOption.c_str());
		}
		int32_t iSourceFilterIndex = FindConsoleSourceFilterIndex(vecSourceOptions, _strSourceFilter);
		const std::string strLoweredFilterText = ToLowerCopy(_strFilterText);
		const ConsoleSeverityCounts countsSeverity = CountConsoleSeverities(_vecMessages);
		uint32_t uVisibleMessageCount = 0;
		for (const ConsoleMessage& refMessage : _vecMessages)
		{
			if (MatchesConsoleFilter(refMessage, strLoweredFilterText, _strSourceFilter, refSeverityFilter))
			{
				++uVisibleMessageCount;
			}
		}

		refUi.text("Messages: %u / %u", uVisibleMessageCount, static_cast<uint32_t>(_vecMessages.size()));
		refUi.same_line();
		DrawSeverityFilterChip(refUi, countsSeverity, ConsoleMessageSeverity::Trace, _iSeverityFilterIndex);
		refUi.same_line();
		DrawSeverityFilterChip(refUi, countsSeverity, ConsoleMessageSeverity::Info, _iSeverityFilterIndex);
		refUi.same_line();
		DrawSeverityFilterChip(refUi, countsSeverity, ConsoleMessageSeverity::Warning, _iSeverityFilterIndex);
		refUi.same_line();
		DrawSeverityFilterChip(refUi, countsSeverity, ConsoleMessageSeverity::Error, _iSeverityFilterIndex);
		refUi.same_line();
		refUi.set_next_item_width(180.0f);
		refUi.input_text("Filter", _strFilterText);
		refUi.same_line();
		refUi.set_next_item_width(150.0f);
		if (refUi.combo("Source", iSourceFilterIndex, vecSourceLabels))
		{
			_strSourceFilter =
				iSourceFilterIndex <= 0 || iSourceFilterIndex >= static_cast<int32_t>(vecSourceOptions.size())
				? std::string{}
				: vecSourceOptions[static_cast<size_t>(iSourceFilterIndex)];
		}
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
		DrawConsoleFilterSummary(refUi, refSeverityFilter, _strSourceFilter, _strFilterText);
		if (HasAnyFilters())
		{
			refUi.separator();
		}
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
				if (!_strSourceFilter.empty())
				{
					refUi.text("Source: %s", _strSourceFilter.c_str());
				}
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
				ConsoleMessageContextAction action{};

				for (const ConsoleMessage& refMessage : _vecMessages)
				{
					if (!MatchesConsoleFilter(refMessage, strLoweredFilterText, _strSourceFilter, refSeverityFilter))
					{
						continue;
					}

					refUi.table_next_row();
					refUi.table_next_column();
					refUi.text_colored(
						GetSeverityColor(refUi, refMessage.eSeverity),
						"%s",
						GetSeverityLabel(refMessage.eSeverity));
					refUi.table_next_column();
					const char* pSourceLabel = refMessage.strSource.empty() ? "-" : refMessage.strSource.c_str();
					refUi.push_id(static_cast<int32_t>(&refMessage - _vecMessages.data()));
					if (refUi.small_button(pSourceLabel))
					{
						_strSourceFilter = refMessage.strSource;
					}
					refUi.pop_id();
					refUi.table_next_column();
					refUi.text_wrapped("%s", refMessage.strText.c_str());
					DrawConsoleMessageContextMenu(
						refUi,
						refMessage,
						action);
				}

				refUi.end_table();

				if (action.bResetAllFilters)
				{
					ResetFilters();
				}
				else
				{
					if (action.bApplySourceFilter)
					{
						_strSourceFilter = action.strSourceFilter;
					}
					if (action.bApplySeverityFilter)
					{
						_iSeverityFilterIndex = action.iSeverityFilterIndex;
					}
					if (action.bClearSourceFilter)
					{
						_strSourceFilter.clear();
					}
					if (action.bClearSeverityFilter)
					{
						_iSeverityFilterIndex = 0;
					}
					if (action.bClearTextFilter)
					{
						_strFilterText.clear();
					}
				}

				if (action.bClearConsole)
				{
					ClearMessages();
				}
			}
		}
		refUi.end_child();

		EndPanelWindow(frameContext);
	}
}
