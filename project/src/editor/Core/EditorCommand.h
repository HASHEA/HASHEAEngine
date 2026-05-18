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
		Entity,
		Entities
	};

	struct EditorCommandSelection
	{
		EditorCommandSelectionMode eMode = EditorCommandSelectionMode::Keep;
		SceneEntityId uEntityId = 0;
		std::vector<SceneEntityId> vecEntityIds{};

		static EditorCommandSelection Keep()
		{
			return {};
		}

		static EditorCommandSelection Clear()
		{
			EditorCommandSelection selection{};
			selection.eMode = EditorCommandSelectionMode::Clear;
			return selection;
		}

		static EditorCommandSelection Entity(SceneEntityId uEntityId)
		{
			return uEntityId != 0
				? EditorCommandSelection{ EditorCommandSelectionMode::Entity, uEntityId, {} }
				: Clear();
		}

		static EditorCommandSelection Entities(const std::vector<SceneEntityId>& vecEntityIds)
		{
			EditorCommandSelection selection{};
			for (const SceneEntityId uEntityId : vecEntityIds)
			{
				if (uEntityId != 0)
				{
					selection.vecEntityIds.push_back(uEntityId);
				}
			}
			selection.eMode = selection.vecEntityIds.empty()
				? EditorCommandSelectionMode::Clear
				: EditorCommandSelectionMode::Entities;
			selection.uEntityId = selection.vecEntityIds.empty() ? 0 : selection.vecEntityIds.back();
			return selection;
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
