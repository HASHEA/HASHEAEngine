#pragma once

#include "Function/Asset/TerrainData.h"

#include <cstdint>
#include <memory>

namespace AshEditor
{
	class EditorCommand;

	enum class EditorCommandRecordResult : uint8_t
	{
		Recorded = 0,
		RolledBack,
		RollbackFailed
	};

	class IEditorCommandExecutor
	{
	public:
		virtual ~IEditorCommandExecutor() = default;

		virtual bool ExecuteCommand(std::unique_ptr<EditorCommand> upCommand) = 0;
		virtual EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<EditorCommand> upCommand) = 0;
		virtual bool RemoveCommandsForTerrainAsset(
			const AshEngine::TerrainAssetId assetId) noexcept
		{
			(void)assetId;
			return true;
		}
		virtual bool BeginCommandTransaction(const char* pLabel)
		{
			(void)pLabel;
			return false;
		}
		virtual bool CommitCommandTransaction()
		{
			return false;
		}
		virtual void CancelCommandTransaction()
		{
		}
	};
}
