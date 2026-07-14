#include "Services/TerrainEditorService.h"

#include "Core/IEditorCommandExecutor.h"
#include "Core/TerrainCommands.h"
#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace AshEditor
{
	namespace
	{
		using TerrainFileJobResult = std::pair<bool, std::string>;

		bool TryNormalizeAssetPathWithinRoot(
			const std::filesystem::path& pathAssetRoot,
			const std::filesystem::path& pathCandidate,
			std::filesystem::path& outRelativePath)
		{
			outRelativePath.clear();
			if (pathAssetRoot.empty() || pathCandidate.empty())
			{
				return false;
			}

			std::error_code errorCode{};
			const std::filesystem::path pathAbsoluteRoot = pathAssetRoot.is_absolute()
				? pathAssetRoot
				: std::filesystem::absolute(pathAssetRoot, errorCode);
			if (errorCode)
			{
				return false;
			}
			const std::filesystem::path pathNormalizedRoot =
				std::filesystem::weakly_canonical(pathAbsoluteRoot, errorCode);
			if (errorCode || pathNormalizedRoot.empty())
			{
				return false;
			}

			const std::filesystem::path pathAbsoluteCandidate = pathCandidate.is_absolute()
				? pathCandidate
				: pathNormalizedRoot / pathCandidate;
			const std::filesystem::path pathNormalizedCandidate =
				std::filesystem::weakly_canonical(pathAbsoluteCandidate, errorCode);
			if (errorCode || pathNormalizedCandidate.empty())
			{
				return false;
			}

			std::filesystem::path pathRelative = std::filesystem::relative(
				pathNormalizedCandidate,
				pathNormalizedRoot,
				errorCode).lexically_normal();
			if (errorCode || pathRelative.empty() || pathRelative.is_absolute())
			{
				return false;
			}
			for (const std::filesystem::path& pathSegment : pathRelative)
			{
				if (pathSegment == "..")
				{
					return false;
				}
			}

			std::string key = pathRelative.generic_string();
			std::transform(
				key.begin(),
				key.end(),
				key.begin(),
				[](const unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			outRelativePath = std::filesystem::path(std::move(key));
			return true;
		}

		struct TerrainCopyTemporaryGuard
		{
			std::filesystem::path path{};
			bool released = false;

			~TerrainCopyTemporaryGuard()
			{
				if (!released && !path.empty())
				{
					std::error_code ignored{};
					std::filesystem::remove(path, ignored);
				}
			}

			void Release() noexcept
			{
				released = true;
			}
		};

		std::atomic<uint64_t> g_nextTerrainCopyTemporarySerial{ 0u };

		std::filesystem::path MakeTerrainCopyTemporaryPath(
			const std::filesystem::path& destination)
		{
			const uint64_t serial =
				g_nextTerrainCopyTemporarySerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
#if defined(_WIN32)
			const uint64_t processId = static_cast<uint64_t>(GetCurrentProcessId());
#else
			const uint64_t processId = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
#endif
			std::filesystem::path temporary = destination;
			temporary += ".save-copy." + std::to_string(processId) + "." +
				std::to_string(serial) + ".tmp";
			return temporary;
		}

		AshEngine::TerrainContainerResult SaveTerrainCopyNew(
			const std::filesystem::path& destination,
			const AshEngine::TerrainAssetSnapshot& snapshot,
			std::string& outError)
		{
			const std::filesystem::path temporary =
				MakeTerrainCopyTemporaryPath(destination);
			TerrainCopyTemporaryGuard temporaryGuard{ temporary };
			std::error_code errorCode{};
			if (std::filesystem::exists(temporary, errorCode) || errorCode)
			{
				outError = "Terrain Save Copy As could not reserve a unique temporary path.";
				return AshEngine::TerrainContainerResult::IoFailure;
			}

			const AshEngine::TerrainContainerResult writeResult =
				AshEngine::save_terrain_container_incremental(temporary, snapshot, {}, nullptr, &outError);
			if (writeResult != AshEngine::TerrainContainerResult::Success)
			{
				return writeResult;
			}

			std::shared_ptr<const AshEngine::TerrainAssetSnapshot> validated{};
			std::string validationError{};
			const AshEngine::TerrainContainerResult validationResult =
				AshEngine::load_terrain_container(temporary, validated, nullptr, &validationError);
			if (validationResult != AshEngine::TerrainContainerResult::Success || !validated ||
				validated->content_generation != snapshot.content_generation ||
				validated->components.size() != snapshot.components.size())
			{
				outError = validationError.empty()
					? "Terrain Save Copy As temporary container failed validation."
					: std::move(validationError);
				return AshEngine::TerrainContainerResult::Corrupt;
			}

#if defined(_WIN32)
			if (MoveFileExW(
					temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE)
#else
			std::filesystem::create_hard_link(temporary, destination, errorCode);
			if (errorCode)
#endif
			{
				std::error_code existsError{};
				const bool destinationExists =
					std::filesystem::exists(destination, existsError) && !existsError;
				outError = destinationExists
					? "Terrain Save As destination appeared before the copy was published."
					: "Terrain Save Copy As failed to atomically publish the new asset.";
				return AshEngine::TerrainContainerResult::IoFailure;
			}
#if !defined(_WIN32)
			std::filesystem::remove(temporary, errorCode);
			if (errorCode)
			{
				outError = "Terrain Save Copy As published but could not remove its temporary link.";
				return AshEngine::TerrainContainerResult::IoFailure;
			}
#endif
			temporaryGuard.Release();
			outError.clear();
			return AshEngine::TerrainContainerResult::Success;
		}

		struct TerrainFileDispatchState
		{
			std::shared_ptr<std::promise<TerrainFileJobResult>> promise{};
			std::thread::id caller_thread{};
			std::atomic<bool> enqueue_in_progress{ true };
			std::atomic<bool> started{ false };

			explicit TerrainFileDispatchState(
				std::shared_ptr<std::promise<TerrainFileJobResult>> inPromise)
				: promise(std::move(inPromise))
				, caller_thread(std::this_thread::get_id())
			{
			}

			~TerrainFileDispatchState()
			{
				if (!started.exchange(true, std::memory_order_acq_rel))
				{
					Resolve(false, "Terrain file worker command was rejected before execution.");
				}
			}

			void Resolve(bool succeeded, std::string error) noexcept
			{
				try
				{
					promise->set_value({ succeeded, std::move(error) });
				}
				catch (...)
				{
				}
			}
		};

		bool IsTerrainFileOperationInProgress(const TerrainFileOperationStatus status)
		{
			return status == TerrainFileOperationStatus::AwaitingPublication ||
				status == TerrainFileOperationStatus::Running;
		}

		bool HasParentTraversal(const std::filesystem::path& path)
		{
			return std::any_of(path.begin(), path.end(), [](const std::filesystem::path& part)
			{
				return part == "..";
			});
		}

		bool IsValidUnitFloat(const float value)
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}

		bool IsValidAuthoringConfig(const TerrainAuthoringConfig& refConfig)
		{
			const uint8_t modeValue = static_cast<uint8_t>(refConfig.mode);
			const uint8_t toolValue = static_cast<uint8_t>(refConfig.brush.tool);
			if (modeValue > static_cast<uint8_t>(TerrainEditorMode::Layers) ||
				toolValue > static_cast<uint8_t>(AshEngine::TerrainBrushTool::Erase) ||
				!std::isfinite(refConfig.brush.radius_meters) ||
				refConfig.brush.radius_meters <= 0.0f ||
				refConfig.brush.radius_meters > 2048.0f ||
				!IsValidUnitFloat(refConfig.brush.strength) ||
				!IsValidUnitFloat(refConfig.brush.falloff) ||
				!std::isfinite(refConfig.brush.stroke_spacing_meters) ||
				refConfig.brush.stroke_spacing_meters <= 0.0f ||
				refConfig.brush.material_layer_index >= AshEngine::k_terrain_material_layer_count)
			{
				return false;
			}

			const bool heightTool =
				refConfig.brush.tool >= AshEngine::TerrainBrushTool::Raise &&
				refConfig.brush.tool <= AshEngine::TerrainBrushTool::Noise;
			const bool weightTool =
				refConfig.brush.tool == AshEngine::TerrainBrushTool::Paint ||
				refConfig.brush.tool == AshEngine::TerrainBrushTool::Erase;
			return (refConfig.mode != TerrainEditorMode::Sculpt || heightTool) &&
				(refConfig.mode != TerrainEditorMode::Paint || weightTool);
		}

		bool BrushParametersMatch(
			const AshEngine::TerrainBrushParameters& refLeft,
			const AshEngine::TerrainBrushParameters& refRight)
		{
			return refLeft.tool == refRight.tool &&
				refLeft.radius_meters == refRight.radius_meters &&
				refLeft.strength == refRight.strength &&
				refLeft.falloff == refRight.falloff &&
				refLeft.stroke_spacing_meters == refRight.stroke_spacing_meters &&
				refLeft.layer_id == refRight.layer_id &&
				refLeft.material_layer_index == refRight.material_layer_index &&
				refLeft.random_seed == refRight.random_seed;
		}

		bool IsToolCompatibleWithLayer(
			const TerrainEditorMode eMode,
			const AshEngine::TerrainBrushTool eTool,
			const AshEngine::TerrainHeightBlendMode eBlendMode)
		{
			if (eMode == TerrainEditorMode::Paint)
			{
				return eTool == AshEngine::TerrainBrushTool::Paint ||
					eTool == AshEngine::TerrainBrushTool::Erase;
			}
			if (eMode != TerrainEditorMode::Sculpt)
			{
				return true;
			}
			if (eBlendMode == AshEngine::TerrainHeightBlendMode::Additive)
			{
				return eTool == AshEngine::TerrainBrushTool::Raise ||
					eTool == AshEngine::TerrainBrushTool::Lower ||
					eTool == AshEngine::TerrainBrushTool::Noise;
			}
			return eTool == AshEngine::TerrainBrushTool::Smooth ||
				eTool == AshEngine::TerrainBrushTool::Flatten;
		}
	}

	bool TerrainEditorService::Initialize(IEditorCommandExecutor& refCommands)
	{
		Shutdown();
		_pCommands = &refCommands;
		return true;
	}

	bool TerrainEditorService::Initialize(
		AshEngine::AssetDatabase& refAssets,
		IEditorCommandExecutor& refCommands)
	{
		Initialize(refCommands);
		_pAssets = &refAssets;
		return true;
	}

	void TerrainEditorService::Shutdown()
	{
		if (_fileOperationState.status == TerrainFileOperationStatus::Running &&
			_pendingFileOperation.valid())
		{
			_pendingFileOperation.wait();
			CompletePendingFileOperation();
		}
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		_fileOperationState = {};
		_publishedSnapshot.reset();
		_nextStrokeSequence = 0u;
		_nextLayerSequence = 0u;
		_nextCompositionSerial = 0u;
		_latestCompositionSourceSequence = 0u;
		_nextFileOperationSerial = 0u;
		_historyRollbackFailed = false;
		_strLastError.clear();
		_authoringConfig = {};
		_core.Close();
		_pCommands = nullptr;
		_pAssets = nullptr;
	}

	void TerrainEditorService::Update()
	{
		if (_pendingLoad.valid() &&
			_pendingLoad.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			CompletePendingLoad();
		}

		if (_optPendingComposition)
		{
			CompletePendingComposition();
		}

		if (_fileOperationState.status == TerrainFileOperationStatus::AwaitingPublication)
		{
			TryStartFileOperation();
		}
		if (_fileOperationState.status == TerrainFileOperationStatus::Running &&
			_pendingFileOperation.valid() &&
			_pendingFileOperation.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			CompletePendingFileOperation();
		}
	}

	bool TerrainEditorService::SubmitIntent(const TerrainEditorIntent& refIntent)
	{
		if (!_pCommands)
		{
			_strLastError = "Terrain editor service is not initialized.";
			return false;
		}
		if (_historyRollbackFailed && refIntent.kind != TerrainEditorIntent::Kind::SelectAsset)
		{
			_strLastError =
				"Terrain authoring is quarantined because command-history rollback failed; reload or select another asset.";
			return false;
		}
		if (_fileOperationState.status == TerrainFileOperationStatus::AwaitingPublication &&
			refIntent.kind != TerrainEditorIntent::Kind::Save &&
			refIntent.kind != TerrainEditorIntent::Kind::SaveAs &&
			refIntent.kind != TerrainEditorIntent::Kind::Optimize)
		{
			_strLastError =
				"Terrain authoring is briefly paused until the requested save generation is published.";
			return false;
		}

		switch (refIntent.kind)
		{
		case TerrainEditorIntent::Kind::SelectAsset:
			return SubmitSelectAssetIntent(refIntent);
		case TerrainEditorIntent::Kind::SelectLayer:
			return SubmitSelectLayerIntent(refIntent);
		case TerrainEditorIntent::Kind::ConfigureAuthoring:
			return SubmitConfigureAuthoringIntent(refIntent);
		case TerrainEditorIntent::Kind::BeginStroke:
			return BeginStroke(refIntent);
		case TerrainEditorIntent::Kind::AddStrokeSample:
			return AddStrokeSample(refIntent);
		case TerrainEditorIntent::Kind::EndStroke:
			return EndStroke(refIntent);
		case TerrainEditorIntent::Kind::CancelStroke:
			return CancelStroke(refIntent);
		case TerrainEditorIntent::Kind::LayerAction:
			return SubmitLayerAction(refIntent);
		case TerrainEditorIntent::Kind::Save:
		case TerrainEditorIntent::Kind::SaveAs:
		case TerrainEditorIntent::Kind::Optimize:
			return SubmitFileOperation(refIntent);
		default:
			return false;
		}
	}

	bool TerrainEditorService::OpenSnapshotForAuthoring(
		const AshEngine::TerrainAssetSnapshot& refSnapshot)
	{
		if (HasFileOperationInProgress())
		{
			_strLastError = "Terrain authoring cannot replace an asset while a file operation is running.";
			return false;
		}
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> initialSnapshot{};
		try
		{
			initialSnapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(refSnapshot);
		}
		catch (const std::bad_alloc&)
		{
			_strLastError = "Terrain immutable preview snapshot allocation failed.";
			return false;
		}

		AshEngine::TerrainWorkingSet workingSet{};
		std::string strError{};
		if (!AshEngine::make_terrain_working_set(refSnapshot, workingSet, &strError) ||
			!_core.Open(std::move(workingSet)))
		{
			_strLastError = strError.empty()
				? "Terrain snapshot is invalid for authoring."
				: std::move(strError);
			return false;
		}
		SyncAuthoringLayerSelection();

		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		_fileOperationState = {};
		_publishedSnapshot = std::move(initialSnapshot);
		_historyRollbackFailed = false;
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::ApplyStrokePatches(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches,
		const AshEngine::TerrainEditPatchDirection eDirection,
		const uint64_t sequence)
	{
		if (_historyRollbackFailed)
		{
			_strLastError =
				"Terrain command replay is disabled because the authoring session is quarantined.";
			return false;
		}
		if (_optActiveStroke || _core.HasActiveStroke())
		{
			_strLastError = "Terrain command replay is disabled while a stroke is active.";
			return false;
		}
		if (sequence == 0u)
		{
			_strLastError = "Terrain stroke patch replay sequence is invalid.";
			return false;
		}

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

		ScheduleComposition(sequence, std::move(dirtyComponents));
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::ApplyLayerStackPatch(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerStackPatch& refPatch,
		const AshEngine::TerrainEditPatchDirection eDirection,
		const AshEngine::TerrainLayerId selectedLayerId,
		const uint64_t sequence)
	{
		if (_historyRollbackFailed)
		{
			_strLastError =
				"Terrain layer command replay is disabled because the authoring session is quarantined.";
			return false;
		}
		if (_optActiveStroke || _core.HasActiveStroke())
		{
			_strLastError = "Terrain layer command replay is disabled while a stroke is active.";
			return false;
		}
		if (sequence == 0u)
		{
			_strLastError = "Terrain layer patch replay sequence is invalid.";
			return false;
		}

		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyLayerStackPatch(
				assetId,
				refPatch,
				eDirection,
				selectedLayerId,
				dirtyComponents,
				&strError))
		{
			_strLastError = strError.empty()
				? "Terrain layer patch replay failed." : std::move(strError);
			return false;
		}

		SyncAuthoringLayerSelection();
		ScheduleComposition(sequence, std::move(dirtyComponents));
		_strLastError.clear();
		return true;
	}

	const TerrainEditorPreviewState& TerrainEditorService::GetPreviewState() const
	{
		return _core.GetPreviewState();
	}

	bool TerrainEditorService::SetViewportPreview(
		const AshEngine::TerrainAssetId assetId,
		const TerrainViewportPreviewState& refPreview)
	{
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		const bool authoringMode =
			_authoringConfig.mode == TerrainEditorMode::Sculpt ||
			_authoringConfig.mode == TerrainEditorMode::Paint;
		if (!pWorkingSet || assetId == 0u || assetId != pWorkingSet->asset_id ||
			!authoringMode)
		{
			return false;
		}

		TerrainViewportPreviewState candidate = refPreview;
		candidate.radius_meters = _authoringConfig.brush.radius_meters;
		return _core.SetViewportPreview(candidate);
	}

	void TerrainEditorService::ClearViewportPreview()
	{
		_core.ClearViewportPreview();
	}

	const TerrainAuthoringConfig& TerrainEditorService::GetAuthoringConfig() const
	{
		return _authoringConfig;
	}

	AshEngine::TerrainAssetId TerrainEditorService::GetSelectedAssetId() const
	{
		return _core.GetAssetId();
	}

	AshEngine::TerrainLayerId TerrainEditorService::GetSelectedLayerId() const
	{
		return _core.GetSelectedLayerId();
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorService::GetWorkingSet() const
	{
		return _core.GetWorkingSet();
	}

	TerrainAssetReferenceMatch TerrainEditorService::ClassifyCurrentAssetReferences(
		const std::vector<std::filesystem::path>& refReferences) const
	{
		if (refReferences.empty())
		{
			return TerrainAssetReferenceMatch::Different;
		}

		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!_pAssets || !_pAssets->is_valid() || !pWorkingSet)
		{
			return TerrainAssetReferenceMatch::Unsafe;
		}

		std::filesystem::path pathWorkingSetRelative{};
		const std::filesystem::path& pathAssetRoot = _pAssets->get_root_path();
		if (!TryNormalizeAssetPathWithinRoot(
				pathAssetRoot,
				pWorkingSet->source_path,
				pathWorkingSetRelative))
		{
			return TerrainAssetReferenceMatch::Unsafe;
		}

		bool sawUnsafeReference = false;
		for (const std::filesystem::path& pathReference : refReferences)
		{
			std::filesystem::path pathReferenceRelative{};
			if (!TryNormalizeAssetPathWithinRoot(
					pathAssetRoot,
					pathReference,
					pathReferenceRelative))
			{
				sawUnsafeReference = true;
				continue;
			}
			if (pathReferenceRelative == pathWorkingSetRelative)
			{
				return TerrainAssetReferenceMatch::Current;
			}
		}

		return sawUnsafeReference
			? TerrainAssetReferenceMatch::Unsafe
			: TerrainAssetReferenceMatch::Different;
	}

	const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>&
		TerrainEditorService::GetPublishedSnapshot() const
	{
		return _publishedSnapshot;
	}

	bool TerrainEditorService::HasDirtyAssets() const
	{
		return _core.IsDirty();
	}

	bool TerrainEditorService::HasPendingComposition() const
	{
		return _optPendingComposition.has_value();
	}

	bool TerrainEditorService::HasBlockingOperation() const
	{
		return _pendingLoad.valid() || _optPendingComposition.has_value() ||
			_fileOperationState.status == TerrainFileOperationStatus::AwaitingPublication;
	}

	bool TerrainEditorService::HasFileOperationInProgress() const
	{
		return IsTerrainFileOperationInProgress(_fileOperationState.status);
	}

	const TerrainFileOperationState& TerrainEditorService::GetFileOperationState() const
	{
		return _fileOperationState;
	}

	const std::string& TerrainEditorService::GetLastError() const
	{
		return _strLastError;
	}

	bool TerrainEditorService::SubmitSelectAssetIntent(const TerrainEditorIntent& refIntent)
	{
		const AshEngine::TerrainAssetId previousAssetId = _core.GetAssetId();
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		const bool alreadyOpen = pWorkingSet && pWorkingSet->asset_id == refIntent.asset_id;
		const bool changesAsset = previousAssetId != refIntent.asset_id;
		if (changesAsset && HasFileOperationInProgress())
		{
			_strLastError =
				"Terrain asset selection cannot replace a session while a file operation is in progress.";
			return false;
		}
		if (changesAsset && !_historyRollbackFailed &&
			(_core.IsDirty() || _optActiveStroke || _core.HasActiveStroke() ||
			_optPendingComposition))
		{
			_strLastError = _core.IsDirty()
				? "Terrain asset selection cannot replace a dirty authoring session."
				: "Terrain asset selection cannot replace an active authoring operation.";
			return false;
		}
		if (refIntent.asset_id != 0u && !alreadyOpen && !_pAssets)
		{
			_strLastError = "Terrain asset selection requires an AssetDatabase.";
			return false;
		}
		if (!_core.Reduce(refIntent))
		{
			return false;
		}

		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_strLastError.clear();
		if (previousAssetId != refIntent.asset_id)
		{
			_optActiveStroke.reset();
			_optPendingComposition.reset();
			_publishedSnapshot.reset();
			_historyRollbackFailed = false;
			SyncAuthoringLayerSelection();
		}
		if (refIntent.asset_id == 0u || alreadyOpen)
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

	bool TerrainEditorService::SubmitSelectLayerIntent(const TerrainEditorIntent& refIntent)
	{
		if (_optActiveStroke || _core.HasActiveStroke() ||
			!_core.SelectLayer(refIntent.layer_id))
		{
			_strLastError =
				"Terrain layer selection requires an idle session and an existing stable layer ID.";
			return false;
		}

		SyncAuthoringLayerSelection();
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::SubmitConfigureAuthoringIntent(const TerrainEditorIntent& refIntent)
	{
		if (_optActiveStroke || _core.HasActiveStroke())
		{
			_strLastError = "Terrain authoring configuration cannot change during an active stroke.";
			return false;
		}

		TerrainAuthoringConfig candidate{};
		candidate.mode = refIntent.mode;
		candidate.brush = refIntent.brush;
		candidate.brush.layer_id = _core.GetSelectedLayerId();
		if (!IsValidAuthoringConfig(candidate))
		{
			_strLastError = "Terrain authoring mode or brush configuration is invalid.";
			return false;
		}
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (pWorkingSet && candidate.brush.layer_id.is_valid())
		{
			const auto layer = std::find_if(
				pWorkingSet->edit_layers.begin(),
				pWorkingSet->edit_layers.end(),
				[&candidate](const AshEngine::TerrainEditLayer& refLayer)
				{
					return refLayer.id == candidate.brush.layer_id;
				});
			if (layer == pWorkingSet->edit_layers.end() ||
				!IsToolCompatibleWithLayer(candidate.mode, candidate.brush.tool, layer->height_blend_mode))
			{
				_strLastError = "Terrain authoring tool is incompatible with the selected layer.";
				return false;
			}
		}

		_authoringConfig = std::move(candidate);
		const bool authoringMode =
			_authoringConfig.mode == TerrainEditorMode::Sculpt ||
			_authoringConfig.mode == TerrainEditorMode::Paint;
		if (!authoringMode)
		{
			_core.ClearViewportPreview();
		}
		else if (_core.GetPreviewState().viewport.query_status !=
			AshEngine::TerrainQueryStatus::Outside)
		{
			TerrainViewportPreviewState viewport = _core.GetPreviewState().viewport;
			viewport.radius_meters = _authoringConfig.brush.radius_meters;
			if (!_core.SetViewportPreview(viewport))
			{
				_core.ClearViewportPreview();
			}
		}
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::BeginStroke(const TerrainEditorIntent& refIntent)
	{
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		const AshEngine::TerrainLayerId selectedLayerId = _core.GetSelectedLayerId();
		const bool validMetric =
			std::isfinite(refIntent.brush_metric.world_meters_per_terrain_meter.x) &&
			refIntent.brush_metric.world_meters_per_terrain_meter.x > 0.0f &&
			std::isfinite(refIntent.brush_metric.world_meters_per_terrain_meter.y) &&
			refIntent.brush_metric.world_meters_per_terrain_meter.y > 0.0f;
		const bool authoringMode =
			_authoringConfig.mode == TerrainEditorMode::Sculpt ||
			_authoringConfig.mode == TerrainEditorMode::Paint;
		if (!pWorkingSet || _optActiveStroke || _core.HasActiveStroke() ||
			_core.GetPreviewState().query_status != AshEngine::TerrainQueryStatus::Ready ||
			!authoringMode || !validMetric || !selectedLayerId.is_valid() ||
			refIntent.asset_id != pWorkingSet->asset_id ||
			refIntent.layer_id != selectedLayerId ||
			_authoringConfig.brush.layer_id != selectedLayerId ||
			!BrushParametersMatch(refIntent.brush, _authoringConfig.brush))
		{
			_strLastError = "Terrain stroke begin state, asset, mode, metric, layer, or authoring configuration is invalid.";
			return false;
		}
		if (pWorkingSet->content_generation >= std::numeric_limits<uint64_t>::max() - 1u)
		{
			_strLastError = "Terrain content generation cannot reserve a rollback generation.";
			return false;
		}

		const auto layer = std::find_if(
			pWorkingSet->edit_layers.begin(),
			pWorkingSet->edit_layers.end(),
			[selectedLayerId](const AshEngine::TerrainEditLayer& refLayer)
			{
				return refLayer.id == selectedLayerId;
			});
		if (layer == pWorkingSet->edit_layers.end())
		{
			_strLastError = "Terrain stroke layer does not exist.";
			return false;
		}
		if (layer->locked)
		{
			_strLastError = "Terrain stroke layer is locked.";
			return false;
		}

		if (!IsToolCompatibleWithLayer(
				_authoringConfig.mode,
				_authoringConfig.brush.tool,
				layer->height_blend_mode))
		{
			_strLastError = "Terrain brush tool is incompatible with the selected layer.";
			return false;
		}

		++_nextStrokeSequence;
		if (_nextStrokeSequence == 0u)
		{
			++_nextStrokeSequence;
		}
		_optActiveStroke.emplace();
		_optActiveStroke->asset_id = pWorkingSet->asset_id;
		_optActiveStroke->layer_id = selectedLayerId;
		_optActiveStroke->sequence = _nextStrokeSequence;
		_optActiveStroke->parameters = _authoringConfig.brush;
		_optActiveStroke->metric = refIntent.brush_metric;
		if (!_core.BeginStroke(_nextStrokeSequence))
		{
			_optActiveStroke.reset();
			_strLastError = "Terrain session rejected stroke activation.";
			return false;
		}

		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::AddStrokeSample(const TerrainEditorIntent& refIntent)
	{
		if (!_optActiveStroke || !_core.HasActiveStroke() ||
			(refIntent.sequence != 0u && refIntent.sequence != _optActiveStroke->sequence))
		{
			_strLastError = "Terrain stroke sample does not match the active sequence.";
			return false;
		}

		try
		{
			_optActiveStroke->raw_samples.push_back(refIntent.stroke_sample);
		}
		catch (const std::bad_alloc&)
		{
			_strLastError = "Terrain stroke sample allocation failed.";
			return false;
		}
		catch (const std::length_error&)
		{
			_strLastError = "Terrain stroke sample count is unsupported.";
			return false;
		}

		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::EndStroke(const TerrainEditorIntent& refIntent)
	{
		if (!_optActiveStroke || !_core.HasActiveStroke() ||
			(refIntent.sequence != 0u && refIntent.sequence != _optActiveStroke->sequence))
		{
			_strLastError = "Terrain stroke end does not match the active sequence.";
			return false;
		}

		ActiveStroke stroke = std::move(*_optActiveStroke);
		_optActiveStroke.reset();
		if (stroke.raw_samples.empty())
		{
			if (!_core.EndStroke(stroke.sequence))
			{
				_strLastError = "Terrain session rejected empty stroke completion.";
				return false;
			}
			_strLastError.clear();
			return true;
		}

		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyBrushStroke(
				stroke.sequence,
				stroke.parameters,
				stroke.metric,
				stroke.raw_samples,
				patches,
				dirtyComponents,
				&strError))
		{
			_strLastError = strError.empty() ? "Terrain brush transaction failed." : std::move(strError);
			return false;
		}
		if (patches.empty())
		{
			_strLastError.clear();
			return true;
		}

		const AshEngine::TerrainWorkingSet* pForwardWorkingSet = _core.GetWorkingSet();
		if (!pForwardWorkingSet)
		{
			_historyRollbackFailed = true;
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = "Terrain stroke mutation lost its authoring working set.";
			return false;
		}
		const uint64_t forwardGeneration = pForwardWorkingSet->content_generation;
		ScheduleComposition(stroke.sequence, std::move(dirtyComponents));
		const auto rollbackIsComplete = [this, &stroke, forwardGeneration]()
		{
			const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
			return pWorkingSet &&
				pWorkingSet->asset_id == stroke.asset_id &&
				pWorkingSet->content_generation == forwardGeneration + 1u &&
				_optPendingComposition &&
				_optPendingComposition->asset_id == stroke.asset_id &&
				_optPendingComposition->source_sequence == stroke.sequence &&
				_optPendingComposition->content_generation == pWorkingSet->content_generation &&
				_optPendingComposition->dirty_components == pWorkingSet->dirty_components;
		};
		const auto quarantineMutation = [this](const char* pError)
		{
			_optPendingComposition.reset();
			_historyRollbackFailed = true;
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = pError;
		};
		std::unique_ptr<TerrainStrokeCommand> upCommand{};
		try
		{
			std::vector<AshEngine::TerrainEditPatch> commandPatches = patches;
			upCommand = std::make_unique<TerrainStrokeCommand>(
				stroke.asset_id,
				stroke.layer_id,
				stroke.sequence,
				std::move(commandPatches));
		}
		catch (const std::bad_alloc&)
		{
			const bool rolledBack = RollBackStroke(stroke, patches);
			if (!rolledBack || !rollbackIsComplete())
			{
				quarantineMutation("Terrain stroke command allocation and rollback failed; authoring was quarantined.");
			}
			else
			{
				_strLastError = "Terrain stroke command allocation failed; the mutation was rolled back.";
			}
			return false;
		}
		catch (const std::length_error&)
		{
			const bool rolledBack = RollBackStroke(stroke, patches);
			if (!rolledBack || !rollbackIsComplete())
			{
				quarantineMutation("Terrain stroke command size and rollback failed; authoring was quarantined.");
			}
			else
			{
				_strLastError = "Terrain stroke command size is unsupported; the mutation was rolled back.";
			}
			return false;
		}

		EditorCommandRecordResult recordResult = EditorCommandRecordResult::RollbackFailed;
		try
		{
			recordResult = _pCommands->RecordExecutedCommand(std::move(upCommand));
		}
		catch (...)
		{
			quarantineMutation("Terrain stroke history recording raised an exception; authoring was quarantined.");
			return false;
		}

		if (recordResult == EditorCommandRecordResult::RolledBack && rollbackIsComplete())
		{
			_strLastError = "Terrain stroke history recording failed; the command contract rolled back the mutation.";
			return false;
		}
		if (recordResult != EditorCommandRecordResult::Recorded)
		{
			quarantineMutation(
				"Terrain stroke history recording could not prove rollback; authoring was quarantined.");
			return false;
		}

		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::CancelStroke(const TerrainEditorIntent& refIntent)
	{
		if (!_optActiveStroke || !_core.HasActiveStroke() ||
			(refIntent.sequence != 0u && refIntent.sequence != _optActiveStroke->sequence))
		{
			_strLastError = "Terrain stroke cancel does not match the active sequence.";
			return false;
		}

		_optActiveStroke.reset();
		_core.CancelStroke();
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::SubmitLayerAction(const TerrainEditorIntent& refIntent)
	{
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || _optActiveStroke || _core.HasActiveStroke() ||
			_core.GetPreviewState().query_status != AshEngine::TerrainQueryStatus::Ready)
		{
			_strLastError = "Terrain layer action requires an idle ready authoring session.";
			return false;
		}
		if (pWorkingSet->content_generation >= std::numeric_limits<uint64_t>::max() - 1u)
		{
			_strLastError = "Terrain content generation cannot reserve a layer rollback generation.";
			return false;
		}
		const AshEngine::TerrainLayerId selectedBefore = _core.GetSelectedLayerId();
		const bool selectedBeforeLocked = _core.GetPreviewState().layer_locked;

		AshEngine::TerrainLayerStackEdit edit{};
		edit.layer_id = refIntent.layer_action.layer_id;
		edit.name = refIntent.layer_action.name;
		edit.destination_index = refIntent.layer_action.destination_index;
		edit.opacity = refIntent.layer_action.opacity;
		edit.flag_value = refIntent.layer_action.flag_value;
		edit.blend_mode = refIntent.layer_action.blend_mode;
		switch (refIntent.layer_action.kind)
		{
		case TerrainLayerActionKind::Add:
			edit.kind = AshEngine::TerrainLayerStackEditKind::Add;
			break;
		case TerrainLayerActionKind::Delete:
			edit.kind = AshEngine::TerrainLayerStackEditKind::Delete;
			break;
		case TerrainLayerActionKind::Duplicate:
			edit.kind = AshEngine::TerrainLayerStackEditKind::Duplicate;
			break;
		case TerrainLayerActionKind::Rename:
			edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
			break;
		case TerrainLayerActionKind::Move:
			edit.kind = AshEngine::TerrainLayerStackEditKind::Move;
			break;
		case TerrainLayerActionKind::SetVisible:
			edit.kind = AshEngine::TerrainLayerStackEditKind::SetVisible;
			break;
		case TerrainLayerActionKind::SetLocked:
			edit.kind = AshEngine::TerrainLayerStackEditKind::SetLocked;
			break;
		case TerrainLayerActionKind::SetOpacity:
			edit.kind = AshEngine::TerrainLayerStackEditKind::SetOpacity;
			break;
		default:
			_strLastError = "Terrain layer action kind is invalid.";
			return false;
		}

		AshEngine::TerrainLayerStackPatch patch{};
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyLayerStackEdit(edit, patch, dirtyComponents, &strError))
		{
			_strLastError = strError.empty()
				? "Terrain layer transaction failed." : std::move(strError);
			return false;
		}
		if (!patch.has_change())
		{
			_strLastError.clear();
			return true;
		}
		const AshEngine::TerrainLayerId selectedAfter = _core.GetSelectedLayerId();
		SyncAuthoringLayerSelection();

		++_nextLayerSequence;
		if (_nextLayerSequence == 0u)
		{
			++_nextLayerSequence;
		}
		const uint64_t sequence = _nextLayerSequence;
		const AshEngine::TerrainAssetId assetId = patch.asset_id;
		pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || pWorkingSet->asset_id != assetId)
		{
			_historyRollbackFailed = true;
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = "Terrain layer mutation lost its authoring working set.";
			return false;
		}
		const uint64_t forwardGeneration = pWorkingSet->content_generation;
		ScheduleComposition(sequence, std::move(dirtyComponents));

		const auto rollbackIsComplete = [
			this,
			assetId,
			sequence,
			forwardGeneration,
			selectedBefore,
			selectedBeforeLocked]()
		{
			const AshEngine::TerrainWorkingSet* pCurrentWorkingSet = _core.GetWorkingSet();
			const TerrainEditorPreviewState& preview = _core.GetPreviewState();
			return pCurrentWorkingSet && pCurrentWorkingSet->asset_id == assetId &&
				pCurrentWorkingSet->content_generation == forwardGeneration + 1u &&
				_core.GetSelectedLayerId() == selectedBefore &&
				preview.layer_locked == selectedBeforeLocked &&
				preview.query_status == AshEngine::TerrainQueryStatus::Ready &&
				_optPendingComposition && _optPendingComposition->asset_id == assetId &&
				_optPendingComposition->source_sequence == sequence &&
				_optPendingComposition->content_generation == pCurrentWorkingSet->content_generation &&
				_optPendingComposition->dirty_components == pCurrentWorkingSet->dirty_components;
		};
		const auto quarantineMutation = [this](const char* pError)
		{
			_optPendingComposition.reset();
			_historyRollbackFailed = true;
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = pError;
		};

		std::unique_ptr<TerrainLayerCommand> upCommand{};
		try
		{
			AshEngine::TerrainLayerStackPatch commandPatch = patch;
			upCommand = std::make_unique<TerrainLayerCommand>(
				assetId,
				sequence,
				std::move(commandPatch),
				selectedBefore,
				selectedAfter);
		}
		catch (const std::bad_alloc&)
		{
			const bool rolledBack = RollBackLayerAction(
				assetId, patch, selectedBefore, sequence);
			if (!rolledBack || !rollbackIsComplete())
			{
				quarantineMutation(
					"Terrain layer command allocation and rollback failed; authoring was quarantined.");
			}
			else
			{
				_strLastError = "Terrain layer command allocation failed; the mutation was rolled back.";
			}
			return false;
		}
		catch (const std::length_error&)
		{
			const bool rolledBack = RollBackLayerAction(
				assetId, patch, selectedBefore, sequence);
			if (!rolledBack || !rollbackIsComplete())
			{
				quarantineMutation(
					"Terrain layer command size and rollback failed; authoring was quarantined.");
			}
			else
			{
				_strLastError = "Terrain layer command size is unsupported; the mutation was rolled back.";
			}
			return false;
		}

		EditorCommandRecordResult recordResult = EditorCommandRecordResult::RollbackFailed;
		try
		{
			recordResult = _pCommands->RecordExecutedCommand(std::move(upCommand));
		}
		catch (...)
		{
			quarantineMutation(
				"Terrain layer history recording raised an exception; authoring was quarantined.");
			return false;
		}
		if (recordResult == EditorCommandRecordResult::RolledBack && rollbackIsComplete())
		{
			_strLastError =
				"Terrain layer history recording failed; the command contract rolled back the mutation.";
			return false;
		}
		if (recordResult != EditorCommandRecordResult::Recorded)
		{
			quarantineMutation(
				"Terrain layer history recording could not prove rollback; authoring was quarantined.");
			return false;
		}

		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::ResolveFileOperationPath(
		const TerrainEditorIntent& refIntent,
		std::filesystem::path& outPath,
		std::string& outError) const
	{
		outPath.clear();
		outError.clear();
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet)
		{
			outError = "Terrain file operation requires an open authoring session.";
			return false;
		}

		std::filesystem::path requested = refIntent.asset_path;
		if (requested.empty())
		{
			requested = pWorkingSet->source_path;
		}
		if (requested.empty())
		{
			outError = "Terrain file operation requires an asset path.";
			return false;
		}

		if (!_pAssets || !_pAssets->is_valid() || _pAssets->get_root_path().empty())
		{
			outError = "Terrain file operation requires a configured asset root.";
			return false;
		}
		const std::filesystem::path root = _pAssets->get_root_path();
		if (requested.is_relative())
		{
			requested = root / requested;
		}

		std::error_code errorCode{};
		std::filesystem::path absolute = std::filesystem::absolute(requested, errorCode);
		if (errorCode)
		{
			outError = "Terrain asset path could not be resolved.";
			return false;
		}
		absolute = absolute.lexically_normal();
		{
			std::filesystem::path absoluteRoot = std::filesystem::absolute(root, errorCode);
			if (errorCode)
			{
				outError = "Terrain asset root could not be resolved.";
				return false;
			}
			absoluteRoot = absoluteRoot.lexically_normal();
			const std::filesystem::path relative =
				std::filesystem::relative(absolute, absoluteRoot, errorCode);
			if (errorCode || relative.empty() || relative.is_absolute() || HasParentTraversal(relative))
			{
				outError = "Terrain asset path must stay inside the configured asset root.";
				return false;
			}
		}

		std::string extension = absolute.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
		if (extension != ".ashterrain")
		{
			outError = "Terrain asset path must use the .AshTerrain extension.";
			return false;
		}
		outPath = std::move(absolute);
		return true;
	}

	bool TerrainEditorService::SubmitFileOperation(const TerrainEditorIntent& refIntent)
	{
		if (HasFileOperationInProgress())
		{
			_strLastError = "A Terrain file operation is already in progress.";
			return false;
		}
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || !_publishedSnapshot)
		{
			_strLastError = "Terrain file operation requires a published authoring snapshot.";
			return false;
		}
		if (_optActiveStroke || _core.HasActiveStroke())
		{
			_strLastError = "Terrain file operation is unavailable while a stroke is active.";
			return false;
		}

		TerrainFileOperationKind kind = TerrainFileOperationKind::None;
		switch (refIntent.kind)
		{
		case TerrainEditorIntent::Kind::Save:
			kind = TerrainFileOperationKind::Save;
			if (!_core.IsDirty())
			{
				_strLastError = "Terrain asset has no unsaved changes.";
				return false;
			}
			break;
		case TerrainEditorIntent::Kind::SaveAs:
			kind = TerrainFileOperationKind::SaveAs;
			if (refIntent.asset_path.empty())
			{
				_strLastError = "Terrain Save As requires a destination path.";
				return false;
			}
			break;
		case TerrainEditorIntent::Kind::Optimize:
			kind = TerrainFileOperationKind::Optimize;
			if (_core.IsDirty())
			{
				_strLastError = "Terrain Optimize requires a clean, saved asset; save dirty changes first.";
				return false;
			}
			if (_optPendingComposition)
			{
				_strLastError = "Terrain Optimize cannot start while composition is pending.";
				return false;
			}
			break;
		default:
			return false;
		}
		if (kind != TerrainFileOperationKind::SaveAs && !refIntent.asset_path.empty())
		{
			_strLastError =
				"Terrain Save and Optimize always target the current Terrain; use Save Copy As for a new path.";
			return false;
		}

		std::filesystem::path path{};
		std::string error{};
		if (!ResolveFileOperationPath(refIntent, path, error))
		{
			_strLastError = std::move(error);
			return false;
		}
		if (kind == TerrainFileOperationKind::SaveAs)
		{
			std::error_code existsError{};
			if (std::filesystem::exists(path, existsError) || existsError)
			{
				_strLastError = existsError
					? "Terrain Save As destination could not be inspected."
					: "Terrain Save As writes a new copy and will not overwrite an existing asset.";
				return false;
			}
		}

		++_nextFileOperationSerial;
		if (_nextFileOperationSerial == 0u)
		{
			++_nextFileOperationSerial;
		}
		_fileOperationState = {};
		_fileOperationState.kind = kind;
		_fileOperationState.status = TerrainFileOperationStatus::AwaitingPublication;
		_fileOperationState.operation_serial = _nextFileOperationSerial;
		_fileOperationState.asset_id = pWorkingSet->asset_id;
		_fileOperationState.content_generation = pWorkingSet->content_generation;
		_fileOperationState.path = std::move(path);
		_strLastError.clear();

		if (!_optPendingComposition)
		{
			TryStartFileOperation();
		}
		return _fileOperationState.status != TerrainFileOperationStatus::Failed;
	}

	void TerrainEditorService::FailFileOperation(std::string error)
	{
		if (error.empty())
		{
			error = "Terrain file operation failed.";
		}
		_fileOperationState.status = TerrainFileOperationStatus::Failed;
		_fileOperationState.error = error;
		_strLastError = std::move(error);
	}

	bool TerrainEditorService::TryStartFileOperation()
	{
		if (_fileOperationState.status != TerrainFileOperationStatus::AwaitingPublication)
		{
			return false;
		}
		if (_optPendingComposition || _optActiveStroke || _core.HasActiveStroke())
		{
			return true;
		}

		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || pWorkingSet->asset_id != _fileOperationState.asset_id ||
			pWorkingSet->content_generation != _fileOperationState.content_generation)
		{
			FailFileOperation("Terrain save generation changed before its immutable snapshot was captured.");
			return false;
		}
		if (!_publishedSnapshot || _publishedSnapshot->asset_id != pWorkingSet->asset_id ||
			_publishedSnapshot->content_generation != pWorkingSet->content_generation ||
			!pWorkingSet->dirty_components.empty())
		{
			FailFileOperation(_strLastError.empty()
				? "Terrain save could not capture the requested published generation."
				: _strLastError);
			return false;
		}

		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::vector<AshEngine::TerrainDirtyComponentPayload> dirtyPayloads{};
		if (_fileOperationState.kind != TerrainFileOperationKind::Optimize)
		{
			snapshot = _publishedSnapshot;
			if (_fileOperationState.kind == TerrainFileOperationKind::Save)
			{
				const uint64_t savingGeneration = _core.BeginSaveContentGeneration();
				if (savingGeneration == 0u || savingGeneration != _fileOperationState.content_generation)
				{
					FailFileOperation("Terrain save could not capture a dirty content generation.");
					return false;
				}
				std::error_code existsError{};
				const bool exists = std::filesystem::exists(_fileOperationState.path, existsError);
				if (existsError)
				{
					FailFileOperation("Terrain save destination could not be inspected.");
					return false;
				}
				if (exists)
				{
					const uint64_t persistedGeneration = _core.GetPersistedContentGeneration();
					for (const auto& component : snapshot->components)
					{
						if (component && component->content_generation > persistedGeneration)
						{
							dirtyPayloads.push_back({
								component->coord,
								component->content_generation,
								component
							});
						}
					}
				}
			}
		}

		try
		{
			auto promise = std::make_shared<std::promise<TerrainFileJobResult>>();
			_pendingFileOperation = promise->get_future().share();
			auto dispatchState = std::make_shared<TerrainFileDispatchState>(std::move(promise));
			const TerrainFileOperationKind kind = _fileOperationState.kind;
			const std::filesystem::path path = _fileOperationState.path;
			_fileOperationState.status = TerrainFileOperationStatus::Running;
			try
			{
				const auto dispatchFuture = AshEngine::dispatch_background_task(
					"TerrainEditorService::file_operation",
					[dispatchState,
					 kind,
					 path,
					 snapshot = std::move(snapshot),
					 dirtyPayloads = std::move(dirtyPayloads)]() mutable
					{
						const bool inlineFallback =
							std::this_thread::get_id() == dispatchState->caller_thread &&
							dispatchState->enqueue_in_progress.load(std::memory_order_acquire);
						if (dispatchState->started.exchange(true, std::memory_order_acq_rel))
						{
							return;
						}
						if (inlineFallback)
						{
							dispatchState->Resolve(
								false,
								"Terrain async file operation requires an available worker thread.");
							return;
						}

						try
						{
							std::string error{};
							AshEngine::TerrainContainerResult result =
								AshEngine::TerrainContainerResult::InvalidData;
							if (kind == TerrainFileOperationKind::Optimize)
							{
								result = AshEngine::optimize_terrain_container(path, nullptr, &error);
							}
							else if (kind == TerrainFileOperationKind::SaveAs)
							{
								result = SaveTerrainCopyNew(path, *snapshot, error);
							}
							else
							{
								result = AshEngine::save_terrain_container_incremental(
									path, *snapshot, dirtyPayloads, nullptr, &error);
							}
							const bool succeeded =
								result == AshEngine::TerrainContainerResult::Success;
							if (!succeeded && error.empty())
							{
								error = "Terrain container operation failed.";
							}
							dispatchState->Resolve(succeeded, std::move(error));
						}
						catch (const std::exception& exception)
						{
							dispatchState->Resolve(false, exception.what());
						}
						catch (...)
						{
							dispatchState->Resolve(
								false,
								"Terrain file worker raised an unknown exception.");
						}
					});
				(void)dispatchFuture;
				dispatchState->enqueue_in_progress.store(false, std::memory_order_release);
			}
			catch (...)
			{
				dispatchState->enqueue_in_progress.store(false, std::memory_order_release);
				throw;
			}
		}
		catch (const std::exception& exception)
		{
			_pendingFileOperation = {};
			FailFileOperation(exception.what());
			return false;
		}
		catch (...)
		{
			_pendingFileOperation = {};
			FailFileOperation("Terrain file operation dispatch failed.");
			return false;
		}
		return true;
	}

	void TerrainEditorService::CompletePendingFileOperation()
	{
		if (_fileOperationState.status != TerrainFileOperationStatus::Running ||
			!_pendingFileOperation.valid())
		{
			return;
		}

		TerrainFileJobResult result{};
		try
		{
			result = _pendingFileOperation.get();
		}
		catch (const std::exception& exception)
		{
			result = { false, exception.what() };
		}
		catch (...)
		{
			result = { false, "Terrain file worker raised an unknown exception." };
		}
		_pendingFileOperation = {};

		if (!result.first)
		{
			if (_fileOperationState.kind == TerrainFileOperationKind::Save)
			{
				_core.CompleteSaveContentGeneration(
					_fileOperationState.content_generation, false);
			}
			FailFileOperation(std::move(result.second));
			return;
		}
		if (_fileOperationState.kind == TerrainFileOperationKind::Save &&
			(_core.GetAssetId() != _fileOperationState.asset_id ||
			 !_core.CompleteSaveContentGeneration(
				 _fileOperationState.content_generation, true)))
		{
			FailFileOperation(
				"Terrain file was written, but the captured generation no longer belongs to this session.");
			return;
		}

		_fileOperationState.status = TerrainFileOperationStatus::Succeeded;
		_fileOperationState.error.clear();
		_strLastError.clear();
	}

	void TerrainEditorService::ScheduleComposition(
		const uint64_t sourceSequence,
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents)
	{
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (sourceSequence == 0u || !pWorkingSet ||
			dirtyComponents != pWorkingSet->dirty_components)
		{
			_optPendingComposition.reset();
			return;
		}

		++_nextCompositionSerial;
		if (_nextCompositionSerial == 0u)
		{
			++_nextCompositionSerial;
		}
		_optPendingComposition.emplace();
		_optPendingComposition->asset_id = pWorkingSet->asset_id;
		_optPendingComposition->source_sequence = sourceSequence;
		_optPendingComposition->operation_serial = _nextCompositionSerial;
		_optPendingComposition->content_generation = pWorkingSet->content_generation;
		_optPendingComposition->dirty_components = std::move(dirtyComponents);
		_latestCompositionSourceSequence = sourceSequence;
	}

	bool TerrainEditorService::RollBackStroke(
		const ActiveStroke& refStroke,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches)
	{
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyStrokePatches(
				refStroke.asset_id,
				refStroke.layer_id,
				refPatches,
				AshEngine::TerrainEditPatchDirection::Undo,
				dirtyComponents,
				&strError))
		{
			return false;
		}

		ScheduleComposition(refStroke.sequence, std::move(dirtyComponents));
		return true;
	}

	bool TerrainEditorService::RollBackLayerAction(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerStackPatch& refPatch,
		const AshEngine::TerrainLayerId selectedLayerId,
		const uint64_t sequence)
	{
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents{};
		std::string strError{};
		if (!_core.ApplyLayerStackPatch(
				assetId,
				refPatch,
				AshEngine::TerrainEditPatchDirection::Undo,
				selectedLayerId,
				dirtyComponents,
				&strError))
		{
			return false;
		}

		SyncAuthoringLayerSelection();
		ScheduleComposition(sequence, std::move(dirtyComponents));
		return true;
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

	void TerrainEditorService::CompletePendingComposition()
	{
		PendingComposition pending = std::move(*_optPendingComposition);
		_optPendingComposition.reset();
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || pending.asset_id != _core.GetAssetId() ||
			pending.source_sequence != _latestCompositionSourceSequence ||
			pending.operation_serial != _nextCompositionSerial ||
			pending.content_generation != pWorkingSet->content_generation ||
			pending.dirty_components != pWorkingSet->dirty_components)
		{
			return;
		}

		std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
		std::string strError{};
		if (!_core.ComposeComponents(pending.dirty_components, payloads, &strError))
		{
			_strLastError = strError.empty() ? "Terrain dirty-component composition failed." : std::move(strError);
			return;
		}

		pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet || pending.asset_id != _core.GetAssetId() ||
			pending.source_sequence != _latestCompositionSourceSequence ||
			pending.operation_serial != _nextCompositionSerial ||
			pending.content_generation != pWorkingSet->content_generation ||
			pending.dirty_components != pWorkingSet->dirty_components)
		{
			return;
		}

		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		try
		{
			const TerrainEditorSessionCore::TerrainSnapshotPublisher publisher =
				[this](
					const AshEngine::TerrainAssetId assetId,
					const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot)
				{
					return !_pAssets || _pAssets->publish_terrain_snapshot(assetId, refSnapshot);
				};
			if (!_core.PublishDirtyComponents(payloads, publisher, snapshot, &strError))
			{
				_strLastError = strError.empty() ? "Terrain snapshot publication failed." : std::move(strError);
				return;
			}
		}
		catch (const std::bad_alloc&)
		{
			_strLastError = "Terrain publication callback allocation failed.";
			return;
		}

		_publishedSnapshot = std::move(snapshot);
		_strLastError.clear();
	}

	void TerrainEditorService::SyncAuthoringLayerSelection()
	{
		_authoringConfig.brush.layer_id = _core.GetSelectedLayerId();
		if (_authoringConfig.mode == TerrainEditorMode::Paint)
		{
			if (_authoringConfig.brush.tool != AshEngine::TerrainBrushTool::Paint &&
				_authoringConfig.brush.tool != AshEngine::TerrainBrushTool::Erase)
			{
				_authoringConfig.brush.tool = AshEngine::TerrainBrushTool::Paint;
			}
			return;
		}
		if (_authoringConfig.mode != TerrainEditorMode::Sculpt)
		{
			return;
		}

		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!pWorkingSet)
		{
			_authoringConfig.brush.tool = AshEngine::TerrainBrushTool::Raise;
			return;
		}
		const auto layer = std::find_if(
			pWorkingSet->edit_layers.begin(),
			pWorkingSet->edit_layers.end(),
			[this](const AshEngine::TerrainEditLayer& refLayer)
			{
				return refLayer.id == _authoringConfig.brush.layer_id;
			});
		if (layer != pWorkingSet->edit_layers.end() &&
			!IsToolCompatibleWithLayer(
				_authoringConfig.mode,
				_authoringConfig.brush.tool,
				layer->height_blend_mode))
		{
			_authoringConfig.brush.tool =
				layer->height_blend_mode == AshEngine::TerrainHeightBlendMode::Additive
				? AshEngine::TerrainBrushTool::Raise
				: AshEngine::TerrainBrushTool::Smooth;
		}
	}
}
