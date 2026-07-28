#pragma once

#include "Function/Asset/TerrainData.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace AshEditor
{
	class EditorCommand;
	struct EditorCommandDocumentKey;

	enum class EditorCommandRecordResult : uint8_t;

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
		virtual std::size_t RemoveCommandsForDocument(
			const EditorCommandDocumentKey& refKey)
		{
			(void)refKey;
			return 0;
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
