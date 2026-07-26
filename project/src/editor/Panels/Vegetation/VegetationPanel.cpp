#include "Panels/Vegetation/VegetationPanel.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr char kRemoveSpeciesPopupId[] =
			"Remove Vegetation Species";
		constexpr char kReloadDiscardPopupId[] =
			"Discard Vegetation Layer Changes";

		const char* SessionLabel(const VegetationSessionState state)
		{
			switch (state)
			{
			case VegetationSessionState::Clean:
				return "Clean";
			case VegetationSessionState::Dirty:
				return "Dirty";
			case VegetationSessionState::Saving:
				return "Saving";
			case VegetationSessionState::SourceChanged:
				return "Source Changed";
			case VegetationSessionState::Failed:
			default:
				return "Failed";
			}
		}

		const char* OperationLabel(const VegetationOperationState state)
		{
			switch (state)
			{
			case VegetationOperationState::Idle:
				return "Idle";
			case VegetationOperationState::Pending:
				return "Pending";
			case VegetationOperationState::Running:
				return "Running";
			case VegetationOperationState::Succeeded:
				return "Succeeded";
			case VegetationOperationState::DirtyConflict:
				return "Dirty Conflict";
			case VegetationOperationState::SourceChanged:
				return "Source Changed";
			case VegetationOperationState::AlreadyExists:
				return "Already Exists";
			case VegetationOperationState::Cancelled:
				return "Cancelled";
			case VegetationOperationState::TimedOut:
				return "Timed Out";
			case VegetationOperationState::Failed:
			default:
				return "Failed";
			}
		}

		bool ParseSeed(const std::string& text, uint64_t& outSeed)
		{
			if (text.empty())
			{
				return false;
			}
			uint64_t seed = 0;
			const std::from_chars_result parsed = std::from_chars(
				text.data(),
				text.data() + text.size(),
				seed);
			if (parsed.ec != std::errc{} ||
				parsed.ptr != text.data() + text.size())
			{
				return false;
			}
			outSeed = seed;
			return true;
		}

		std::string SpeciesIdLabel(const AshEngine::VegetationId& id)
		{
			static constexpr char kHex[] = "0123456789abcdef";
			std::string label(id.size() * 2, '0');
			for (size_t index = 0; index < id.size(); ++index)
			{
				label[index * 2] = kHex[id[index] >> 4];
				label[index * 2 + 1] = kHex[id[index] & 0x0f];
			}
			return label;
		}
	}

	VegetationPanel::VegetationPanel(VegetationPanelDeps deps)
		: EditorPanel(
			EditorPanelIds::Vegetation,
			EditorWindowTitles::Vegetation)
		, _deps(deps)
	{
	}

	void VegetationPanel::OnAttach()
	{
		RefreshStatus();
	}

	void VegetationPanel::OnDetach()
	{
		_deps = {};
		_status = {};
		_strLocalError.clear();
		_selectedSpecies.reset();
		_pendingRemove.reset();
		_pendingReloadDiscard.reset();
		_bOpenRemoveConfirmation = false;
		_bOpenReloadDiscardConfirmation = false;
	}

	void VegetationPanel::OnUpdate()
	{
		RefreshStatus();
	}

	void VegetationPanel::OnGui(
		const EditorFrameContext& refFrameContext)
	{
		if (!BeginPanelWindow(refFrameContext))
		{
			EndPanelWindow(refFrameContext);
			return;
		}
		if (!refFrameContext.pUiContext)
		{
			EndPanelWindow(refFrameContext);
			return;
		}

		RefreshStatus();
		AshEngine::UIContext& ui = *refFrameContext.pUiContext;
		VegetationEditorService* const service =
			_deps.pVegetationService;
		if (!service)
		{
			ui.text_unformatted(
				"Vegetation authoring service is unavailable.");
			EndPanelWindow(refFrameContext);
			return;
		}

		ui.text("Session: %s", SessionLabel(_status.session));
		ui.same_line();
		ui.text("Operation: %s", OperationLabel(_status.operation));
		ui.text_wrapped(
			"Layer: %s",
			_status.source_path.empty()
				? "-"
				: _status.source_path.generic_string().c_str());
		ui.text(
			"Generation: %llu (saved %llu)",
			static_cast<unsigned long long>(
				_status.content_generation),
			static_cast<unsigned long long>(
				_status.persisted_generation));
		if (!_status.detail.empty())
		{
			ui.text_wrapped("%s", _status.detail.c_str());
		}
		if (!_strLocalError.empty())
		{
			ui.text_wrapped(
				"Action failed: %s",
				_strLocalError.c_str());
		}

		ui.separator();
		ui.text_unformatted("Layer");
		ui.input_text("Layer Asset", _strLayerPath);
		ui.input_text("Seed", _strLayerSeed);
		uint64_t layerSeed = 0;
		const bool validSeed = ParseSeed(_strLayerSeed, layerSeed);

		ui.begin_disabled(
			!_status.capabilities.can_create ||
			_strLayerPath.empty() ||
			!validSeed);
		if (ui.button("New Layer"))
		{
			const std::string previousDetail = _status.detail;
			const bool succeeded =
				service->CreateLayer(_strLayerPath, layerSeed);
			CompleteIntent(
				succeeded,
				previousDetail,
				"Could not create the Layer. Use a new "
				".AshVegetationLayer path inside the asset root.");
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(
			!_status.capabilities.can_load ||
			_strLayerPath.empty());
		if (ui.button("Load"))
		{
			const std::string previousDetail = _status.detail;
			const bool succeeded =
				service->OpenLayer(_strLayerPath);
			CompleteIntent(
				succeeded,
				previousDetail,
				"Could not load the Layer. Choose an existing "
				".AshVegetationLayer asset.");
		}
		ui.end_disabled();
		if (!validSeed)
		{
			ui.text_wrapped(
				"Seed must be an unsigned 64-bit decimal value.");
		}

		ui.separator();
		ui.text_unformatted("Palette");
		if (!_status.palette || _status.palette->empty())
		{
			ui.text_unformatted("No Species in this Layer.");
		}
		else
		{
			for (size_t index = 0;
				index < _status.palette->size();
				++index)
			{
				const VegetationPaletteViewEntry& entry =
					(*_status.palette)[index];
				ui.push_id(static_cast<int32_t>(index));
				const std::string speciesId =
					SpeciesIdLabel(entry.species_id);
				const std::string label =
					(entry.display_name.empty()
						? entry.species_path.generic_string()
						: entry.display_name) +
					"##VegetationSpecies";
				const bool selected =
					_selectedSpecies.has_value() &&
					*_selectedSpecies == entry.species_id;
				if (ui.selectable(label.c_str(), selected))
				{
					_selectedSpecies = entry.species_id;
				}
				ui.text_wrapped(
					"%s | %s",
					speciesId.c_str(),
					entry.species_path.generic_string().c_str());
				if (!entry.error.empty())
				{
					ui.text_wrapped("%s", entry.error.c_str());
				}
				ui.pop_id();
			}
		}

		ui.input_text("Species Asset", _strSpeciesPath);
		const bool hasSelectedSpecies =
			_selectedSpecies.has_value();
		const bool canEditPalette =
			_status.capabilities.can_edit_palette;
		ui.begin_disabled(
			!canEditPalette || _strSpeciesPath.empty());
		if (ui.button("Add Species"))
		{
			const std::string previousDetail = _status.detail;
			const bool succeeded =
				service->AddPaletteSpecies(_strSpeciesPath);
			CompleteIntent(
				succeeded,
				previousDetail,
				"Could not add Species. Choose an existing valid "
				".AshVegetation asset.");
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(
			!canEditPalette ||
			!hasSelectedSpecies ||
			_strSpeciesPath.empty());
		if (ui.button("Replace Species"))
		{
			const std::string previousDetail = _status.detail;
			const bool succeeded =
				service->ReplacePaletteSpecies(
					*_selectedSpecies,
					_strSpeciesPath);
			CompleteIntent(
				succeeded,
				previousDetail,
				"Could not replace Species. Refresh the palette and "
				"choose an existing valid .AshVegetation asset.");
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(
			!canEditPalette || !hasSelectedSpecies);
		if (ui.button("Remove Species"))
		{
			_pendingRemove = RemoveConfirmation{
				{
					_status.source_path,
					_status.content_generation,
					_status.session
				},
				*_selectedSpecies
			};
			_bOpenRemoveConfirmation = true;
		}
		ui.end_disabled();

		if (_bOpenRemoveConfirmation)
		{
			ui.open_popup(kRemoveSpeciesPopupId);
			_bOpenRemoveConfirmation = false;
		}
		if (ui.begin_popup_modal(
			kRemoveSpeciesPopupId,
			nullptr,
			AshEngine::UIWindowFlagBits::AlwaysAutoResize))
		{
			ui.text_wrapped(
				"Remove this Species and explicitly clear every "
				"referenced weight plane?");
			const bool removeStillCurrent =
				_pendingRemove.has_value() &&
				MatchesCurrentRevision(
					_pendingRemove->revision) &&
				PaletteContainsSpecies(
					_pendingRemove->species_id);
			if (!removeStillCurrent)
			{
				ui.text_wrapped(
					"The Layer or palette changed after this "
					"confirmation opened. Cancel and select the "
					"Species again.");
			}
			ui.begin_disabled(!removeStillCurrent);
			if (ui.button("Remove and Clear Weights"))
			{
				const RemoveConfirmation pending =
					*_pendingRemove;
				RefreshStatus();
				if (!MatchesCurrentRevision(pending.revision) ||
					!PaletteContainsSpecies(pending.species_id))
				{
					_strLocalError =
						"Remove was cancelled because the Layer or "
						"palette changed. Select the Species and retry.";
				}
				else
				{
					const std::string previousDetail =
						_status.detail;
					const bool succeeded =
						service->RemovePaletteSpecies(
							pending.species_id,
							true);
					CompleteIntent(
						succeeded,
						previousDetail,
						"Could not remove Species and clear its "
						"weight planes. Refresh the palette and retry.");
				}
				_pendingRemove.reset();
				ui.close_current_popup();
			}
			ui.end_disabled();
			ui.same_line();
			if (ui.button("Cancel"))
			{
				_pendingRemove.reset();
				ui.close_current_popup();
			}
			ui.end_popup();
		}

		ui.separator();
		ui.begin_disabled(!_status.capabilities.can_save);
		if (ui.button("Save"))
		{
			const std::string previousDetail = _status.detail;
			const bool succeeded = service->RequestSave(
				std::chrono::steady_clock::now());
			CompleteIntent(
				succeeded,
				previousDetail,
				"Could not save the Layer. Resolve the current "
				"operation state and retry.");
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(!_status.capabilities.can_reload);
		if (ui.button("Reload"))
		{
			if (_status.session ==
				VegetationSessionState::Dirty)
			{
				_pendingReloadDiscard = ConfirmationRevision{
					_status.source_path,
					_status.content_generation,
					_status.session
				};
				_bOpenReloadDiscardConfirmation = true;
			}
			else
			{
				const std::string previousDetail =
					_status.detail;
				const bool succeeded =
					service->RequestReload(
						std::chrono::steady_clock::now());
				CompleteIntent(
					succeeded,
					previousDetail,
					"Could not reload the Layer. Resolve the current "
					"operation state and retry.");
			}
		}
		ui.end_disabled();

		if (_bOpenReloadDiscardConfirmation)
		{
			ui.open_popup(kReloadDiscardPopupId);
			_bOpenReloadDiscardConfirmation = false;
		}
		if (ui.begin_popup_modal(
			kReloadDiscardPopupId,
			nullptr,
			AshEngine::UIWindowFlagBits::AlwaysAutoResize))
		{
			ui.text_wrapped(
				"Reload this Layer and permanently discard all "
				"unsaved vegetation changes?");
			const bool reloadStillCurrent =
				_pendingReloadDiscard.has_value() &&
				_pendingReloadDiscard->session ==
					VegetationSessionState::Dirty &&
				MatchesCurrentRevision(
					*_pendingReloadDiscard);
			if (!reloadStillCurrent)
			{
				ui.text_wrapped(
					"The Layer changed after this confirmation "
					"opened. Cancel and request Reload again.");
			}
			ui.begin_disabled(!reloadStillCurrent);
			if (ui.button("Reload and Discard Changes"))
			{
				const ConfirmationRevision pending =
					*_pendingReloadDiscard;
				RefreshStatus();
				if (pending.session !=
						VegetationSessionState::Dirty ||
					!MatchesCurrentRevision(pending))
				{
					_strLocalError =
						"Reload discard was cancelled because the "
						"Layer changed. Request Reload again.";
				}
				else
				{
					const std::string previousDetail =
						_status.detail;
					const bool succeeded =
						service->RequestReloadDiscard(
							true,
							std::chrono::steady_clock::now());
					CompleteIntent(
						succeeded,
						previousDetail,
						"Could not reload and discard changes. "
						"Resolve the current operation state and retry.");
				}
				_pendingReloadDiscard.reset();
				ui.close_current_popup();
			}
			ui.end_disabled();
			ui.same_line();
			if (ui.button("Cancel"))
			{
				_pendingReloadDiscard.reset();
				ui.close_current_popup();
			}
			ui.end_popup();
		}

		ui.separator();
		ui.text_unformatted("Surface Authoring");
		// Phase 2 intentionally has no product surface provider or exact-action
		// binding resolver. These intents stay disabled until a later
		// integration can resolve a nonzero binding for the exact click.
		ui.begin_disabled(true);
		ui.button("Paint");
		ui.same_line();
		ui.button("Erase");
		ui.same_line();
		ui.button("Bake");
		ui.end_disabled();
		if (!_status.capabilities.surface_unavailable_reason.empty())
		{
			ui.text_wrapped(
				"%s",
				_status.capabilities.surface_unavailable_reason.c_str());
		}

		EndPanelWindow(refFrameContext);
	}

	void VegetationPanel::RefreshStatus()
	{
		if (!_deps.pVegetationService)
		{
			return;
		}
		_status = _deps.pVegetationService->GetStatusSnapshot();
		ValidateSelectedSpecies();
	}

	void VegetationPanel::ValidateSelectedSpecies()
	{
		if (!_selectedSpecies || !_status.palette)
		{
			return;
		}
		const auto found = std::find_if(
			_status.palette->begin(),
			_status.palette->end(),
			[this](const VegetationPaletteViewEntry& entry)
			{
				return entry.species_id == *_selectedSpecies;
			});
		if (found == _status.palette->end())
		{
			_selectedSpecies.reset();
		}
	}

	bool VegetationPanel::MatchesCurrentRevision(
		const ConfirmationRevision& revision) const
	{
		return _status.source_path == revision.source_path &&
			_status.content_generation ==
				revision.content_generation &&
			_status.session == revision.session;
	}

	bool VegetationPanel::PaletteContainsSpecies(
		const AshEngine::VegetationId& speciesId) const
	{
		return _status.palette &&
			std::any_of(
				_status.palette->begin(),
				_status.palette->end(),
				[&speciesId](
					const VegetationPaletteViewEntry& entry)
				{
					return entry.species_id == speciesId;
				});
	}

	void VegetationPanel::CompleteIntent(
		const bool succeeded,
		const std::string& previousDetail,
		const char* const pFallback)
	{
		RefreshStatus();
		if (succeeded)
		{
			_strLocalError.clear();
			return;
		}
		if (_status.detail.empty() ||
			_status.detail == previousDetail)
		{
			_strLocalError = pFallback
				? pFallback
				: "The vegetation action was rejected.";
			return;
		}
		_strLocalError.clear();
	}
}
