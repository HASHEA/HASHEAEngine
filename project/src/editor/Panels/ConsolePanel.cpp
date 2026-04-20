#include "Panels/ConsolePanel.h"
#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorSettingsService.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace AshEditor
{
	namespace
	{
		struct SeverityFilterOption
		{
			const char* label = "";
			ConsoleMessageSeverity severity = ConsoleMessageSeverity::Info;
			bool match_all = false;
		};

		constexpr std::array<SeverityFilterOption, 5> k_severityFilters{ {
			{ "All", ConsoleMessageSeverity::Info, true },
			{ "Trace", ConsoleMessageSeverity::Trace, false },
			{ "Info", ConsoleMessageSeverity::Info, false },
			{ "Warning", ConsoleMessageSeverity::Warning, false },
			{ "Error", ConsoleMessageSeverity::Error, false },
		} };

		auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		auto infer_severity_from_text(const std::string& text) -> ConsoleMessageSeverity
		{
			const std::string lowered = to_lower_copy(text);
			if (lowered.find("error") != std::string::npos || lowered.find("failed") != std::string::npos)
			{
				return ConsoleMessageSeverity::Error;
			}
			if (lowered.find("warn") != std::string::npos || lowered.find("unsafe") != std::string::npos)
			{
				return ConsoleMessageSeverity::Warning;
			}
			if (lowered.find("trace") != std::string::npos)
			{
				return ConsoleMessageSeverity::Trace;
			}
			return ConsoleMessageSeverity::Info;
		}

		auto get_severity_label(ConsoleMessageSeverity severity) -> const char*
		{
			switch (severity)
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

		auto get_severity_color(ConsoleMessageSeverity severity) -> AshEngine::UIColor
		{
			switch (severity)
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

		auto matches_console_filter(const ConsoleMessage& message, const std::string& filter_text, const SeverityFilterOption& severity_filter) -> bool
		{
			if (!severity_filter.match_all && message.severity != severity_filter.severity)
			{
				return false;
			}

			if (filter_text.empty())
			{
				return true;
			}

			const std::string lowered_filter = to_lower_copy(filter_text);
			return
				to_lower_copy(message.text).find(lowered_filter) != std::string::npos ||
				to_lower_copy(message.source).find(lowered_filter) != std::string::npos ||
				to_lower_copy(get_severity_label(message.severity)).find(lowered_filter) != std::string::npos;
		}

		struct ConsoleSeverityCounts
		{
			uint32_t trace = 0;
			uint32_t info = 0;
			uint32_t warning = 0;
			uint32_t error = 0;
		};

		auto count_console_severities(const std::vector<ConsoleMessage>& messages) -> ConsoleSeverityCounts
		{
			ConsoleSeverityCounts counts{};
			for (const ConsoleMessage& message : messages)
			{
				switch (message.severity)
				{
				case ConsoleMessageSeverity::Trace:
					++counts.trace;
					break;
				case ConsoleMessageSeverity::Info:
					++counts.info;
					break;
				case ConsoleMessageSeverity::Warning:
					++counts.warning;
					break;
				case ConsoleMessageSeverity::Error:
					++counts.error;
					break;
				default:
					break;
				}
			}
			return counts;
		}
	}

	ConsolePanel::ConsolePanel()
		: EditorPanel("console", "Console")
	{
	}

	void ConsolePanel::add_message(std::string message, ConsoleMessageSeverity severity, std::string source)
	{
		if (severity == ConsoleMessageSeverity::Info)
		{
			severity = infer_severity_from_text(message);
		}

		m_messages.push_back({ severity, std::move(source), std::move(message) });
	}

	void ConsolePanel::clear_messages()
	{
		m_messages.clear();
	}

	const std::vector<ConsoleMessage>& ConsolePanel::get_messages() const
	{
		return m_messages;
	}

	void ConsolePanel::on_attach(EditorContext& context)
	{
		if (context.settings_service)
		{
			const EditorSettings& settings = context.settings_service->get_settings();
			m_filterText = settings.console_filter_text;
			m_severityFilterIndex = settings.console_severity_filter;
		}
		HLogInfo("ConsolePanel attached.");
	}

	void ConsolePanel::sync_settings(EditorContext& context) const
	{
		if (!context.settings_service)
		{
			return;
		}

		EditorSettings& settings = context.settings_service->get_settings();
		settings.console_filter_text = m_filterText;
		settings.console_severity_filter = m_severityFilterIndex;
	}

	bool ConsolePanel::has_any_filters() const
	{
		return !m_filterText.empty() || m_severityFilterIndex != 0;
	}

	void ConsolePanel::reset_filters()
	{
		m_filterText.clear();
		m_severityFilterIndex = 0;
	}

	void ConsolePanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window(context))
		{
			end_panel_window(context);
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		const SeverityFilterOption& severity_filter = k_severityFilters[std::clamp(m_severityFilterIndex, 0, static_cast<int32_t>(k_severityFilters.size() - 1))];
		const std::vector<const char*> severity_labels{ "All", "Trace", "Info", "Warning", "Error" };
		const ConsoleSeverityCounts counts = count_console_severities(m_messages);
		uint32_t visible_message_count = 0;
		for (const ConsoleMessage& message : m_messages)
		{
			if (matches_console_filter(message, m_filterText, severity_filter))
			{
				++visible_message_count;
			}
		}

		ui.text("Messages: %u / %u", visible_message_count, static_cast<uint32_t>(m_messages.size()));
		ui.same_line();
		ui.text("T:%u I:%u W:%u E:%u", counts.trace, counts.info, counts.warning, counts.error);
		ui.same_line();
		ui.set_next_item_width(180.0f);
		ui.input_text("Filter", m_filterText);
		ui.same_line();
		ui.set_next_item_width(120.0f);
		ui.combo("Severity", m_severityFilterIndex, severity_labels);
		ui.same_line();
		if (ui.button("Clear"))
		{
			clear_messages();
		}
		ui.same_line();
		ui.begin_disabled(!has_any_filters());
		if (ui.button("Reset Filters"))
		{
			reset_filters();
		}
		ui.end_disabled();
		ui.separator();
		sync_settings(context);

		const bool show_messages = ui.begin_child("ConsoleMessages", {}, AshEngine::UIChildFlagBits::Border);
		if (show_messages)
		{
			if (m_messages.empty())
			{
				ui.text_unformatted("No console messages yet.");
				ui.text_unformatted("Runtime and editor logs will appear here when available.");
			}
			else if (visible_message_count == 0)
			{
				ui.text_unformatted("No messages match the current filter.");
				ui.text("Severity: %s", severity_filter.label);
				if (!m_filterText.empty())
				{
					ui.text("Filter: %s", m_filterText.c_str());
				}
				ui.begin_disabled(!has_any_filters());
				if (ui.button("Clear Console Filters"))
				{
					reset_filters();
				}
				ui.end_disabled();
			}
			else if (ui.begin_table(
				"ConsoleTable",
				3,
				AshEngine::UITableFlagBits::RowBg |
					AshEngine::UITableFlagBits::BordersInner |
					AshEngine::UITableFlagBits::Resizable |
					AshEngine::UITableFlagBits::SizingStretchProp |
					AshEngine::UITableFlagBits::ScrollY))
			{
				ui.table_setup_column("Severity", AshEngine::UITableColumnFlagBits::WidthFixed, 80.0f);
				ui.table_setup_column("Source", AshEngine::UITableColumnFlagBits::WidthFixed, 110.0f);
				ui.table_setup_column("Message", AshEngine::UITableColumnFlagBits::WidthStretch);
				ui.table_headers_row();

				for (const ConsoleMessage& message : m_messages)
				{
					if (!matches_console_filter(message, m_filterText, severity_filter))
					{
						continue;
					}

					ui.table_next_row();
					ui.table_next_column();
					ui.text_colored(get_severity_color(message.severity), "%s", get_severity_label(message.severity));
					ui.table_next_column();
					ui.text_unformatted(message.source.c_str());
					ui.table_next_column();
					ui.text_wrapped("%s", message.text.c_str());
				}

				ui.end_table();
			}
		}
		ui.end_child();

		end_panel_window(context);
	}
}
