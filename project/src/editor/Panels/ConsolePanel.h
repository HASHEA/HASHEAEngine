#pragma once
#include "Core/EditorPanel.h"
#include <cstdint>
#include <string>
#include <vector>

namespace AshEditor
{
	enum class ConsoleMessageSeverity : uint8_t
	{
		Trace = 0,
		Info,
		Warning,
		Error
	};

	struct ConsoleMessage
	{
		ConsoleMessageSeverity severity = ConsoleMessageSeverity::Info;
		std::string source = "Editor";
		std::string text{};
	};

	class ConsolePanel final : public EditorPanel
	{
	public:
		ConsolePanel();

	public:
		void add_message(std::string message, ConsoleMessageSeverity severity = ConsoleMessageSeverity::Info, std::string source = "Editor");
		void clear_messages();
		const std::vector<ConsoleMessage>& get_messages() const;

		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		void reset_filters();
		void sync_settings(EditorContext& context) const;
		bool has_any_filters() const;

	private:
		std::string m_filterText{};
		int32_t m_severityFilterIndex = 0;
		std::vector<ConsoleMessage> m_messages{};
	};
}
