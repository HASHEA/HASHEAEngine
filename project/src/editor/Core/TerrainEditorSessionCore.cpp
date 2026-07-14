#include "Core/TerrainEditorSessionCore.h"

#include "Function/Asset/TerrainComposition.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>

namespace AshEditor
{
	AshEngine::TerrainAssetId TerrainEditorSessionCore::GetAssetId() const
	{
		return _assetId;
	}

	AshEngine::TerrainLayerId TerrainEditorSessionCore::GetSelectedLayerId() const
	{
		return _selectedLayerId;
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
			_selectedLayerId = {};
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
		_selectedLayerId = _optWorkingSet->edit_layers.empty()
			? AshEngine::TerrainLayerId{} : _optWorkingSet->edit_layers.front().id;
		_preview = {};
		_preview.query_status = AshEngine::TerrainQueryStatus::Ready;
		RefreshSelectedLayerPreview();
		return true;
	}

	void TerrainEditorSessionCore::Close()
	{
		_assetId = 0u;
		_activeSequence = 0u;
		_persistedContentGeneration = 0u;
		_selectedLayerId = {};
		_optWorkingSet.reset();
		_preview = {};
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorSessionCore::GetWorkingSet() const
	{
		return _optWorkingSet ? &*_optWorkingSet : nullptr;
	}

	bool TerrainEditorSessionCore::SelectLayer(const AshEngine::TerrainLayerId layerId)
	{
		if (!_optWorkingSet || _activeSequence != 0u || !layerId.is_valid())
		{
			return false;
		}
		const auto selected = std::find_if(
			_optWorkingSet->edit_layers.begin(),
			_optWorkingSet->edit_layers.end(),
			[layerId](const AshEngine::TerrainEditLayer& refLayer)
			{
				return refLayer.id == layerId;
			});
		if (selected == _optWorkingSet->edit_layers.end())
		{
			return false;
		}

		_selectedLayerId = layerId;
		RefreshSelectedLayerPreview();
		return true;
	}

	bool TerrainEditorSessionCore::BeginStroke(const uint64_t sequence)
	{
		if (!_optWorkingSet || sequence == 0u || _activeSequence != 0u ||
			_preview.query_status != AshEngine::TerrainQueryStatus::Ready ||
			!_selectedLayerId.is_valid())
		{
			return false;
		}
		const auto selected = std::find_if(
			_optWorkingSet->edit_layers.begin(),
			_optWorkingSet->edit_layers.end(),
			[this](const AshEngine::TerrainEditLayer& refLayer)
			{
				return refLayer.id == _selectedLayerId;
			});
		if (selected == _optWorkingSet->edit_layers.end() || selected->locked)
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

	bool TerrainEditorSessionCore::ApplyLayerStackEdit(
		const AshEngine::TerrainLayerStackEdit& refEdit,
		AshEngine::TerrainLayerStackPatch& refPatch,
		std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
		std::string* pError)
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || _activeSequence != 0u)
		{
			if (pError)
			{
				*pError = "Terrain layer edit requires an idle open authoring session.";
			}
			return false;
		}

		if (!AshEngine::apply_terrain_layer_stack_edit(
				*_optWorkingSet,
				refEdit,
				refPatch,
				refDirtyComponents,
				pError))
		{
			return false;
		}
		if (refPatch.has_change())
		{
			SelectAfterLayerTransition(refPatch, AshEngine::TerrainEditPatchDirection::Redo);
		}
		return true;
	}

