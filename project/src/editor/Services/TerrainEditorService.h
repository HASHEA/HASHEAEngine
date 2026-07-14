#pragma once

#include "Core/TerrainEditorSessionCore.h"

#include <future>
#include <memory>
#include <string>

namespace AshEngine
{
	class AssetDatabase;
	struct TerrainAssetSnapshot;
}

namespace AshEditor
{
	class IEditorCommandExecutor;

	class TerrainEditorService final
	{
	public:
		bool Initialize(AshEngine::AssetDatabase& refAssets, IEditorCommandExecutor& refCommands);
		void Shutdown();
		void Update();
		bool SubmitIntent(const TerrainEditorIntent& refIntent);

		const TerrainEditorPreviewState& GetPreviewState() const;
		AshEngine::TerrainAssetId GetSelectedAssetId() const;
		bool HasDirtyAssets() const;
		bool HasBlockingOperation() const;
		const std::string& GetLastError() const;

	private:
		void CompletePendingLoad();

	private:
		AshEngine::AssetDatabase* _pAssets = nullptr;
		IEditorCommandExecutor* _pCommands = nullptr;
		TerrainEditorSessionCore _core{};
		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> _pendingLoad{};
		AshEngine::TerrainAssetId _pendingLoadAssetId = 0;
		std::string _strLastError{};
	};
}
