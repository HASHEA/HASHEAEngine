#include "Services/TerrainEditorService.h"

#include "Core/IEditorCommandExecutor.h"
#include "Core/TerrainCommands.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace AshEditor
{
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
		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_optActiveStroke.reset();
		_optPendingComposition.reset();
		_publishedSnapshot.reset();
		_nextStrokeSequence = 0u;
		_nextLayerSequence = 0u;
		_nextCompositionSerial = 0u;
		_latestCompositionSourceSequence = 0u;
		_historyRollbackFailed = false;
		_strLastError.clear();
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

		switch (refIntent.kind)
		{
		case TerrainEditorIntent::Kind::SelectAsset:
			return SubmitSelectAssetIntent(refIntent);
		case TerrainEditorIntent::Kind::SelectLayer:
			return SubmitSelectLayerIntent(refIntent);
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
		default:
			return false;
		}
	}

	bool TerrainEditorService::OpenSnapshotForAuthoring(
		const AshEngine::TerrainAssetSnapshot& refSnapshot)
	{
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

		_pendingLoad = {};
		_pendingLoadAssetId = 0u;
		_optActiveStroke.reset();
		_optPendingComposition.reset();
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

		ScheduleComposition(sequence, std::move(dirtyComponents));
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

	AshEngine::TerrainLayerId TerrainEditorService::GetSelectedLayerId() const
	{
		return _core.GetSelectedLayerId();
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorService::GetWorkingSet() const
	{
		return _core.GetWorkingSet();
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
		return _pendingLoad.valid() || _optPendingComposition.has_value();
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
		const uint8_t toolValue = static_cast<uint8_t>(refIntent.brush.tool);
		if (!pWorkingSet || _optActiveStroke || _core.HasActiveStroke() ||
			_core.GetPreviewState().query_status != AshEngine::TerrainQueryStatus::Ready ||
			!validMetric || !selectedLayerId.is_valid() ||
			refIntent.layer_id != selectedLayerId ||
			refIntent.brush.layer_id != selectedLayerId ||
			toolValue > static_cast<uint8_t>(AshEngine::TerrainBrushTool::Erase))
		{
			_strLastError = "Terrain stroke begin state, metric, layer, or tool is invalid.";
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

		const bool additiveHeightTool =
			refIntent.brush.tool == AshEngine::TerrainBrushTool::Raise ||
			refIntent.brush.tool == AshEngine::TerrainBrushTool::Lower ||
			refIntent.brush.tool == AshEngine::TerrainBrushTool::Noise;
		const bool alphaHeightTool =
			refIntent.brush.tool == AshEngine::TerrainBrushTool::Smooth ||
			refIntent.brush.tool == AshEngine::TerrainBrushTool::Flatten;
		if ((additiveHeightTool &&
				layer->height_blend_mode != AshEngine::TerrainHeightBlendMode::Additive) ||
			(alphaHeightTool &&
				layer->height_blend_mode != AshEngine::TerrainHeightBlendMode::Alpha))
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
		_optActiveStroke->parameters = refIntent.brush;
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
}