	bool TerrainEditorSessionCore::ApplyLayerStackPatch(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerStackPatch& refPatch,
		const AshEngine::TerrainEditPatchDirection eDirection,
		const AshEngine::TerrainLayerId selectedLayerId,
		std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
		std::string* pError)
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || _activeSequence != 0u || assetId == 0u ||
			assetId != _assetId || refPatch.asset_id != assetId)
		{
			if (pError)
			{
				*pError = "Terrain layer command does not match an idle open authoring session.";
			}
			return false;
		}
		const std::vector<AshEngine::TerrainLayerId>& targetOrder =
			eDirection == AshEngine::TerrainEditPatchDirection::Undo
			? refPatch.before_order : refPatch.after_order;
		const bool validSelection = targetOrder.empty()
			? !selectedLayerId.is_valid()
			: selectedLayerId.is_valid() &&
				std::find(targetOrder.begin(), targetOrder.end(), selectedLayerId) != targetOrder.end();
		if (!validSelection)
		{
			if (pError)
			{
				*pError = "Terrain layer command selection does not exist in the target layer order.";
			}
			return false;
		}

		if (!AshEngine::apply_terrain_layer_stack_patch(
				*_optWorkingSet,
				refPatch,
				eDirection,
				refDirtyComponents,
				pError))
		{
			return false;
		}
		_selectedLayerId = selectedLayerId;
		RefreshSelectedLayerPreview();
		return true;
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

	bool TerrainEditorSessionCore::SetViewportPreview(
		const TerrainViewportPreviewState& refPreview)
	{
		if (_preview.query_status != AshEngine::TerrainQueryStatus::Ready)
		{
			return false;
		}

		switch (refPreview.query_status)
		{
		case AshEngine::TerrainQueryStatus::Ready:
			if (!refPreview.has_world_position)
			{
				return false;
			}
			break;
		case AshEngine::TerrainQueryStatus::Pending:
		case AshEngine::TerrainQueryStatus::Failed:
		{
			TerrainViewportPreviewState retained = _preview.viewport;
			retained.query_status = refPreview.query_status;
			if (!retained.has_world_position)
			{
				retained = {};
				retained.query_status = refPreview.query_status;
				_preview.viewport = retained;
				return true;
			}
			if (!std::isfinite(refPreview.radius_meters) ||
				refPreview.radius_meters <= 0.0f)
			{
				return false;
			}
			// A non-ready query may recolor the last service-owned brush radius,
			// but it cannot replace the last validated same-session hit anchor.
			retained.radius_meters = refPreview.radius_meters;
			_preview.viewport = retained;
			return true;
		}
		case AshEngine::TerrainQueryStatus::Outside:
			_preview.viewport = {};
			return true;
		default:
			return false;
		}

		TerrainViewportPreviewState candidate = refPreview;
		const bool finiteCenter =
			std::isfinite(candidate.center_ws.x) &&
			std::isfinite(candidate.center_ws.y) &&
			std::isfinite(candidate.center_ws.z);
		const bool finiteNormal =
			std::isfinite(candidate.normal_ws.x) &&
			std::isfinite(candidate.normal_ws.y) &&
			std::isfinite(candidate.normal_ws.z);
		const float normalLength = glm::length(candidate.normal_ws);
		if (candidate.terrain_entity_id == 0u ||
			!finiteCenter || !finiteNormal || !std::isfinite(normalLength) ||
			normalLength <= 1.0e-6f ||
			!std::isfinite(candidate.radius_meters) || candidate.radius_meters <= 0.0f)
		{
			return false;
		}

		candidate.normal_ws /= normalLength;
		_preview.viewport = candidate;
		return true;
	}

	void TerrainEditorSessionCore::ClearViewportPreview()
	{
		_preview.viewport = {};
	}

	void TerrainEditorSessionCore::SetPreviewQueryStatus(const AshEngine::TerrainQueryStatus eStatus)
	{
		_preview.query_status = eStatus;
		if (eStatus != AshEngine::TerrainQueryStatus::Ready)
		{
			ClearViewportPreview();
		}
	}

	void TerrainEditorSessionCore::SelectAfterLayerTransition(
		const AshEngine::TerrainLayerStackPatch& refPatch,
		const AshEngine::TerrainEditPatchDirection eDirection)
	{
		const std::vector<AshEngine::TerrainLayerId>& targetOrder =
			eDirection == AshEngine::TerrainEditPatchDirection::Undo
			? refPatch.before_order : refPatch.after_order;
		const std::vector<AshEngine::TerrainLayerId>& sourceOrder =
			eDirection == AshEngine::TerrainEditPatchDirection::Undo
			? refPatch.after_order : refPatch.before_order;
		const auto target = std::find(targetOrder.begin(), targetOrder.end(), refPatch.layer_id);
		if (target != targetOrder.end())
		{
			_selectedLayerId = refPatch.layer_id;
		}
		else if (!targetOrder.empty())
		{
			const auto source = std::find(sourceOrder.begin(), sourceOrder.end(), refPatch.layer_id);
			const size_t sourceIndex = source == sourceOrder.end()
				? 0u : static_cast<size_t>(source - sourceOrder.begin());
			_selectedLayerId = targetOrder[std::min(sourceIndex, targetOrder.size() - 1u)];
		}
		else
		{
			_selectedLayerId = {};
		}
		RefreshSelectedLayerPreview();
	}

	void TerrainEditorSessionCore::RefreshSelectedLayerPreview()
	{
		_preview.layer_locked = false;
		if (!_optWorkingSet || !_selectedLayerId.is_valid())
		{
			return;
		}
		const auto selected = std::find_if(
			_optWorkingSet->edit_layers.begin(),
			_optWorkingSet->edit_layers.end(),
			[this](const AshEngine::TerrainEditLayer& refLayer)
			{
				return refLayer.id == _selectedLayerId;
			});
		if (selected == _optWorkingSet->edit_layers.end())
		{
			_selectedLayerId = {};
			return;
		}
		_preview.layer_locked = selected->locked;
	}
}
