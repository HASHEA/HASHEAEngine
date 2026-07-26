#pragma once

#include "Core/EditorPanel.h"
#include "Core/PanelDeps/VegetationPanelDeps.h"
#include "Services/VegetationEditorService.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace AshEditor
{
	class VegetationPanel final : public EditorPanel
	{
	public:
		explicit VegetationPanel(VegetationPanelDeps deps = {});

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		const VegetationEditorStatusSnapshot& GetCachedStatus() const
		{
			return _status;
		}

	private:
		struct ConfirmationRevision
		{
			std::filesystem::path source_path{};
			uint64_t content_generation = 0;
			VegetationSessionState session =
				VegetationSessionState::Failed;
		};

		struct RemoveConfirmation
		{
			ConfirmationRevision revision{};
			AshEngine::VegetationId species_id{};
		};

		void RefreshStatus();
		void ValidateSelectedSpecies();
		bool MatchesCurrentRevision(
			const ConfirmationRevision& revision) const;
		bool PaletteContainsSpecies(
			const AshEngine::VegetationId& speciesId) const;
		void CompleteIntent(
			bool succeeded,
			const std::string& previousDetail,
			const char* pFallback);

		VegetationPanelDeps _deps{};
		VegetationEditorStatusSnapshot _status{};
		std::string _strLayerPath{};
		std::string _strLayerSeed{ "0" };
		std::string _strSpeciesPath{};
		std::string _strLocalError{};
		std::optional<AshEngine::VegetationId> _selectedSpecies{};
		std::optional<RemoveConfirmation> _pendingRemove{};
		std::optional<ConfirmationRevision> _pendingReloadDiscard{};
		bool _bOpenRemoveConfirmation = false;
		bool _bOpenReloadDiscardConfirmation = false;
	};
}
