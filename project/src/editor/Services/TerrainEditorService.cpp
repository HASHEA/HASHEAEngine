#include "Services/TerrainEditorService.h"

#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"

#include <chrono>
#include <exception>
#include <utility>

namespace AshEditor
{
	bool TerrainEditorService::Initialize(
		AshEngine::AssetDatabase& refAssets,
		IEditorCommandExecutor& refCommands)
	{
		Shutdown();
		_pAssets = &refAssets;
		_pCommands = &refCommands;
		return true;
	}

	void TerrainEditorService::Shutdown()
	{
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_strLastError.clear();
		_core.Close();
		_pCommands = nullptr;
		_pAssets = nullptr;
	}

	void TerrainEditorService::Update()
	{
		if (!_pendingLoad.valid() ||
			_pendingLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return;
		}

		CompletePendingLoad();
	}

	bool TerrainEditorService::SubmitIntent(const TerrainEditorIntent& refIntent)
	{
		if (!_pAssets || !_pCommands || refIntent.kind != TerrainEditorIntent::Kind::SelectAsset ||
			!_core.Reduce(refIntent))
		{
			return false;
		}

		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_strLastError.clear();
		if (refIntent.asset_id == 0u)
		{
			return true;
		}

		if (_core.GetWorkingSet() && _core.GetAssetId() == refIntent.asset_id)
		{
			return true;
		}

		_pendingLoad = _pAssets->load_terrain_by_id_async(refIntent.asset_id);
		if (!_pendingLoad.valid())
		{
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = "AssetDatabase did not return a Terrain load future.";
			return false;
		}

		_pendingLoadAssetId = refIntent.asset_id;
		_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Pending);
		return true;
	}

	bool TerrainEditorService::OpenSnapshotForAuthoring(
		const AshEngine::TerrainAssetSnapshot& refSnapshot)
	{
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_strLastError.clear();
		AshEngine::TerrainWorkingSet workingSet{};
		std::string strError{};
		if (AshEngine::make_terrain_working_set(refSnapshot, workingSet, &strError) &&
			_core.Open(std::move(workingSet)))
		{
			return true;
		}

		_strLastError = strError.empty()
			? "Terrain snapshot is invalid for authoring."
			: std::move(strError);
		return false;
	}

	bool TerrainEditorService::ApplyStrokePatches(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches,
		const AshEngine::TerrainEditPatchDirection eDirection)
	{
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyStrokePatches(
				assetId,
				layerId,
				refPatches,
				eDirection,
				dirtyComponents,
				&strError))
		{
			_strLastError = strError.empty() ? "Terrain stroke patch replay failed." : std::move(strError);
			return false;
		}

		_strLastError.clear();
		return true;
	}

	const TerrainEditorPreviewState& TerrainEditorService::GetPreviewState() const
	{
		return _core.GetPreviewState();
	}

	AshEngine::TerrainAssetId TerrainEditorService::GetSelectedAssetId() const
	{
		return _core.GetAssetId();
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorService::GetWorkingSet() const
	{
		return _core.GetWorkingSet();
	}

	bool TerrainEditorService::HasDirtyAssets() const
	{
		return _core.IsDirty();
	}

	bool TerrainEditorService::HasBlockingOperation() const
	{
		return _pendingLoad.valid();
	}

	const std::string& TerrainEditorService::GetLastError() const
	{
		return _strLastError;
	}

	void TerrainEditorService::CompletePendingLoad()
	{
		const AshEngine::TerrainAssetId completedAssetId = _pendingLoadAssetId;
		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> completedLoad =
			std::move(_pendingLoad);
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;

		try
		{
			const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot = completedLoad.get();
			if (completedAssetId == 0u || completedAssetId != _core.GetAssetId())
			{
				return;
			}
			if (!snapshot || snapshot->failed || snapshot->asset_id != completedAssetId)
			{
				_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
				_strLastError = snapshot && !snapshot->failure_detail.empty()
					? snapshot->failure_detail
					: "Terrain asset load failed.";
				return;
			}

			if (!OpenSnapshotForAuthoring(*snapshot))
			{
				_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
				if (_strLastError.empty())
				{
					_strLastError = "Terrain asset could not open an authoring working set.";
				}
				return;
			}

			_strLastError.clear();
		}
		catch (const std::exception& refException)
		{
			if (completedAssetId == _core.GetAssetId())
			{
				_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
				_strLastError = refException.what();
			}
		}
	}
}
