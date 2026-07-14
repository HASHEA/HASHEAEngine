#include "Core/TerrainEditorSessionCore.h"

#include "Function/Asset/TerrainComposition.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace AshEditor
{
	AshEngine::TerrainAssetId TerrainEditorSessionCore::GetAssetId() const
	{
		return _assetId;
	}

	const TerrainEditorPreviewState& TerrainEditorSessionCore::GetPreviewState() const
	{
		return _preview;
	}

	bool TerrainEditorSessionCore::HasActiveStroke() const
	{
		return _activeSequence != 0u;
	}

	bool TerrainEditorSessionCore::Reduce(const TerrainEditorIntent& refIntent)
	{
		if (refIntent.kind != TerrainEditorIntent::Kind::SelectAsset)
		{
			return false;
		}

		if (_assetId != refIntent.asset_id)
		{
			_optWorkingSet.reset();
			_persistedContentGeneration = 0u;
			_activeSequence = 0u;
			_preview = {};
		}
		_assetId = refIntent.asset_id;
		return true;
	}

	bool TerrainEditorSessionCore::Open(AshEngine::TerrainWorkingSet workingSet)
	{
		if (workingSet.asset_id == 0u || !AshEngine::is_valid_terrain_grid_layout(workingSet.layout))
		{
			return false;
		}

		_assetId = workingSet.asset_id;
		_persistedContentGeneration = workingSet.content_generation;
		_optWorkingSet = std::move(workingSet);
		_activeSequence = 0u;
		_preview = {};
		_preview.query_status = AshEngine::TerrainQueryStatus::Ready;
		return true;
	}

	void TerrainEditorSessionCore::Close()
	{
		_assetId = 0u;
		_activeSequence = 0u;
		_persistedContentGeneration = 0u;
		_optWorkingSet.reset();
		_preview = {};
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorSessionCore::GetWorkingSet() const
	{
		return _optWorkingSet ? &*_optWorkingSet : nullptr;
	}

	bool TerrainEditorSessionCore::BeginStroke(const uint64_t sequence)
	{
		if (!_optWorkingSet || sequence == 0u || _activeSequence != 0u ||
			_preview.query_status != AshEngine::TerrainQueryStatus::Ready)
		{
			return false;
		}

		_activeSequence = sequence;
		_preview.stroke_active = true;
		return true;
	}

	bool TerrainEditorSessionCore::EndStroke(const uint64_t sequence)
	{
		if (sequence == 0u || sequence != _activeSequence)
		{
			return false;
		}

		_activeSequence = 0u;
		_preview.stroke_active = false;
		return true;
	}

	void TerrainEditorSessionCore::CancelStroke()
	{
		_activeSequence = 0u;
		_preview.stroke_active = false;
	}

	bool TerrainEditorSessionCore::ApplyBrushStroke(
		const uint64_t sequence,
		const AshEngine::TerrainBrushParameters& refParameters,
		const AshEngine::TerrainBrushMetric& refMetric,
		const std::vector<AshEngine::TerrainStrokeSample>& refRawSamples,
		std::vector<AshEngine::TerrainEditPatch>& refPatches,
		std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
		std::string* pError)
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || sequence == 0u || sequence != _activeSequence)
		{
			if (pError)
			{
				*pError = "Terrain brush does not match the active authoring sequence.";
			}
			return false;
		}

		const bool applied = AshEngine::apply_terrain_brush_stroke(
			*_optWorkingSet,
			refParameters,
			refMetric,
			refRawSamples,
			refPatches,
			refDirtyComponents,
			pError);
		_activeSequence = 0u;
		_preview.stroke_active = false;
		return applied;
	}

	bool TerrainEditorSessionCore::ApplyStrokePatches(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches,
		const AshEngine::TerrainEditPatchDirection eDirection,
		std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
		std::string* pError)
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || assetId == 0u || assetId != _assetId || !layerId.is_valid() ||
			refPatches.empty())
		{
			if (pError)
			{
				*pError = "Terrain stroke command does not match an open authoring session.";
			}
			return false;
		}

		for (const AshEngine::TerrainEditPatch& refPatch : refPatches)
		{
			if (refPatch.asset_id != assetId || refPatch.layer_id != layerId)
			{
				if (pError)
				{
					*pError = "Terrain stroke patch identity does not match its command.";
				}
				return false;
			}
		}

		return AshEngine::apply_terrain_edit_patches(
			*_optWorkingSet,
			refPatches,
			eDirection,
			refDirtyComponents,
			pError);
	}

	bool TerrainEditorSessionCore::ComposeComponents(
		const std::vector<AshEngine::TerrainComponentCoord>& refRequestedComponents,
		std::vector<AshEngine::TerrainDirtyComponentPayload>& refPayloads,
		std::string* pError) const
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet)
		{
			if (pError)
			{
				*pError = "Terrain composition requires an open authoring session.";
			}
			return false;
		}

		return AshEngine::compose_terrain_components(
			*_optWorkingSet,
			refRequestedComponents,
			refPayloads,
			pError);
	}

	bool TerrainEditorSessionCore::PublishDirtyComponents(
		const std::vector<AshEngine::TerrainDirtyComponentPayload>& refPayloads,
		const TerrainSnapshotPublisher& refPublisher,
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
		std::string* pError)
	{
		refSnapshot.reset();
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || !refPublisher)
		{
			if (pError)
			{
				*pError = "Terrain publication requires an open session and publisher.";
			}
			return false;
		}

		try
		{
			std::vector<std::shared_ptr<const AshEngine::TerrainComponentSnapshot>> previousComponents =
				_optWorkingSet->components;
			std::vector<AshEngine::TerrainComponentCoord> previousDirty =
				_optWorkingSet->dirty_components;
			std::shared_ptr<const AshEngine::TerrainAssetSnapshot> candidate{};
			if (!AshEngine::publish_terrain_working_set(
					*_optWorkingSet,
					refPayloads,
					candidate,
					pError))
			{
				return false;
			}

			bool published = false;
			try
			{
				published = refPublisher(_assetId, candidate);
			}
			catch (...)
			{
				_optWorkingSet->components.swap(previousComponents);
				_optWorkingSet->dirty_components.swap(previousDirty);
				if (pError)
				{
					*pError = "Terrain snapshot publisher raised an exception.";
				}
				return false;
			}

			if (!published)
			{
				_optWorkingSet->components.swap(previousComponents);
				_optWorkingSet->dirty_components.swap(previousDirty);
				if (pError && pError->empty())
				{
					*pError = "Terrain snapshot publisher rejected the current generation.";
				}
				return false;
			}

			refSnapshot = std::move(candidate);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			if (pError)
			{
				*pError = "Terrain publication allocation failed.";
			}
			return false;
		}
		catch (const std::length_error&)
		{
			if (pError)
			{
				*pError = "Terrain publication size is unsupported.";
			}
			return false;
		}
	}

	bool TerrainEditorSessionCore::IsDirty() const
	{
		return _optWorkingSet && _optWorkingSet->content_generation != _persistedContentGeneration;
	}

	void TerrainEditorSessionCore::SetPreviewQueryStatus(const AshEngine::TerrainQueryStatus eStatus)
	{
		_preview.query_status = eStatus;
	}
}
