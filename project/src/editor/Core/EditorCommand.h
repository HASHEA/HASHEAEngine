#pragma once

#include "Core/EditorSceneTypes.h"

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
		EditorCommandSelectionMode eMode = EditorCommandSelectionMode::Keep;
		SceneEntityId uEntityId = 0;

		static EditorCommandSelection Keep()
		{
			return {};
		}

		static EditorCommandSelection Clear()
		{
			return { EditorCommandSelectionMode::Clear, 0 };
		}

		static EditorCommandSelection Entity(SceneEntityId uEntityId)
		{
			return uEntityId != 0
				? EditorCommandSelection{ EditorCommandSelectionMode::Entity, uEntityId }
				: Clear();
		}
	};

	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		virtual const char* GetLabel() const = 0;
		virtual bool Execute(EditorContext& refContext) = 0;
		virtual bool Undo(EditorContext& refContext) = 0;
		virtual bool TryMerge(const EditorCommand& refSubsequentCommand)
		{
			(void)refSubsequentCommand;
			return false;
		}
		virtual EditorCommandSelection GetSelectionAfterExecute() const
		{
			return EditorCommandSelection::Keep();
		}
		virtual EditorCommandSelection GetSelectionAfterUndo() const
		{
			return EditorCommandSelection::Keep();
		}
	};

	class CompositeCommand final : public EditorCommand
	{
	public:
		explicit CompositeCommand(std::string strLabel = {});

		void Append(std::unique_ptr<EditorCommand> upCommand);
		bool IsEmpty() const;
		size_t GetCommandCount() const;
		std::unique_ptr<EditorCommand> ReleaseSingleCommand();

		const char* GetLabel() const override;
		bool Execute(EditorContext& refContext) override;
		bool Undo(EditorContext& refContext) override;
		EditorCommandSelection GetSelectionAfterExecute() const override;
		EditorCommandSelection GetSelectionAfterUndo() const override;

	private:
		std::string _strLabel{};
		std::vector<std::unique_ptr<EditorCommand>> _vecCommands{};
	};
}
