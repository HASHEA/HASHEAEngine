#pragma once
#include "Function/Scene/Scene.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	struct EditorContext;

	enum class EditorCommandSelectionMode : uint8_t
	{
		Keep = 0,
		Clear,
		Entity
	};

	struct EditorCommandSelection
	{
		EditorCommandSelectionMode mode = EditorCommandSelectionMode::Keep;
		AshEngine::EntityId entity_id = 0;

		static EditorCommandSelection keep()
		{
			return {};
		}

		static EditorCommandSelection clear()
		{
			return { EditorCommandSelectionMode::Clear, 0 };
		}

		static EditorCommandSelection entity(AshEngine::EntityId entity_id)
		{
			return entity_id != 0
				? EditorCommandSelection{ EditorCommandSelectionMode::Entity, entity_id }
				: clear();
		}
	};

	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		virtual const char* get_label() const = 0;
		virtual bool execute(EditorContext& context) = 0;
		virtual bool undo(EditorContext& context) = 0;
		virtual bool try_merge(const EditorCommand& subsequent_command)
		{
			(void)subsequent_command;
			return false;
		}
		virtual EditorCommandSelection get_selection_after_execute() const
		{
			return EditorCommandSelection::keep();
		}
		virtual EditorCommandSelection get_selection_after_undo() const
		{
			return EditorCommandSelection::keep();
		}
	};

	class CompositeCommand final : public EditorCommand
	{
	public:
		explicit CompositeCommand(std::string label = {});

		void append(std::unique_ptr<EditorCommand> command);
		bool is_empty() const;
		size_t get_command_count() const;
		std::unique_ptr<EditorCommand> release_single_command();

		const char* get_label() const override;
		bool execute(EditorContext& context) override;
		bool undo(EditorContext& context) override;
		EditorCommandSelection get_selection_after_execute() const override;
		EditorCommandSelection get_selection_after_undo() const override;

	private:
		std::string m_label{};
		std::vector<std::unique_ptr<EditorCommand>> m_commands{};
	};
}
