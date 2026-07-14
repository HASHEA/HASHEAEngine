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
#include <system_error>
#include <thread>
#include <type_traits>
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

		std::atomic<uint64_t> g_nextTerrainFileTemporarySerial{ 0u };

		std::filesystem::path MakeTerrainFileTemporaryPath(
			const std::filesystem::path& destination,
			const char* purpose)
		{
			const uint64_t serial =
				g_nextTerrainFileTemporarySerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
#if defined(_WIN32)
			const uint64_t processId = static_cast<uint64_t>(GetCurrentProcessId());
#else
			const uint64_t processId = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
#endif
			std::filesystem::path temporary = destination;
			temporary += "." + std::string(purpose) + "." + std::to_string(processId) + "." +
				std::to_string(serial) + ".tmp";
			return temporary;
		}

		std::filesystem::path MakeTerrainCopyTemporaryPath(
			const std::filesystem::path& destination)
		{
			return MakeTerrainFileTemporaryPath(destination, "save-copy");
		}

		bool HeightFileExtensionMatches(
			const std::filesystem::path& path,
			const AshEngine::TerrainHeightFileFormat format)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
			switch (format)
			{
			case AshEngine::TerrainHeightFileFormat::RawR16:
			case AshEngine::TerrainHeightFileFormat::RawR32F:
				return extension == ".raw";
			case AshEngine::TerrainHeightFileFormat::Png:
				return extension == ".png";
			case AshEngine::TerrainHeightFileFormat::Exr:
				return extension == ".exr";
			default:
				return false;
			}
		}

		bool IsValidTerrainHeightMapping(
			const AshEngine::TerrainHeightMapping& mapping)
		{
			return std::isfinite(mapping.height_offset) &&
				std::isfinite(mapping.height_range) && mapping.height_range > 0.0f &&
				std::isfinite(mapping.height_offset + mapping.height_range);
		}

		bool ResolveTerrainImportSource(
			const std::filesystem::path& assetRoot,
			const std::filesystem::path& requested,
			std::filesystem::path& outPath,
			std::string& outError)
		{
			outPath.clear();
			if (assetRoot.empty() || requested.empty())
			{
				outError = "Terrain Import requires a source file.";
				return false;
			}
			std::error_code errorCode{};
			const std::filesystem::path candidate = requested.is_absolute()
				? requested
				: assetRoot / requested;
			const std::filesystem::path canonical =
				std::filesystem::weakly_canonical(candidate, errorCode);
			if (errorCode || canonical.empty() ||
				!std::filesystem::is_regular_file(canonical, errorCode) || errorCode)
			{
				outError = "Terrain Import source must resolve to an existing readable file.";
				return false;
			}
			outPath = canonical;
			return true;
		}

		bool PublishStagedFileNew(
			const std::filesystem::path& destination,
			const std::filesystem::path& staged,
			std::string& outError)
		{
#if defined(_WIN32)
			HANDLE stagedFile = CreateFileW(
				staged.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (stagedFile == INVALID_HANDLE_VALUE)
			{
				outError = "Terrain Export staged file could not be opened for durable publish.";
				return false;
			}
			const bool flushed = FlushFileBuffers(stagedFile) != FALSE;
			CloseHandle(stagedFile);
			if (!flushed)
			{
				outError = "Terrain Export staged file could not be flushed before publish.";
				return false;
			}
			if (MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE)
			{
				const DWORD error = GetLastError();
				outError = error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS
					? "Terrain Export destination already exists and will not be overwritten."
					: "Terrain Export staged file could not be atomically published.";
				return false;
			}
#else
			std::error_code errorCode{};
			std::filesystem::create_hard_link(staged, destination, errorCode);
			if (errorCode)
			{
				outError = "Terrain Export staged file could not be published without replacement.";
				return false;
			}
			std::filesystem::remove(staged, errorCode);
#endif
			outError.clear();
			return true;
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

			const AshEngine::TerrainContainerResult publishResult =
				AshEngine::publish_staged_terrain_container_new(
					destination, temporary, &outError);
			if (publishResult != AshEngine::TerrainContainerResult::Success)
			{
				return publishResult;
			}
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

			void Resolve(TerrainFileJobResult result) noexcept
			{
				try
				{
					promise->set_value(std::move(result));
				}
				catch (...)
				{
				}
			}

			void Resolve(bool succeeded, std::string error) noexcept
			{
				Resolve({ succeeded, std::move(error), {} });
			}
		};

		bool IsTerrainFileOperationInProgress(const TerrainFileOperationStatus status)
		{
			return status == TerrainFileOperationStatus::AwaitingPublication ||
				status == TerrainFileOperationStatus::Running ||
				status == TerrainFileOperationStatus::PublishedAwaitingCatalog;
		}

		uint64_t GetEffectiveTerrainDiskGeneration(
			const AshEngine::TerrainAssetSnapshot& snapshot) noexcept
		{
			return snapshot.recovered_previous_generation &&
				snapshot.rejected_content_generation > snapshot.content_generation
				? snapshot.rejected_content_generation
				: snapshot.content_generation;
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

#if defined(ASH_TESTS)
	void TerrainEditorService::SetFileJobTestHook(FileJobTestHook hook)
	{
		_fileJobTestHook = std::move(hook);
	}
#endif

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
			if (_pendingFileOperationPhase)
			{
				CancellableFileJobPhase expected = CancellableFileJobPhase::Cancellable;
				_pendingFileOperationPhase->compare_exchange_strong(
					expected,
					CancellableFileJobPhase::Cancelled,
					std::memory_order_acq_rel,
					std::memory_order_acquire);
			}
			if (_pendingImportDesc)
			{
				_pendingImportDesc->cancellation.cancel();
			}
			if (_pendingExportDesc)
			{
				_pendingExportDesc->cancellation.cancel();
			}
			_pendingFileOperation.wait();
			CompletePendingFileOperation();
		}
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		_pendingLoadRollback.reset();
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		_pendingFileOperationPhase.reset();
		_fileOperationState = {};
		_pendingPublishedBindOperationSerial = 0u;
		_pendingPublishedBindAssetId = 0u;
		_pendingCreateDesc.reset();
		_pendingImportDesc.reset();
		_pendingExportDesc.reset();
		_externalChangeState = {};
		_publishedSnapshot.reset();
		_nextStrokeSequence = 0u;
		_nextLayerSequence = 0u;
		_nextCompositionSerial = 0u;
		_latestCompositionSourceSequence = 0u;
		_nextFileOperationSerial = 0u;
		_nextExternalStateSerial = 0u;
		_nextExternalCompositionSequence = 0u;
		_observedSourceWriteTime = {};
		_observedSourceRevision.reset();
		_nextExternalPollTime = {};
		_nextPublishedCatalogRetryTime = {};
		_sourceWriteTimeErrorCount = 0u;
		_hasObservedSourceWriteTime = false;
		_conflictSaveAsPending = false;
		_historyRollbackFailed = false;
		_strLastError.clear();
#if defined(ASH_TESTS)
		_fileJobTestHook = {};
#endif
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
		if (_fileOperationState.status == TerrainFileOperationStatus::PublishedAwaitingCatalog &&
			std::chrono::steady_clock::now() >= _nextPublishedCatalogRetryTime)
		{
			TryBindPublishedFileOperation();
		}

		PollExternalModification();
	}

	bool TerrainEditorService::SubmitIntent(const TerrainEditorIntent& refIntent)
	{
		if (!_pCommands)
		{
			_strLastError = "Terrain editor service is not initialized.";
			return false;
		}
		if (refIntent.kind == TerrainEditorIntent::Kind::CancelFileOperation)
		{
			return CancelFileOperation();
		}
		if (_historyRollbackFailed &&
			refIntent.kind != TerrainEditorIntent::Kind::SelectAsset &&
			refIntent.kind != TerrainEditorIntent::Kind::Reload &&
			refIntent.kind != TerrainEditorIntent::Kind::Repair)
		{
			_strLastError =
				"Terrain authoring is quarantined because command-history rollback failed; reload or select another asset.";
			return false;
		}
		const bool conflictIntent =
			refIntent.kind == TerrainEditorIntent::Kind::Reload ||
			refIntent.kind == TerrainEditorIntent::Kind::KeepLocal ||
			refIntent.kind == TerrainEditorIntent::Kind::SaveAs;
		const bool recoveryIntent =
			refIntent.kind == TerrainEditorIntent::Kind::Repair ||
			refIntent.kind == TerrainEditorIntent::Kind::SaveAs;
		if (_externalChangeState.status == TerrainExternalChangeStatus::Conflict &&
			!conflictIntent && refIntent.kind != TerrainEditorIntent::Kind::SelectAsset &&
			refIntent.kind != TerrainEditorIntent::Kind::CancelStroke)
		{
			_strLastError =
				"Terrain authoring is paused until the external modification conflict is resolved.";
			return false;
		}
		if ((_externalChangeState.status == TerrainExternalChangeStatus::RecoveredReadOnly ||
			 _externalChangeState.status == TerrainExternalChangeStatus::Failed) &&
			!recoveryIntent && refIntent.kind != TerrainEditorIntent::Kind::SelectAsset)
		{
			_strLastError =
				"Terrain authoring is read only until the source asset is repaired or copied.";
			return false;
		}
		if (_externalChangeState.status == TerrainExternalChangeStatus::Reloading &&
			refIntent.kind != TerrainEditorIntent::Kind::SelectAsset &&
			refIntent.kind != TerrainEditorIntent::Kind::CancelStroke)
		{
			_strLastError = "Terrain authoring is paused while the source asset reloads.";
			return false;
		}
		const bool replacingSession =
			_fileOperationState.kind == TerrainFileOperationKind::Create ||
			_fileOperationState.kind == TerrainFileOperationKind::Import;
		if (replacingSession && HasFileOperationInProgress())
		{
			_strLastError = _fileOperationState.status ==
				TerrainFileOperationStatus::PublishedAwaitingCatalog
				? "Terrain asset was published and is waiting to bind through the asset catalog."
				: "Terrain authoring is frozen while a new Terrain asset is created or imported.";
			return false;
		}
		if (_fileOperationState.status == TerrainFileOperationStatus::AwaitingPublication &&
			refIntent.kind != TerrainEditorIntent::Kind::Save &&
			refIntent.kind != TerrainEditorIntent::Kind::SaveAs &&
			refIntent.kind != TerrainEditorIntent::Kind::Optimize &&
			refIntent.kind != TerrainEditorIntent::Kind::Create &&
			refIntent.kind != TerrainEditorIntent::Kind::Import &&
			refIntent.kind != TerrainEditorIntent::Kind::Export)
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
		case TerrainEditorIntent::Kind::Reload:
			return SubmitReloadIntent(false);
		case TerrainEditorIntent::Kind::Repair:
			return SubmitReloadIntent(true);
		case TerrainEditorIntent::Kind::KeepLocal:
			return ResolveKeepLocalConflict();
		case TerrainEditorIntent::Kind::Save:
		case TerrainEditorIntent::Kind::SaveAs:
		case TerrainEditorIntent::Kind::Optimize:
		case TerrainEditorIntent::Kind::Create:
		case TerrainEditorIntent::Kind::Import:
		case TerrainEditorIntent::Kind::Export:
		{
			const bool conflictSaveAs =
				refIntent.kind == TerrainEditorIntent::Kind::SaveAs &&
				_externalChangeState.status == TerrainExternalChangeStatus::Conflict;
			if (conflictSaveAs && !_core.ResolveConflict(TerrainConflictChoice::SaveAs))
			{
				_strLastError = "Terrain Save As no longer matches an active conflict.";
				return false;
			}
			if (!SubmitFileOperation(refIntent))
			{
				return false;
			}
			_conflictSaveAsPending = conflictSaveAs;
			return true;
		}
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
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		_pendingLoadRollback.reset();
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		_fileOperationState = {};
		_pendingPublishedBindOperationSerial = 0u;
		_pendingPublishedBindAssetId = 0u;
		_pendingCreateDesc.reset();
		_pendingImportDesc.reset();
		_pendingExportDesc.reset();
		_publishedSnapshot = std::move(initialSnapshot);
		_historyRollbackFailed = false;
		_conflictSaveAsPending = false;
		CaptureObservedSourceWriteTime();
		if (refSnapshot.recovered_previous_generation)
		{
			const std::string diagnostic =
				"Terrain container recovered generation " +
				std::to_string(refSnapshot.content_generation) +
				" after rejecting generation " +
				std::to_string(refSnapshot.rejected_content_generation) +
				(refSnapshot.recovery_detail.empty()
					? "." : ": " + refSnapshot.recovery_detail);
			SetExternalChangeState(
				TerrainExternalChangeStatus::RecoveredReadOnly,
				refSnapshot.content_generation,
				GetEffectiveTerrainDiskGeneration(refSnapshot),
				diagnostic,
				true,
				_pAssets != nullptr && _pAssets->is_valid() && refSnapshot.asset_id != 0u,
				_pAssets != nullptr && _pAssets->is_valid());
			_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
			_strLastError = diagnostic;
		}
		else
		{
			ResetExternalChangeState();
			_strLastError.clear();
		}
		return true;
	}

	bool TerrainEditorService::ApplyStrokePatches(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches,
		const AshEngine::TerrainEditPatchDirection eDirection,
		const uint64_t sequence)
	{
		if (_externalChangeState.status != TerrainExternalChangeStatus::None)
		{
			_strLastError = "Terrain command replay is disabled while reload recovery is active.";
			return false;
		}
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
		if (_externalChangeState.status != TerrainExternalChangeStatus::None)
		{
			_strLastError = "Terrain layer replay is disabled while reload recovery is active.";
			return false;
		}
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
		return _historyRollbackFailed || _pendingLoad.valid() ||
			_optPendingComposition.has_value() ||
			HasFileOperationInProgress() ||
			_externalChangeState.status == TerrainExternalChangeStatus::Reloading ||
			_externalChangeState.status == TerrainExternalChangeStatus::Conflict;
	}

	bool TerrainEditorService::HasFileOperationInProgress() const
	{
		return IsTerrainFileOperationInProgress(_fileOperationState.status);
	}

	const TerrainFileOperationState& TerrainEditorService::GetFileOperationState() const
	{
		return _fileOperationState;
	}

	const TerrainExternalChangeState& TerrainEditorService::GetExternalChangeState() const
	{
		return _externalChangeState;
	}

	const std::string& TerrainEditorService::GetLastError() const
	{
		return _strLastError;
	}

	void TerrainEditorService::SetExternalChangeState(
		const TerrainExternalChangeStatus status,
		const uint64_t localGeneration,
		const uint64_t diskGeneration,
		std::string diagnostic,
		const bool readOnly,
		const bool canRepair,
		const bool canSaveAs)
	{
		++_nextExternalStateSerial;
		if (_nextExternalStateSerial == 0u)
		{
			++_nextExternalStateSerial;
		}
		_externalChangeState.status = status;
		_externalChangeState.serial = _nextExternalStateSerial;
		_externalChangeState.local_generation = localGeneration;
		_externalChangeState.disk_generation = diskGeneration;
		_externalChangeState.diagnostic = std::move(diagnostic);
		_externalChangeState.read_only = readOnly;
		_externalChangeState.can_repair = canRepair;
		_externalChangeState.can_save_as = canSaveAs;
	}

	void TerrainEditorService::ResetExternalChangeState()
	{
		SetExternalChangeState(
			TerrainExternalChangeStatus::None,
			_core.GetContentGeneration(),
			_core.GetPersistedContentGeneration(),
			{},
			false,
			false,
			false);
	}

	void TerrainEditorService::EnterExternalFailure(std::string error)
	{
		if (error.empty())
		{
			error = "Terrain asset reload failed.";
		}
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_pendingLoadRollback.reset();
		_conflictSaveAsPending = false;
		_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
		SetExternalChangeState(
			TerrainExternalChangeStatus::Failed,
			_core.GetContentGeneration(),
			0u,
			error,
			true,
			_pAssets != nullptr && _pAssets->is_valid() && _core.GetAssetId() != 0u,
			_pAssets != nullptr && _pAssets->is_valid() && _publishedSnapshot != nullptr);
		_strLastError = std::move(error);
	}

	void TerrainEditorService::FailPendingSelection(std::string error)
	{
		if (!_pendingLoadRollback)
		{
			EnterExternalFailure(std::move(error));
			return;
		}
		if (error.empty())
		{
			error = "Terrain asset selection failed.";
		}

		PendingLoadRollbackState rollback = std::move(*_pendingLoadRollback);
		_pendingLoadRollback.reset();
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		_externalChangeState = std::move(rollback.external_state);
		_core.SetPreviewQueryStatus(rollback.query_status);
		_externalCandidate = std::move(rollback.external_candidate);
		_externalCandidateSourceWriteTime = rollback.candidate_source_write_time;
		_externalCandidateExpectedPublication =
			std::move(rollback.candidate_expected_publication);
		_conflictSaveAsPending = rollback.conflict_save_as_pending;
		_strLastError = std::move(error);
	}

	bool TerrainEditorService::BeginReload(const PendingLoadReason reason)
	{
		const AshEngine::TerrainAssetId assetId = _core.GetAssetId();
		if (!_pAssets || !_pAssets->is_valid() || assetId == 0u)
		{
			_strLastError = "Terrain reload requires a selected AssetDatabase Terrain asset.";
			return false;
		}
		if (_pendingLoad.valid() || HasFileOperationInProgress() || _optPendingComposition ||
			_optActiveStroke || _core.HasActiveStroke())
		{
			_strLastError =
				"Terrain reload requires an idle session with no load, composition, stroke, or file operation.";
			return false;
		}
		if (!CapturePendingLoadSourceWriteTime(assetId))
		{
			_strLastError =
				"Terrain reload could not capture a stable source write time; retry after the file is available.";
			return false;
		}
		_pendingLoadExpectedPublication =
			_pAssets->capture_terrain_snapshot_publication(assetId);

		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> load =
			_pAssets->load_terrain_candidate_by_id_async(assetId);
		if (!load.valid())
		{
			_pendingLoadSourceWriteTime.reset();
			_pendingLoadExpectedPublication.reset();
			_strLastError = "AssetDatabase did not return a Terrain reload future.";
			return false;
		}

		PendingLoadRollbackState rollback{};
		rollback.previous_asset_id = assetId;
		rollback.external_state = _externalChangeState;
		rollback.query_status = _core.GetPreviewState().query_status;
		rollback.external_candidate = _externalCandidate;
		rollback.candidate_source_write_time = _externalCandidateSourceWriteTime;
		rollback.candidate_expected_publication = _externalCandidateExpectedPublication;
		rollback.conflict_save_as_pending = _conflictSaveAsPending;
		rollback.last_error = _strLastError;
		_pendingLoadRollback = std::move(rollback);
		_pendingLoad = std::move(load);
		_pendingLoadAssetId = assetId;
		_pendingLoadReason = reason;
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		SetExternalChangeState(
			TerrainExternalChangeStatus::Reloading,
			_core.GetContentGeneration(),
			0u,
			reason == PendingLoadReason::Repair
				? "Repairing Terrain asset from disk."
				: "Reloading Terrain asset from disk.",
			true,
			false,
			_publishedSnapshot != nullptr);
		_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Pending);
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::SubmitReloadIntent(const bool repair)
	{
		if (HasFileOperationInProgress())
		{
			_strLastError =
				"Terrain reload cannot replace a session while a file operation is in progress.";
			return false;
		}
		if (!repair && _externalCandidate && _externalCandidateExpectedPublication)
		{
			const AshEngine::TerrainSnapshotPublicationToken current =
				_pAssets->capture_terrain_snapshot_publication(_externalCandidate->asset_id);
			const AshEngine::TerrainSnapshotPublicationToken& expected =
				*_externalCandidateExpectedPublication;
			if (current.asset_id == expected.asset_id &&
				current.catalog_generation == expected.catalog_generation &&
				current.load_serial == expected.load_serial &&
				current.snapshot == expected.snapshot)
			{
				return ApplyExternalCandidate(_core.GetAssetId());
			}

			// A catalog refresh or another publisher invalidates the validated
			// candidate. Re-read from the fresh lineage instead of applying stale bytes.
			_externalCandidate.reset();
			_externalCandidateSourceWriteTime.reset();
			_externalCandidateExpectedPublication.reset();
		}
		if (repair &&
			_externalChangeState.status != TerrainExternalChangeStatus::RecoveredReadOnly &&
			_externalChangeState.status != TerrainExternalChangeStatus::Failed)
		{
			_strLastError = "Terrain Repair requires a recovered or failed source asset.";
			return false;
		}
		const PendingLoadReason reason = repair
			? PendingLoadReason::Repair
			: (_externalChangeState.status == TerrainExternalChangeStatus::Conflict
				? PendingLoadReason::ConflictReload
				: PendingLoadReason::Reload);
		return BeginReload(reason);
	}

	bool TerrainEditorService::ResolveKeepLocalConflict()
	{
		if (_externalChangeState.status != TerrainExternalChangeStatus::Conflict ||
			!_externalCandidate)
		{
			_strLastError = "Terrain Keep Local could not safely advance above the disk generation.";
			return false;
		}
		if (HasFileOperationInProgress())
		{
			_strLastError =
				"Terrain Keep Local cannot run while a Terrain file operation is in progress.";
			return false;
		}

		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		std::vector<AshEngine::TerrainComponentCoord> allComponents{};
		try
		{
			if (!pWorkingSet)
			{
				_strLastError = "Terrain Keep Local requires an open authoring working set.";
				return false;
			}
			const size_t componentCount =
				static_cast<size_t>(pWorkingSet->layout.component_count_x) *
				pWorkingSet->layout.component_count_z;
			allComponents.reserve(componentCount);
			for (uint32_t z = 0u; z < pWorkingSet->layout.component_count_z; ++z)
			{
				for (uint32_t x = 0u; x < pWorkingSet->layout.component_count_x; ++x)
				{
					allComponents.push_back({
						static_cast<uint16_t>(x),
						static_cast<uint16_t>(z) });
				}
			}
		}
		catch (const std::bad_alloc&)
		{
			_strLastError = "Terrain Keep Local could not allocate the full recomposition set.";
			return false;
		}
		catch (const std::length_error&)
		{
			_strLastError = "Terrain Keep Local recomposition set is too large.";
			return false;
		}

		if (!_core.ResolveConflict(TerrainConflictChoice::KeepLocal))
		{
			_strLastError = "Terrain Keep Local could not safely advance above the disk generation.";
			return false;
		}

		const std::optional<std::filesystem::file_time_type> candidateSourceWriteTime =
			_externalCandidateSourceWriteTime;
		const std::optional<AshEngine::TerrainContainerRevision> candidateSourceRevision =
			_externalCandidate && _externalCandidate->source_revision.is_valid()
			? std::optional<AshEngine::TerrainContainerRevision>{ _externalCandidate->source_revision }
			: std::nullopt;
		++_nextExternalCompositionSequence;
		if (_nextExternalCompositionSequence == 0u)
		{
			++_nextExternalCompositionSequence;
		}
		ScheduleComposition(
			_nextExternalCompositionSequence,
			std::move(allComponents));
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_conflictSaveAsPending = false;
		ObserveLoadedSourceWriteTime(candidateSourceWriteTime, candidateSourceRevision);
		ResetExternalChangeState();
		_strLastError.clear();
		return true;
	}

	bool TerrainEditorService::ApplyExternalCandidate(
		const AshEngine::TerrainAssetId historyAssetId)
	{
		if (!_externalCandidate ||
			_externalCandidate->asset_id == 0u ||
			!_externalCandidateExpectedPublication || !_pAssets ||
			_externalCandidateExpectedPublication->asset_id != _externalCandidate->asset_id)
		{
			_strLastError = "Terrain conflict has no validated disk candidate to apply.";
			return false;
		}

		const AshEngine::TerrainAssetId assetId = _externalCandidate->asset_id;
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> candidate = _externalCandidate;
		const bool preservePublishedFileOperation =
			_fileOperationState.status == TerrainFileOperationStatus::PublishedAwaitingCatalog &&
			(_fileOperationState.kind == TerrainFileOperationKind::Create ||
			 _fileOperationState.kind == TerrainFileOperationKind::Import) &&
			_pendingPublishedBindOperationSerial == _fileOperationState.operation_serial &&
			_pendingPublishedBindAssetId == assetId && _fileOperationState.asset_id == assetId;
		const std::optional<std::filesystem::file_time_type> candidateSourceWriteTime =
			_externalCandidateSourceWriteTime;

		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> preparedPublished = candidate;
		AshEngine::TerrainWorkingSet preparedWorkingSet{};
		TerrainEditorSessionCore preparedCore{};
		std::string preparedDiagnostic{};
		std::string preparedLastError{};
		try
		{
			if (!AshEngine::make_terrain_working_set(
					*candidate, preparedWorkingSet, &_strLastError) ||
				!preparedCore.Open(std::move(preparedWorkingSet)))
			{
				if (_strLastError.empty())
				{
					_strLastError = "Terrain disk candidate is invalid for authoring.";
				}
				return false;
			}
			if (candidate->recovered_previous_generation)
			{
				preparedDiagnostic =
					"Terrain container recovered generation " +
					std::to_string(candidate->content_generation) +
					" after rejecting generation " +
					std::to_string(candidate->rejected_content_generation) +
					(candidate->recovery_detail.empty()
						? "." : ": " + candidate->recovery_detail);
				preparedCore.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
				preparedLastError = preparedDiagnostic;
			}
		}
		catch (const std::exception& exception)
		{
			_strLastError = exception.what();
			return false;
		}
		catch (...)
		{
			_strLastError = "Terrain disk candidate preparation failed.";
			return false;
		}

		AshEngine::TerrainSnapshotPublicationToken previousPublication =
			*_externalCandidateExpectedPublication;
		AshEngine::TerrainSnapshotPublicationToken acceptedPublication{};
		const bool exchanged = _pAssets->compare_exchange_terrain_snapshot(
			assetId, previousPublication, candidate, &acceptedPublication);
		if (!exchanged)
		{
			_strLastError = _pAssets->get_last_error();
			if (_strLastError.empty())
			{
				_strLastError =
					"Terrain disk candidate could not replace the published asset snapshot.";
			}
			return false;
		}
		if (historyAssetId != 0u && _pCommands &&
			!_pCommands->RemoveCommandsForTerrainAsset(historyAssetId))
		{
			AshEngine::TerrainSnapshotPublicationToken rolledBackPublication{};
			if (!_pAssets->compare_exchange_terrain_snapshot(
					assetId,
					acceptedPublication,
					previousPublication.snapshot,
					&rolledBackPublication))
			{
				_strLastError =
					"Terrain reload could not remove stale command history, and the "
					"published cache changed before rollback.";
				return false;
			}
			_externalCandidateExpectedPublication = std::move(rolledBackPublication);
			_strLastError = "Terrain reload could not remove stale command history.";
			return false;
		}

		static_assert(std::is_nothrow_move_assignable_v<TerrainEditorSessionCore>);
		_core = std::move(preparedCore);
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		_pendingLoadRollback.reset();
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		if (!preservePublishedFileOperation)
		{
			_fileOperationState = {};
			_pendingPublishedBindOperationSerial = 0u;
			_pendingPublishedBindAssetId = 0u;
			_pendingCreateDesc.reset();
			_pendingImportDesc.reset();
			_pendingExportDesc.reset();
		}
		_publishedSnapshot = std::move(preparedPublished);
		_historyRollbackFailed = false;
		_conflictSaveAsPending = false;
		SyncAuthoringLayerSelection();
		ObserveLoadedSourceWriteTime(
			candidateSourceWriteTime,
			candidate->source_revision.is_valid()
				? std::optional<AshEngine::TerrainContainerRevision>{ candidate->source_revision }
				: std::nullopt);
		if (candidate->recovered_previous_generation)
		{
			SetExternalChangeState(
				TerrainExternalChangeStatus::RecoveredReadOnly,
				candidate->content_generation,
				GetEffectiveTerrainDiskGeneration(*candidate),
				std::move(preparedDiagnostic),
				true,
				_pAssets->is_valid() && assetId != 0u,
				_pAssets->is_valid());
			_strLastError = std::move(preparedLastError);
		}
		else
		{
			ResetExternalChangeState();
			_strLastError.clear();
		}
		return true;
	}

	void TerrainEditorService::CaptureObservedSourceWriteTime()
	{
		_hasObservedSourceWriteTime = false;
		_observedSourceRevision.reset();
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!_pAssets || !_pAssets->is_valid() || !pWorkingSet || pWorkingSet->source_path.empty())
		{
			return;
		}
		const std::filesystem::path path = pWorkingSet->source_path.is_absolute()
			? pWorkingSet->source_path
			: _pAssets->get_root_path() / pWorkingSet->source_path;
		std::error_code errorCode{};
		const std::filesystem::file_time_type writeTime =
			std::filesystem::last_write_time(path, errorCode);
		if (!errorCode)
		{
			_observedSourceWriteTime = writeTime;
			_hasObservedSourceWriteTime = true;
			_sourceWriteTimeErrorCount = 0u;
		}
		if (_publishedSnapshot && _publishedSnapshot->source_revision.is_valid())
		{
			_observedSourceRevision = _publishedSnapshot->source_revision;
		}
		else
		{
			AshEngine::TerrainContainerRevision revision{};
			if (AshEngine::inspect_terrain_container_revision(path, revision, nullptr) ==
				AshEngine::TerrainContainerResult::Success && revision.is_valid())
			{
				_observedSourceRevision = revision;
			}
		}
		_nextExternalPollTime =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
	}

	bool TerrainEditorService::CapturePendingLoadSourceWriteTime(
		const AshEngine::TerrainAssetId assetId)
	{
		_pendingLoadSourceWriteTime.reset();
		if (!_pAssets || !_pAssets->is_valid() || assetId == 0u)
		{
			return false;
		}

		std::filesystem::path sourcePath{};
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (pWorkingSet && pWorkingSet->asset_id == assetId && !pWorkingSet->source_path.empty())
		{
			sourcePath = pWorkingSet->source_path;
		}
		else if (const AshEngine::AssetInfo* pAsset = _pAssets->find_asset_by_id(assetId))
		{
			sourcePath = pAsset->relative_path;
		}
		if (sourcePath.empty())
		{
			return false;
		}

		const std::filesystem::path path = sourcePath.is_absolute()
			? sourcePath
			: _pAssets->get_root_path() / sourcePath;
		std::error_code errorCode{};
		const std::filesystem::file_time_type writeTime =
			std::filesystem::last_write_time(path, errorCode);
		if (!errorCode)
		{
			_pendingLoadSourceWriteTime = writeTime;
			return true;
		}
		return false;
	}

	void TerrainEditorService::ObserveLoadedSourceWriteTime(
		const std::optional<std::filesystem::file_time_type>& sourceWriteTime,
		const std::optional<AshEngine::TerrainContainerRevision>& sourceRevision) noexcept
	{
		if (sourceRevision && sourceRevision->is_valid())
		{
			_observedSourceRevision = *sourceRevision;
		}
		else
		{
			_observedSourceRevision.reset();
		}
		if (!sourceWriteTime)
		{
			_hasObservedSourceWriteTime = false;
			_sourceWriteTimeErrorCount = 0u;
			_nextExternalPollTime = std::chrono::steady_clock::now();
			return;
		}
		_observedSourceWriteTime = *sourceWriteTime;
		_hasObservedSourceWriteTime = true;
		_sourceWriteTimeErrorCount = 0u;
		_nextExternalPollTime = std::chrono::steady_clock::now();
	}

	void TerrainEditorService::PollExternalModification()
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < _nextExternalPollTime ||
			_externalChangeState.status != TerrainExternalChangeStatus::None ||
			_pendingLoad.valid() || HasFileOperationInProgress() || _optPendingComposition ||
			_optActiveStroke || _core.HasActiveStroke())
		{
			return;
		}
		_nextExternalPollTime = now + std::chrono::milliseconds(500);
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (!_pAssets || !_pAssets->is_valid() || !pWorkingSet || pWorkingSet->source_path.empty())
		{
			return;
		}
		const std::filesystem::path path = pWorkingSet->source_path.is_absolute()
			? pWorkingSet->source_path
			: _pAssets->get_root_path() / pWorkingSet->source_path;
		AshEngine::TerrainContainerRevision revision{};
		std::string revisionError{};
		const AshEngine::TerrainContainerResult revisionResult =
			AshEngine::inspect_terrain_container_revision(path, revision, &revisionError);
		if (revisionResult == AshEngine::TerrainContainerResult::Busy)
		{
			// A compliant writer owns the named commit lease. Its in-progress append/header
			// transition is not a committed revision and must not consume the corrupt-
			// source debounce budget.
			return;
		}
		std::error_code errorCode{};
		const std::filesystem::file_time_type writeTime =
			std::filesystem::last_write_time(path, errorCode);
		if (revisionResult != AshEngine::TerrainContainerResult::Success ||
			!revision.is_valid() || errorCode)
		{
			++_sourceWriteTimeErrorCount;
			if (_sourceWriteTimeErrorCount >= 2u)
			{
				_sourceWriteTimeErrorCount = 0u;
				EnterExternalFailure(
					errorCode == std::errc::no_such_file_or_directory
						? "Terrain source asset is missing on disk."
						: "Terrain source asset metadata could not be inspected.");
			}
			return;
		}
		_sourceWriteTimeErrorCount = 0u;
		if (!_observedSourceRevision)
		{
			_observedSourceRevision = revision;
			_observedSourceWriteTime = writeTime;
			_hasObservedSourceWriteTime = true;
			return;
		}
		if (revision != *_observedSourceRevision)
		{
			BeginReload(PendingLoadReason::Reload);
			return;
		}
		_observedSourceWriteTime = writeTime;
		_hasObservedSourceWriteTime = true;
	}

	bool TerrainEditorService::PrepareForSceneChange()
	{
		if (_optActiveStroke || _core.HasActiveStroke())
		{
			_optActiveStroke.reset();
			_core.CancelStroke();
		}
		if (HasFileOperationInProgress() || _pendingLoad.valid() || _optPendingComposition ||
			_core.IsDirty() || _core.HasExternalConflict() || _historyRollbackFailed)
		{
			_strLastError =
				"Scene change is blocked until Terrain loading, saving, composition, conflict, or dirty state is resolved.";
			return false;
		}
		if (_externalChangeState.status == TerrainExternalChangeStatus::None)
		{
			_strLastError.clear();
		}
		else
		{
			_strLastError = _externalChangeState.diagnostic;
		}
		return true;
	}

	void TerrainEditorService::CommitSceneChange()
	{
		if (HasFileOperationInProgress())
		{
			return;
		}
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		_pendingLoadRollback.reset();
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_pendingFileOperation = {};
		_fileOperationState = {};
		_pendingPublishedBindOperationSerial = 0u;
		_pendingPublishedBindAssetId = 0u;
		_pendingCreateDesc.reset();
		_pendingImportDesc.reset();
		_pendingExportDesc.reset();
		_publishedSnapshot.reset();
		_conflictSaveAsPending = false;
		_historyRollbackFailed = false;
		_hasObservedSourceWriteTime = false;
		_observedSourceRevision.reset();
		_sourceWriteTimeErrorCount = 0u;
		_authoringConfig = {};
		_core.Close();
		ResetExternalChangeState();
		_strLastError.clear();
	}

	bool TerrainEditorService::SubmitSelectAssetIntent(const TerrainEditorIntent& refIntent)
	{
		const AshEngine::TerrainAssetId previousAssetId = _core.GetAssetId();
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		const bool alreadyOpen = pWorkingSet && pWorkingSet->asset_id == refIntent.asset_id;
		const bool alreadyLoading = _pendingLoad.valid() &&
			_pendingLoadAssetId == refIntent.asset_id;
		const bool changesAsset = previousAssetId != refIntent.asset_id;
		if (alreadyLoading)
		{
			return true;
		}
		if (_pendingLoad.valid())
		{
			if (_pendingLoadReason == PendingLoadReason::Selection &&
				refIntent.asset_id == previousAssetId && _pendingLoadRollback)
			{
				PendingLoadRollbackState rollback = std::move(*_pendingLoadRollback);
				_pendingLoadRollback.reset();
				_pendingLoad = {};
				_pendingLoadAssetId = 0u;
				_pendingLoadReason = PendingLoadReason::None;
				_pendingLoadSourceWriteTime.reset();
				_pendingLoadExpectedPublication.reset();
				_externalChangeState = std::move(rollback.external_state);
				_core.SetPreviewQueryStatus(rollback.query_status);
				_externalCandidate = std::move(rollback.external_candidate);
				_externalCandidateSourceWriteTime = rollback.candidate_source_write_time;
				_externalCandidateExpectedPublication =
					std::move(rollback.candidate_expected_publication);
				_conflictSaveAsPending = rollback.conflict_save_as_pending;
				_strLastError = std::move(rollback.last_error);
				return true;
			}
			_strLastError =
				"Terrain asset selection cannot replace another in-flight Terrain load.";
			return false;
		}
		if (alreadyOpen)
		{
			return true;
		}
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
		if (refIntent.asset_id != 0u && !_pAssets)
		{
			_strLastError = "Terrain asset selection requires an AssetDatabase.";
			return false;
		}
		if (refIntent.asset_id == 0u)
		{
			if (previousAssetId != 0u && _pCommands &&
				!_pCommands->RemoveCommandsForTerrainAsset(previousAssetId))
			{
				_strLastError =
					"Terrain asset selection could not remove commands for the closing session.";
				return false;
			}
			if (!_core.Reduce(refIntent))
			{
				return false;
			}

			_pendingLoad = {};
			_pendingLoadAssetId = 0u;
			_pendingLoadReason = PendingLoadReason::None;
			_pendingLoadSourceWriteTime.reset();
			_pendingLoadExpectedPublication.reset();
			_pendingLoadRollback.reset();
			_optActiveStroke.reset();
			_optPendingComposition.reset();
			_publishedSnapshot.reset();
			_externalCandidate.reset();
			_externalCandidateSourceWriteTime.reset();
			_externalCandidateExpectedPublication.reset();
			_conflictSaveAsPending = false;
			_hasObservedSourceWriteTime = false;
			_observedSourceRevision.reset();
			_sourceWriteTimeErrorCount = 0u;
			ResetExternalChangeState();
			_historyRollbackFailed = false;
			SyncAuthoringLayerSelection();
			_strLastError.clear();
			return true;
		}

		if (!CapturePendingLoadSourceWriteTime(refIntent.asset_id))
		{
			_strLastError = "Terrain asset source write time could not be captured.";
			return false;
		}
		const AshEngine::TerrainSnapshotPublicationToken expectedPublication =
			_pAssets->capture_terrain_snapshot_publication(refIntent.asset_id);
		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> load =
			_pAssets->load_terrain_candidate_by_id_async(refIntent.asset_id);
		if (!load.valid())
		{
			_pendingLoadSourceWriteTime.reset();
			_strLastError = "AssetDatabase did not return a Terrain load future.";
			return false;
		}

		PendingLoadRollbackState rollback{};
		rollback.previous_asset_id = previousAssetId;
		rollback.external_state = _externalChangeState;
		rollback.query_status = _core.GetPreviewState().query_status;
		rollback.external_candidate = _externalCandidate;
		rollback.candidate_source_write_time = _externalCandidateSourceWriteTime;
		rollback.candidate_expected_publication = _externalCandidateExpectedPublication;
		rollback.conflict_save_as_pending = _conflictSaveAsPending;
		rollback.last_error = _strLastError;
		_pendingLoadRollback = std::move(rollback);
		_pendingLoadExpectedPublication = expectedPublication;
		_pendingLoad = std::move(load);
		_pendingLoadAssetId = refIntent.asset_id;
		_pendingLoadReason = PendingLoadReason::Selection;
		_externalCandidate.reset();
		_externalCandidateSourceWriteTime.reset();
		_externalCandidateExpectedPublication.reset();
		_conflictSaveAsPending = false;
		SetExternalChangeState(
			TerrainExternalChangeStatus::Reloading,
			_core.GetContentGeneration(),
			0u,
			"Loading Terrain asset.",
			true,
			false,
			false);
		_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Pending);
		_strLastError.clear();
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
		const bool exportOperation = refIntent.kind == TerrainEditorIntent::Kind::Export;
		std::filesystem::path requested = exportOperation && refIntent.export_desc
			? refIntent.export_desc->destination_path
			: refIntent.asset_path;
		if (requested.empty())
		{
			if (pWorkingSet && !exportOperation)
			{
				requested = pWorkingSet->source_path;
			}
		}
		if (requested.empty())
		{
			outError = "Terrain file operation requires an asset path.";
			return false;
		}

		const bool requestedRelative = requested.is_relative();
		const bool requiresAssetRootForResolution = !exportOperation || requestedRelative;
		const bool requiresAssetRootContainment = !exportOperation;
		std::filesystem::path root{};
		if (requiresAssetRootForResolution)
		{
			if (!_pAssets || !_pAssets->is_valid() || _pAssets->get_root_path().empty())
			{
				outError = "Terrain file operation requires a configured asset root.";
				return false;
			}
			root = _pAssets->get_root_path();
		}
		if (requestedRelative)
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
		std::filesystem::path relativeWithinRoot{};
		if (requiresAssetRootContainment &&
			!TryNormalizeAssetPathWithinRoot(root, absolute, relativeWithinRoot))
		{
			outError = "Terrain asset path must stay inside the configured asset root.";
			return false;
		}
		absolute = std::filesystem::weakly_canonical(absolute, errorCode);
		if (errorCode || absolute.empty())
		{
			outError = "Terrain asset path could not be canonically resolved.";
			return false;
		}
		const std::filesystem::path parent = absolute.parent_path();
		if (parent.empty() || !std::filesystem::is_directory(parent, errorCode) || errorCode)
		{
			outError =
				"Terrain file destination parent must already exist as a directory.";
			return false;
		}

		if (exportOperation)
		{
			if (!refIntent.export_desc ||
				!HeightFileExtensionMatches(absolute, refIntent.export_desc->format))
			{
				outError =
					"Terrain Export destination extension does not match the selected height format.";
				return false;
			}
		}
		else
		{
			std::string extension = absolute.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
			if (extension != ".ashterrain")
			{
				outError = "Terrain asset path must use the .AshTerrain extension.";
				return false;
			}
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
		case TerrainEditorIntent::Kind::Create:
			kind = TerrainFileOperationKind::Create;
			if (refIntent.asset_path.empty())
			{
				_strLastError = "Terrain Create requires a destination asset path.";
				return false;
			}
			if (!AshEngine::is_valid_terrain_grid_layout(refIntent.create_desc.layout) ||
				!IsValidTerrainHeightMapping(refIntent.create_desc.height_mapping) ||
				!std::isfinite(refIntent.create_desc.flat_height))
			{
				_strLastError = "Terrain Create layout, height mapping, or flat height is invalid.";
				return false;
			}
			break;
		case TerrainEditorIntent::Kind::Import:
			kind = TerrainFileOperationKind::Import;
			if (refIntent.asset_path.empty() || !refIntent.import_desc)
			{
				_strLastError =
					"Terrain Import requires a destination asset path and an import description.";
				return false;
			}
			if (!HeightFileExtensionMatches(
					refIntent.import_desc->source_path, refIntent.import_desc->format))
			{
				_strLastError =
					"Terrain Import source extension does not match the selected height format.";
				return false;
			}
			if (!AshEngine::is_valid_terrain_grid_layout(
					refIntent.import_desc->target_layout) ||
				!IsValidTerrainHeightMapping(refIntent.import_desc->height_mapping))
			{
				_strLastError = "Terrain Import target layout or height mapping is invalid.";
				return false;
			}
			if (refIntent.import_desc->source_width == 0u ||
				refIntent.import_desc->source_height == 0u)
			{
				_strLastError = "Terrain Import requires non-zero source dimensions.";
				return false;
			}
			break;
		case TerrainEditorIntent::Kind::Export:
			kind = TerrainFileOperationKind::Export;
			if (!refIntent.export_desc || refIntent.export_desc->destination_path.empty())
			{
				_strLastError = "Terrain Export requires an export description and destination path.";
				return false;
			}
			if (!refIntent.asset_path.empty())
			{
				_strLastError =
					"Terrain Export destination belongs in export_desc, not asset_path.";
				return false;
			}
			if ((refIntent.export_desc->source ==
					AshEngine::TerrainExportSource::HeightEditLayer ||
				 refIntent.export_desc->source ==
					AshEngine::TerrainExportSource::MaterialWeightLayer) &&
				!refIntent.export_desc->source_layer_id.is_valid())
			{
				_strLastError =
					"Terrain layer export requires a valid stable source layer ID.";
				return false;
			}
			break;
		default:
			return false;
		}

		const bool sessionReplacement = kind == TerrainFileOperationKind::Create ||
			kind == TerrainFileOperationKind::Import;
		const bool requiresPublishedSnapshot = !sessionReplacement;
		if (requiresPublishedSnapshot && (!pWorkingSet || !_publishedSnapshot))
		{
			_strLastError = "Terrain file operation requires a published authoring snapshot.";
			return false;
		}
		if (sessionReplacement &&
			(_pendingLoad.valid() || _optPendingComposition || _core.IsDirty() ||
			 _externalChangeState.status != TerrainExternalChangeStatus::None ||
			 _historyRollbackFailed))
		{
			_strLastError =
				"Terrain Create and Import require a clean, idle authoring session.";
			return false;
		}
		if ((kind == TerrainFileOperationKind::Save ||
			 kind == TerrainFileOperationKind::Optimize) && !refIntent.asset_path.empty())
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
		const bool publishesNewPath = kind == TerrainFileOperationKind::SaveAs ||
			kind == TerrainFileOperationKind::Create ||
			kind == TerrainFileOperationKind::Import ||
			kind == TerrainFileOperationKind::Export;
		if (publishesNewPath)
		{
			std::error_code existsError{};
			if (std::filesystem::exists(path, existsError) || existsError)
			{
				_strLastError = existsError
					? "Terrain file destination could not be inspected."
					: "Terrain file operation publishes a new path and will not overwrite an existing file.";
				return false;
			}
		}

		std::optional<AshEngine::TerrainHeightImportDesc> importDesc{};
		if (kind == TerrainFileOperationKind::Import)
		{
			importDesc = *refIntent.import_desc;
			std::filesystem::path importSource{};
			if (!ResolveTerrainImportSource(
					_pAssets->get_root_path(), importDesc->source_path, importSource, error))
			{
				_strLastError = std::move(error);
				return false;
			}
			importDesc->source_path = std::move(importSource);
		}
		std::optional<AshEngine::TerrainHeightExportDesc> exportDesc{};
		if (kind == TerrainFileOperationKind::Export)
		{
			exportDesc = *refIntent.export_desc;
			exportDesc->destination_path = path;
		}

		++_nextFileOperationSerial;
		if (_nextFileOperationSerial == 0u)
		{
			++_nextFileOperationSerial;
		}
		_fileOperationState = {};
		_pendingPublishedBindOperationSerial = 0u;
		_pendingPublishedBindAssetId = 0u;
		_fileOperationState.kind = kind;
		_fileOperationState.status = TerrainFileOperationStatus::AwaitingPublication;
		_fileOperationState.operation_serial = _nextFileOperationSerial;
		_fileOperationState.asset_id = pWorkingSet ? pWorkingSet->asset_id : 0u;
		_fileOperationState.content_generation = pWorkingSet ? pWorkingSet->content_generation : 0u;
		_fileOperationState.path = std::move(path);
		_pendingFileOperationPhase.reset();
		_pendingCreateDesc = kind == TerrainFileOperationKind::Create
			? std::optional<TerrainCreateAssetDesc>{ refIntent.create_desc }
			: std::nullopt;
		_pendingImportDesc = std::move(importDesc);
		_pendingExportDesc = std::move(exportDesc);
		_strLastError.clear();

		if (!_optPendingComposition)
		{
			TryStartFileOperation();
		}
		return _fileOperationState.status != TerrainFileOperationStatus::Failed;
	}

	bool TerrainEditorService::CancelFileOperation()
	{
		if (_fileOperationState.status == TerrainFileOperationStatus::Running &&
			_pendingFileOperation.valid() &&
			_pendingFileOperation.wait_for(std::chrono::seconds(0)) ==
				std::future_status::ready)
		{
			CompletePendingFileOperation();
			_strLastError =
				"Terrain file operation already completed before cancellation was requested.";
			return false;
		}
		if (_fileOperationState.status != TerrainFileOperationStatus::Running)
		{
			_strLastError = "Terrain file cancellation requires a running Import or Export.";
			return false;
		}
		if ((_fileOperationState.kind != TerrainFileOperationKind::Import || !_pendingImportDesc) &&
			(_fileOperationState.kind != TerrainFileOperationKind::Export || !_pendingExportDesc))
		{
			_strLastError = "Only Terrain Import and Export file jobs support cancellation.";
			return false;
		}
		if (!_pendingFileOperationPhase)
		{
			_strLastError = "Terrain file cancellation state is unavailable.";
			return false;
		}

		CancellableFileJobPhase expected = CancellableFileJobPhase::Cancellable;
		if (!_pendingFileOperationPhase->compare_exchange_strong(
				expected,
				CancellableFileJobPhase::Cancelled,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
		{
			switch (expected)
			{
			case CancellableFileJobPhase::Publishing:
				_strLastError =
					"Terrain file cancellation is unavailable after final publication begins.";
				break;
			case CancellableFileJobPhase::Cancelled:
				_strLastError = "Terrain file cancellation was already accepted.";
				break;
			case CancellableFileJobPhase::Completed:
			default:
				_strLastError = "Terrain file operation already completed.";
				break;
			}
			return false;
		}

		if (_pendingImportDesc)
		{
			_pendingImportDesc->cancellation.cancel();
		}
		if (_pendingExportDesc)
		{
			_pendingExportDesc->cancellation.cancel();
		}
		_strLastError.clear();
		return true;
	}

	void TerrainEditorService::FailFileOperation(std::string error)
	{
		if (error.empty())
		{
			error = "Terrain file operation failed.";
		}
		_fileOperationState.status = TerrainFileOperationStatus::Failed;
		_fileOperationState.error = error;
		_pendingFileOperationPhase.reset();
		_pendingCreateDesc.reset();
		_pendingImportDesc.reset();
		_pendingExportDesc.reset();
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

		const TerrainFileOperationKind kind = _fileOperationState.kind;
		const bool sessionReplacement = kind == TerrainFileOperationKind::Create ||
			kind == TerrainFileOperationKind::Import;
		const AshEngine::TerrainWorkingSet* pWorkingSet = _core.GetWorkingSet();
		if (sessionReplacement)
		{
			if ((kind == TerrainFileOperationKind::Create && !_pendingCreateDesc) ||
				(kind == TerrainFileOperationKind::Import && !_pendingImportDesc) ||
				_pendingLoad.valid() || _core.IsDirty() ||
				_externalChangeState.status != TerrainExternalChangeStatus::None)
			{
				FailFileOperation(
					"Terrain Create or Import lost its clean immutable dispatch inputs.");
				return false;
			}
		}
		else if (!pWorkingSet || pWorkingSet->asset_id != _fileOperationState.asset_id ||
			pWorkingSet->content_generation != _fileOperationState.content_generation ||
			!_publishedSnapshot || _publishedSnapshot->asset_id != pWorkingSet->asset_id ||
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
		std::optional<AshEngine::TerrainContainerRevision> expectedRevision{};
		if (kind == TerrainFileOperationKind::Save ||
			kind == TerrainFileOperationKind::Optimize)
		{
			if (!_observedSourceRevision || !_observedSourceRevision->is_valid())
			{
				FailFileOperation(
					"Terrain save cannot start without a validated source revision.");
				return false;
			}
			expectedRevision = *_observedSourceRevision;
		}
		if (kind == TerrainFileOperationKind::Save ||
			kind == TerrainFileOperationKind::SaveAs ||
			kind == TerrainFileOperationKind::Export)
		{
			snapshot = _publishedSnapshot;
			if (kind == TerrainFileOperationKind::Save)
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
			const std::optional<TerrainCreateAssetDesc> createDesc = _pendingCreateDesc;
			const std::optional<AshEngine::TerrainHeightImportDesc> importDesc = _pendingImportDesc;
			const std::optional<AshEngine::TerrainHeightExportDesc> exportDesc = _pendingExportDesc;
			std::shared_ptr<std::atomic<CancellableFileJobPhase>> cancellationPhase{};
			if (kind == TerrainFileOperationKind::Import ||
				kind == TerrainFileOperationKind::Export)
			{
				cancellationPhase =
					std::make_shared<std::atomic<CancellableFileJobPhase>>(
						CancellableFileJobPhase::Cancellable);
			}
			_pendingFileOperationPhase = cancellationPhase;
#if defined(ASH_TESTS)
			const FileJobTestHook fileJobTestHook = _fileJobTestHook;
#endif
			auto promise = std::make_shared<std::promise<TerrainFileJobResult>>();
			_pendingFileOperation = promise->get_future().share();
			auto dispatchState = std::make_shared<TerrainFileDispatchState>(std::move(promise));
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
					 dirtyPayloads = std::move(dirtyPayloads),
					 expectedRevision,
					 createDesc,
					 importDesc,
					 exportDesc,
					 cancellationPhase
#if defined(ASH_TESTS)
					 , fileJobTestHook
#endif
					 ]() mutable
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
						if (cancellationPhase)
						{
							CancellableFileJobPhase expected =
								CancellableFileJobPhase::Cancellable;
							if (!cancellationPhase->compare_exchange_strong(
									expected,
									CancellableFileJobPhase::Completed,
									std::memory_order_acq_rel,
									std::memory_order_acquire) &&
								expected == CancellableFileJobPhase::Cancelled)
							{
								TerrainFileJobResult cancelledResult{};
								cancelledResult.cancelled = true;
								cancelledResult.error =
									"Terrain file operation was cancelled.";
								dispatchState->Resolve(std::move(cancelledResult));
								return;
							}
						}
							dispatchState->Resolve(
								false,
								"Terrain async file operation requires an available worker thread.");
							return;
						}

						try
						{
							std::string error{};
							AshEngine::TerrainContainerResult containerResult =
								AshEngine::TerrainContainerResult::InvalidData;
							AshEngine::TerrainContainerSaveReport saveReport{};
							std::vector<std::string> warnings{};
							bool succeeded = false;
							bool cancelled = false;
							if (kind == TerrainFileOperationKind::Optimize)
							{
								containerResult = AshEngine::optimize_terrain_container(
									path, &*expectedRevision, &saveReport, &error);
								succeeded = containerResult == AshEngine::TerrainContainerResult::Success;
							}
							else if (kind == TerrainFileOperationKind::SaveAs)
							{
								containerResult = SaveTerrainCopyNew(path, *snapshot, error);
								succeeded = containerResult == AshEngine::TerrainContainerResult::Success;
							}
							else if (kind == TerrainFileOperationKind::Save)
							{
								containerResult = AshEngine::save_terrain_container_incremental(
									path, *snapshot, dirtyPayloads,
									&*expectedRevision, &saveReport, &error);
								succeeded = containerResult == AshEngine::TerrainContainerResult::Success;
							}
							else if (kind == TerrainFileOperationKind::Create)
							{
								std::shared_ptr<const AshEngine::TerrainAssetSnapshot> created{};
								if (!createDesc || !AshEngine::create_flat_terrain_snapshot(
										0u,
										createDesc->layout,
										createDesc->height_mapping,
										createDesc->flat_height,
										created,
										&error) || !created)
								{
									if (error.empty())
									{
										error = "Terrain Create could not build its immutable flat snapshot.";
									}
								}
								else
								{
									containerResult = SaveTerrainCopyNew(path, *created, error);
									succeeded =
										containerResult == AshEngine::TerrainContainerResult::Success;
								}
							}
							else if (kind == TerrainFileOperationKind::Import)
							{
								const std::filesystem::path staged =
									MakeTerrainFileTemporaryPath(path, "import");
								TerrainCopyTemporaryGuard stagedGuard{ staged };
								std::error_code stageError{};
								if (!importDesc || std::filesystem::exists(staged, stageError) || stageError)
								{
									error = "Terrain Import could not reserve a unique private staged container.";
								}
								else
								{
									std::shared_ptr<const AshEngine::TerrainAssetSnapshot> imported{};
									AshEngine::TerrainImportReport importReport{};
#if defined(ASH_TESTS)
									if (fileJobTestHook)
									{
										fileJobTestHook(kind, FileJobTestPoint::BeforeCodec);
									}
#endif
									const AshEngine::TerrainImportResult importResult =
										AshEngine::import_terrain_height_to_container(
											0u, *importDesc, staged, imported, &importReport, &error);
									warnings = std::move(importReport.warnings);
									cancelled = importResult == AshEngine::TerrainImportResult::Cancelled ||
										importDesc->cancellation.is_cancelled() ||
										(cancellationPhase && cancellationPhase->load(
											std::memory_order_acquire) ==
											CancellableFileJobPhase::Cancelled);
									if (importResult == AshEngine::TerrainImportResult::Success && !cancelled)
									{
										CancellableFileJobPhase expected =
											CancellableFileJobPhase::Cancellable;
										if (cancellationPhase && cancellationPhase->compare_exchange_strong(
												expected,
												CancellableFileJobPhase::Publishing,
												std::memory_order_acq_rel,
												std::memory_order_acquire))
										{
											#if defined(ASH_TESTS)
											if (fileJobTestHook)
											{
												fileJobTestHook(kind, FileJobTestPoint::AfterPublishClaim);
											}
											#endif
											containerResult = AshEngine::publish_staged_terrain_container_new(
												path, staged, &error);
											succeeded = containerResult ==
												AshEngine::TerrainContainerResult::Success;
											cancellationPhase->store(
												CancellableFileJobPhase::Completed,
												std::memory_order_release);
											if (succeeded)
											{
												stagedGuard.Release();
											}
										}
										else if (expected == CancellableFileJobPhase::Cancelled)
										{
											cancelled = true;
										}
										else
										{
											error = "Terrain Import final publication phase was unavailable.";
										}
									}
								}
							}
							else if (kind == TerrainFileOperationKind::Export)
							{
								const std::filesystem::path staged =
									MakeTerrainFileTemporaryPath(path, "export");
								TerrainCopyTemporaryGuard stagedGuard{ staged };
								std::error_code stageError{};
								if (!exportDesc || !snapshot ||
									std::filesystem::exists(staged, stageError) || stageError)
								{
									error = "Terrain Export could not reserve a unique private staged file.";
								}
								else
								{
									AshEngine::TerrainHeightExportDesc stagedDesc = *exportDesc;
									stagedDesc.destination_path = staged;
									std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
										exportSnapshot = snapshot;
									if (stagedDesc.source ==
										AshEngine::TerrainExportSource::MaterialWeightLayer)
									{
										auto normalizedSnapshot =
											std::make_shared<AshEngine::TerrainAssetSnapshot>(*snapshot);
										normalizedSnapshot->height_mapping = { 0.0f, 1.0f };
										exportSnapshot = std::move(normalizedSnapshot);
									}
#if defined(ASH_TESTS)
									if (fileJobTestHook)
									{
										fileJobTestHook(kind, FileJobTestPoint::BeforeCodec);
									}
#endif
									const AshEngine::TerrainImportResult exportResult =
										AshEngine::export_terrain_height(
											*exportSnapshot, stagedDesc, &error);
									cancelled = exportResult == AshEngine::TerrainImportResult::Cancelled ||
										exportDesc->cancellation.is_cancelled() ||
										(cancellationPhase && cancellationPhase->load(
											std::memory_order_acquire) ==
											CancellableFileJobPhase::Cancelled);
									if (exportResult == AshEngine::TerrainImportResult::Success && !cancelled)
									{
										CancellableFileJobPhase expected =
											CancellableFileJobPhase::Cancellable;
										if (cancellationPhase && cancellationPhase->compare_exchange_strong(
												expected,
												CancellableFileJobPhase::Publishing,
												std::memory_order_acq_rel,
												std::memory_order_acquire))
										{
											#if defined(ASH_TESTS)
											if (fileJobTestHook)
											{
												fileJobTestHook(kind, FileJobTestPoint::AfterPublishClaim);
											}
											#endif
											succeeded = PublishStagedFileNew(path, staged, error);
											cancellationPhase->store(
												CancellableFileJobPhase::Completed,
												std::memory_order_release);
											if (succeeded)
											{
												stagedGuard.Release();
											}
										}
										else if (expected == CancellableFileJobPhase::Cancelled)
										{
											cancelled = true;
										}
										else
										{
											error = "Terrain Export final publication phase was unavailable.";
										}
									}
								}
							}
							else
							{
								error = "Terrain file operation kind is invalid.";
							}
							if (cancellationPhase)
							{
								CancellableFileJobPhase expected =
									CancellableFileJobPhase::Cancellable;
								if (cancelled)
								{
									cancellationPhase->compare_exchange_strong(
										expected,
										CancellableFileJobPhase::Cancelled,
										std::memory_order_acq_rel,
										std::memory_order_acquire);
								}
								else if (!cancellationPhase->compare_exchange_strong(
										expected,
										CancellableFileJobPhase::Completed,
										std::memory_order_acq_rel,
										std::memory_order_acquire) &&
									expected == CancellableFileJobPhase::Cancelled)
								{
									cancelled = true;
									succeeded = false;
								}
							}
							if (cancelled && error.empty())
							{
								error = "Terrain file operation was cancelled.";
							}
							else if (!succeeded && error.empty())
							{
								error = "Terrain file operation failed.";
							}
							TerrainFileJobResult jobResult{};
							jobResult.succeeded = succeeded;
							jobResult.error = std::move(error);
							jobResult.source_changed =
								containerResult == AshEngine::TerrainContainerResult::SourceChanged;
							jobResult.cancelled = cancelled;
							jobResult.warnings = std::move(warnings);
							if (jobResult.succeeded &&
								(kind == TerrainFileOperationKind::Save ||
								 kind == TerrainFileOperationKind::Optimize))
							{
								if (!saveReport.committed_revision.is_valid())
								{
									jobResult.succeeded = false;
									jobResult.error =
										"Terrain file operation completed without a committed source revision.";
								}
								else
								{
									jobResult.committed_revision = saveReport.committed_revision;
								}
								std::error_code writeTimeError{};
								const std::filesystem::file_time_type writeTime =
									std::filesystem::last_write_time(path, writeTimeError);
								if (writeTimeError)
								{
									// The container commit is durable, but without the paired source
									// metadata sample the editor cannot safely advance its observed
									// baseline. Preserve dirty/persisted state and let the normal
									// external-revision poll reconcile the committed file.
									jobResult.succeeded = false;
									jobResult.error =
										"Terrain file operation committed but its stable source write time could not be captured.";
								}
								else
								{
									jobResult.source_write_time = writeTime;
								}
							}
							dispatchState->Resolve(std::move(jobResult));
					}
					catch (const std::exception& exception)
					{
						bool cancellationWon = false;
						if (cancellationPhase)
						{
							CancellableFileJobPhase expected =
								CancellableFileJobPhase::Cancellable;
							if (!cancellationPhase->compare_exchange_strong(
									expected,
									CancellableFileJobPhase::Completed,
									std::memory_order_acq_rel,
									std::memory_order_acquire) &&
								expected == CancellableFileJobPhase::Publishing)
							{
								cancellationPhase->store(
									CancellableFileJobPhase::Completed,
									std::memory_order_release);
							}
							else if (expected == CancellableFileJobPhase::Cancelled)
							{
								cancellationWon = true;
							}
						}
						if (cancellationWon)
						{
							TerrainFileJobResult result{};
							result.cancelled = true;
							result.error = "Terrain file operation was cancelled.";
							dispatchState->Resolve(std::move(result));
						}
						else
						{
							dispatchState->Resolve(false, exception.what());
						}
					}
					catch (...)
					{
						bool cancellationWon = false;
						if (cancellationPhase)
						{
							CancellableFileJobPhase expected =
								CancellableFileJobPhase::Cancellable;
							if (!cancellationPhase->compare_exchange_strong(
									expected,
									CancellableFileJobPhase::Completed,
									std::memory_order_acq_rel,
									std::memory_order_acquire) &&
								expected == CancellableFileJobPhase::Publishing)
							{
								cancellationPhase->store(
									CancellableFileJobPhase::Completed,
									std::memory_order_release);
							}
							else if (expected == CancellableFileJobPhase::Cancelled)
							{
								cancellationWon = true;
							}
						}
						if (cancellationWon)
						{
							TerrainFileJobResult result{};
							result.cancelled = true;
							result.error = "Terrain file operation was cancelled.";
							dispatchState->Resolve(std::move(result));
						}
						else
						{
							dispatchState->Resolve(
								false,
								"Terrain file worker raised an unknown exception.");
						}
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

	bool TerrainEditorService::TryBindPublishedFileOperation()
	{
		if (_fileOperationState.status !=
				TerrainFileOperationStatus::PublishedAwaitingCatalog ||
			(_fileOperationState.kind != TerrainFileOperationKind::Create &&
			 _fileOperationState.kind != TerrainFileOperationKind::Import))
		{
			return false;
		}
		if (_pendingLoad.valid())
		{
			return true;
		}

		auto deferRetry = [this](std::string error)
		{
			if (error.empty())
			{
				error = "Terrain asset was published but could not bind through the asset catalog.";
			}
			_fileOperationState.status =
				TerrainFileOperationStatus::PublishedAwaitingCatalog;
			_fileOperationState.error = error;
			_strLastError = std::move(error);
			_pendingPublishedBindOperationSerial = 0u;
			_pendingPublishedBindAssetId = 0u;
			_nextPublishedCatalogRetryTime =
				std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
			return false;
		};

		if (!_pAssets || !_pAssets->is_valid())
		{
			return deferRetry(
				"Terrain asset was published, but the AssetDatabase is unavailable for binding.");
		}
		if (!_pAssets->refresh())
		{
			return deferRetry(
				"Terrain asset was published, but the AssetDatabase catalog refresh failed.");
		}

		// Refresh invalidates the Terrain cache. Preserve the clean old authoring session
		// until the newly published asset has loaded and committed through SelectAsset.
		if (_publishedSnapshot && _core.GetAssetId() != 0u)
		{
			const AshEngine::AssetInfo* currentInfo =
				_pAssets->find_asset_by_id(_core.GetAssetId());
			if (!currentInfo || currentInfo->type != AshEngine::AssetType::Terrain ||
				!_pAssets->publish_terrain_snapshot(
					_core.GetAssetId(), _publishedSnapshot))
			{
				return deferRetry(
					"Terrain asset was published, but the previous clean session could not be republished after catalog refresh.");
			}
		}

		const AshEngine::AssetInfo* published =
			_pAssets->find_asset_by_path(_fileOperationState.path);
		if (!published || published->type != AshEngine::AssetType::Terrain ||
			published->id == 0u)
		{
			return deferRetry(
				"Terrain asset was published, but the refreshed catalog did not expose it as Terrain.");
		}

		_fileOperationState.asset_id = published->id;
		_fileOperationState.error.clear();
		TerrainEditorIntent select{};
		select.kind = TerrainEditorIntent::Kind::SelectAsset;
		select.asset_id = published->id;
		_fileOperationState.status = TerrainFileOperationStatus::Succeeded;
		if (!SubmitSelectAssetIntent(select))
		{
			const std::string selectionError = _strLastError;
			return deferRetry(selectionError.empty()
				? "Terrain asset was published, but SelectAsset could not start its load."
				: selectionError);
		}
		_pendingPublishedBindOperationSerial = _fileOperationState.operation_serial;
		_pendingPublishedBindAssetId = published->id;
		_fileOperationState.status = TerrainFileOperationStatus::PublishedAwaitingCatalog;
		_nextPublishedCatalogRetryTime =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
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
			result = { false, exception.what(), {} };
		}
		catch (...)
		{
			result = { false, "Terrain file worker raised an unknown exception.", {} };
		}
		_pendingFileOperation = {};
		_pendingFileOperationPhase.reset();
		_fileOperationState.warnings = std::move(result.warnings);

		if (!result.succeeded)
		{
			_conflictSaveAsPending = false;
			if (_fileOperationState.kind == TerrainFileOperationKind::Save)
			{
				_core.CompleteSaveContentGeneration(
					_fileOperationState.content_generation, false);
			}
			if (result.cancelled)
			{
				_fileOperationState.status = TerrainFileOperationStatus::Cancelled;
				_fileOperationState.error = result.error.empty()
					? "Terrain file operation was cancelled."
					: std::move(result.error);
				_pendingCreateDesc.reset();
				_pendingImportDesc.reset();
				_pendingExportDesc.reset();
				_strLastError = _fileOperationState.error;
				return;
			}
			FailFileOperation(std::move(result.error));
			if (result.source_changed)
			{
				_nextExternalPollTime = std::chrono::steady_clock::now();
			}
			return;
		}
		if (_fileOperationState.kind == TerrainFileOperationKind::Create ||
			_fileOperationState.kind == TerrainFileOperationKind::Import)
		{
			_fileOperationState.status =
				TerrainFileOperationStatus::PublishedAwaitingCatalog;
			_fileOperationState.error.clear();
			_pendingCreateDesc.reset();
			_pendingImportDesc.reset();
			_pendingExportDesc.reset();
			_nextPublishedCatalogRetryTime = std::chrono::steady_clock::now();
			_strLastError.clear();
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
		_pendingCreateDesc.reset();
		_pendingImportDesc.reset();
		_pendingExportDesc.reset();
		const bool resolveConflictAfterCopy =
			_conflictSaveAsPending &&
			_fileOperationState.kind == TerrainFileOperationKind::SaveAs;
		_conflictSaveAsPending = false;
		if (resolveConflictAfterCopy)
		{
			const TerrainFileOperationState completedState = _fileOperationState;
			if (!ApplyExternalCandidate(_core.GetAssetId()))
			{
				FailFileOperation(
					"Terrain local copy succeeded, but the validated disk candidate could not be applied: " +
					_strLastError);
				return;
			}
			_fileOperationState = completedState;
		}
		else if (_fileOperationState.kind == TerrainFileOperationKind::Save ||
			_fileOperationState.kind == TerrainFileOperationKind::Optimize)
		{
			ObserveLoadedSourceWriteTime(
				result.source_write_time, result.committed_revision);
		}
		if (_externalChangeState.status == TerrainExternalChangeStatus::None)
		{
			_strLastError.clear();
		}
		else
		{
			_strLastError = _externalChangeState.diagnostic;
		}
	}

	void TerrainEditorService::ScheduleComposition(
		const uint64_t sourceSequence,
		std::vector<AshEngine::TerrainComponentCoord> dirtyComponents) noexcept
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
		const PendingLoadReason completedReason = _pendingLoadReason;
		const bool completesPublishedBinding =
			_fileOperationState.status == TerrainFileOperationStatus::PublishedAwaitingCatalog &&
			(_fileOperationState.kind == TerrainFileOperationKind::Create ||
			 _fileOperationState.kind == TerrainFileOperationKind::Import) &&
			_pendingPublishedBindOperationSerial ==
				_fileOperationState.operation_serial &&
			_pendingPublishedBindAssetId == completedAssetId &&
			_fileOperationState.asset_id == completedAssetId &&
			completedReason == PendingLoadReason::Selection;
		const std::optional<std::filesystem::file_time_type> completedSourceWriteTime =
			_pendingLoadSourceWriteTime;
		const std::optional<AshEngine::TerrainSnapshotPublicationToken>
			completedExpectedPublication = _pendingLoadExpectedPublication;
		std::shared_future<std::shared_ptr<const AshEngine::TerrainAssetSnapshot>> completedLoad =
			std::move(_pendingLoad);
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_pendingLoadReason = PendingLoadReason::None;
		_pendingLoadSourceWriteTime.reset();
		_pendingLoadExpectedPublication.reset();
		auto restorePendingLoadState = [this](
			const bool restorePreviousCandidate,
			std::string error)
		{
			if (!_pendingLoadRollback)
			{
				if (!error.empty())
				{
					_strLastError = std::move(error);
				}
				return;
			}
			PendingLoadRollbackState rollback = std::move(*_pendingLoadRollback);
			_pendingLoadRollback.reset();
			_externalChangeState = std::move(rollback.external_state);
			_core.SetPreviewQueryStatus(rollback.query_status);
			if (restorePreviousCandidate)
			{
				_externalCandidate = std::move(rollback.external_candidate);
				_externalCandidateSourceWriteTime = rollback.candidate_source_write_time;
				_externalCandidateExpectedPublication =
					std::move(rollback.candidate_expected_publication);
			}
			_conflictSaveAsPending = rollback.conflict_save_as_pending;
			_strLastError = error.empty() ? std::move(rollback.last_error) : std::move(error);
		};
		auto retryPublishedBinding = [this, completesPublishedBinding](
			std::string error)
		{
			if (!completesPublishedBinding)
			{
				return;
			}
			_fileOperationState.status =
				TerrainFileOperationStatus::PublishedAwaitingCatalog;
			_fileOperationState.error = error.empty()
				? "Terrain asset was published but its catalog load did not complete."
				: std::move(error);
			_strLastError = _fileOperationState.error;
			_pendingPublishedBindOperationSerial = 0u;
			_pendingPublishedBindAssetId = 0u;
			_nextPublishedCatalogRetryTime =
				std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		};

		try
		{
			const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot = completedLoad.get();
			const bool belongsToCurrentSession = completedReason == PendingLoadReason::Selection
				? (_pendingLoadRollback &&
					_pendingLoadRollback->previous_asset_id == _core.GetAssetId())
				: completedAssetId == _core.GetAssetId();
			if (completedAssetId == 0u || !belongsToCurrentSession)
			{
				_pendingLoadRollback.reset();
				retryPublishedBinding(
					"Terrain published-asset load no longer belongs to the preserved session.");
				return;
			}
			if (!snapshot || snapshot->failed || snapshot->asset_id != completedAssetId)
			{
				std::string error = snapshot && !snapshot->failure_detail.empty()
					? snapshot->failure_detail
					: (_pAssets ? _pAssets->get_asset_last_error(completedAssetId) : std::string{});
				if (error.empty())
				{
					error = "Terrain asset load failed.";
				}
				if (snapshot && snapshot->retryable_failure &&
					completedReason != PendingLoadReason::Selection)
				{
					// A cooperative writer changed/held the source while this candidate
					// decoded. Restore the exact pre-load state instead of freezing the
					// session as Failed. Plain automatic reload will poll again after the
					// wall-clock debounce; explicit conflict/repair actions remain available.
					restorePendingLoadState(true, {});
					if (completedReason == PendingLoadReason::Reload)
					{
						_nextExternalPollTime =
							std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
					}
					else
					{
						_strLastError = std::move(error);
					}
					return;
				}
				if (completedReason == PendingLoadReason::Selection)
				{
					const std::string publishedError = error;
					FailPendingSelection(std::move(error));
					retryPublishedBinding(publishedError);
				}
				else
				{
					EnterExternalFailure(std::move(error));
				}
				return;
			}

			if (completedReason == PendingLoadReason::Selection)
			{
				const AshEngine::TerrainAssetId previousAssetId =
					_pendingLoadRollback ? _pendingLoadRollback->previous_asset_id : 0u;
				_externalCandidate = snapshot;
				_externalCandidateSourceWriteTime = completedSourceWriteTime;
				_externalCandidateExpectedPublication = completedExpectedPublication;
				if (!ApplyExternalCandidate(previousAssetId))
				{
					const std::string selectionError = _strLastError.empty()
						? "Terrain asset could not open an authoring working set."
						: _strLastError;
					FailPendingSelection(selectionError);
					retryPublishedBinding(selectionError);
				}
				else if (completesPublishedBinding)
				{
					_fileOperationState.status = TerrainFileOperationStatus::Succeeded;
					_fileOperationState.asset_id = snapshot->asset_id;
					_fileOperationState.content_generation = snapshot->content_generation;
					_fileOperationState.error.clear();
					_pendingPublishedBindOperationSerial = 0u;
					_pendingPublishedBindAssetId = 0u;
					if (_externalChangeState.status == TerrainExternalChangeStatus::None)
					{
						_strLastError.clear();
					}
				}
				return;
			}

			const uint64_t effectiveDiskGeneration =
				GetEffectiveTerrainDiskGeneration(*snapshot);
			if (effectiveDiskGeneration == 0u)
			{
				EnterExternalFailure("Terrain reload candidate has an invalid content generation.");
				return;
			}

			_externalCandidate = snapshot;
			_externalCandidateSourceWriteTime = completedSourceWriteTime;
			_externalCandidateExpectedPublication = completedExpectedPublication;
			if (completedReason == PendingLoadReason::Repair ||
				completedReason == PendingLoadReason::ConflictReload)
			{
				if (!ApplyExternalCandidate(_core.GetAssetId()))
				{
					restorePendingLoadState(false, _strLastError);
				}
				return;
			}

			const bool physicalRollback =
				effectiveDiskGeneration < _core.GetPersistedContentGeneration();
			if (_core.IsDirty() || physicalRollback)
			{
				if (_core.NotifyExternalContentGeneration(
						effectiveDiskGeneration,
						physicalRollback) !=
					TerrainExternalChangeResult::Conflict)
				{
					EnterExternalFailure(
						"Terrain reload candidate could not enter the expected conflict state.");
					return;
				}
				_externalCandidate = snapshot;
				_externalCandidateSourceWriteTime = completedSourceWriteTime;
				_externalCandidateExpectedPublication = completedExpectedPublication;
				_pendingLoadRollback.reset();
				_core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Ready);
				SetExternalChangeState(
					TerrainExternalChangeStatus::Conflict,
					_core.GetContentGeneration(),
					effectiveDiskGeneration,
					physicalRollback
						? "Terrain file was replaced by an older committed generation; choose Reload or Keep Local."
						: "Terrain file changed on disk while the authoring session has unsaved local changes.",
					true,
					false,
					_publishedSnapshot != nullptr);
				_strLastError = _externalChangeState.diagnostic;
				return;
			}

			if (!ApplyExternalCandidate(_core.GetAssetId()))
			{
				restorePendingLoadState(false, _strLastError);
			}
		}
		catch (const std::exception& refException)
		{
			if (completedReason == PendingLoadReason::Selection)
			{
				const std::string error = refException.what();
				FailPendingSelection(error);
				retryPublishedBinding(error);
			}
			else if (completedAssetId == _core.GetAssetId())
			{
				EnterExternalFailure(refException.what());
			}
		}
		catch (...)
		{
			if (completedReason == PendingLoadReason::Selection)
			{
				const std::string error =
					"Terrain asset load raised an unknown exception.";
				FailPendingSelection(error);
				retryPublishedBinding(error);
			}
			else if (completedAssetId == _core.GetAssetId())
			{
				EnterExternalFailure("Terrain asset load raised an unknown exception.");
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
