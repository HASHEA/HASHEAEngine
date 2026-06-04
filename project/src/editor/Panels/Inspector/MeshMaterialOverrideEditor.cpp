#include "Panels/Inspector/MeshMaterialOverrideEditor.h"

#include "Function/Gui/UIContext.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Services/AssetDatabaseService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/InspectorAssetPathWidgets.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace AshEditor
{
	namespace
	{
		float CalcInspectorSmallButtonWidth(AshEngine::UIContext& refUi, const char* pVisibleLabel)
		{
			return refUi.calc_text_size(pVisibleLabel).x + refUi.get_style_frame_padding().x * 2.0f;
		}

		bool CanDrawInlineLabelButton(
			AshEngine::UIContext& refUi,
			const char* pVisibleLabel,
			const char* pButtonVisibleLabel)
		{
			const float fRequiredWidth =
				refUi.calc_text_size(pVisibleLabel).x +
				refUi.get_style_item_spacing().x +
				CalcInspectorSmallButtonWidth(refUi, pButtonVisibleLabel);
			return refUi.get_content_region_avail().x >= fRequiredWidth;
		}

		InspectorAssetPathFieldDesc MakeMaterialOverridePathDesc(
			const std::string& strPopupId,
			const std::string& strLabel)
		{
			InspectorAssetPathFieldDesc desc{};
			desc.pLabel = strLabel.c_str();
			desc.pPopupId = strPopupId.c_str();
			desc.pPickerTitle = "Select Material Asset";
			desc.pBrowseLabel = "Browse";
			desc.fieldSpec = {
				"Material Asset",
				"Material asset assigned to this mesh material slot override.",
				"Empty",
				"Material asset path",
				"Type a relative material path, browse, or drag-drop from the Asset Browser."
			};
			desc.browseSpec = {
				"Browse Material Assets",
				"Open the material picker and choose a material from the indexed asset database.",
				{},
				{},
				"Immediate action"
			};
			desc.vecAllowedAssetTypes = { AshEngine::AssetType::Material };
			desc.fBrowseButtonWidth = 64.0f;
			return desc;
		}

		bool TryGetMaterialOverrideValidationMessage(
			const AshEngine::MeshMaterialOverride& refMaterialOverride,
			const AssetDatabaseService* pAssetDatabaseService,
			std::string& strOutMessage)
		{
			InspectorAssetPathValidationDesc desc{};
			desc.strAssetPath = refMaterialOverride.material_path;
			desc.vecAllowedAssetTypes = { AshEngine::AssetType::Material };
			desc.pEmptyAssetPathMessage = "Choose a material asset or remove this material override.";
			desc.pMissingAssetMessage = "The typed material asset path is not present in the current asset database.";
			desc.pUnsupportedAssetTypeMessage = "The selected override asset must be a material resource.";
			desc.pLoadStateProblemPrefix = "The selected material asset is currently ";
			desc.bBlockWhenEmpty = true;
			return TryGetInspectorAssetPathValidationMessage(desc, pAssetDatabaseService, strOutMessage);
		}
	}

	MeshMaterialOverridesEditResult DrawMeshMaterialOverridesEditor(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::MeshComponent& refMesh)
	{
		InspectorPanelState& refState = refHost.AccessInspectorState();
		const InspectorPanelDeps& refDeps = refHost.AccessInspectorDeps();
		MeshMaterialOverridesEditResult result{};

		refUi.separator();
		const bool bDrawAddOverrideInline =
			CanDrawInlineLabelButton(refUi, "Material Overrides", "Add Override");
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Material Overrides");
		if (bDrawAddOverrideInline)
		{
			refUi.same_line();
		}
		if (DrawInspectorSmallActionButton(
			refUi,
			"Add Override##MeshMaterialOverride",
			{
				"Add Material Override",
				"Adds a material override entry. Pick a material asset before the entry is committed.",
				{},
				{},
				"Immediate action"
			}))
		{
			AshEngine::MeshMaterialOverride materialOverride{};
			materialOverride.material_slot = static_cast<uint32_t>(refMesh.material_overrides.size());
			refMesh.material_overrides.push_back(std::move(materialOverride));
			result.bCommitRequested = true;
		}

		if (refMesh.material_overrides.empty())
		{
			refUi.text_colored(GetEditorMutedTextColor(refUi), "No material overrides.");
			return result;
		}

		size_t uRemoveIndex = refMesh.material_overrides.size();
		for (size_t uIndex = 0; uIndex < refMesh.material_overrides.size(); ++uIndex)
		{
			AshEngine::MeshMaterialOverride& refMaterialOverride = refMesh.material_overrides[uIndex];
			refUi.push_id(static_cast<int32_t>(uIndex));
			refUi.separator();
			const bool bDrawRemoveInline =
				CanDrawInlineLabelButton(refUi, "Override 000", "Remove");
			refUi.text_colored(
				GetEditorMutedTextColor(refUi),
				"Override %llu",
				static_cast<unsigned long long>(uIndex + 1));
			if (bDrawRemoveInline)
			{
				refUi.same_line();
			}
			if (DrawInspectorSmallActionButton(
				refUi,
				"Remove##MaterialOverride",
				{
					"Remove Material Override",
					"Removes this material override from the mesh component draft.",
					{},
					{},
					"Immediate action"
				}))
			{
				uRemoveIndex = uIndex;
				refUi.pop_id();
				break;
			}

			int32_t iMaterialSlot = static_cast<int32_t>(std::min<uint32_t>(
				refMaterialOverride.material_slot,
				static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
			if (DrawInspectorInputIntField(
				refUi,
				"Slot",
				iMaterialSlot,
				{
					"Material Slot",
					"Zero-based material slot to override on the mesh.",
					"0",
					"[0, +inf)",
					"Negative values are clamped back to 0 before commit."
				}))
			{
				refMaterialOverride.material_slot = static_cast<uint32_t>(std::max(0, iMaterialSlot));
				result.bCommitRequested = true;
			}

			InspectorAssetPathWidgetState widgetState{};
			widgetState.pVecRecentPaths = &refState.vecRecentMaterialPaths;
			widgetState.pStrSearch = &refState.strMaterialAssetPickerSearch;
			const std::string strPopupId =
				std::string("MaterialOverrideAssetPickerPopup") + std::to_string(uIndex);
			const std::string strLabel = std::string("Material##MaterialOverride") + std::to_string(uIndex);
			InspectorAssetPathFieldDesc desc = MakeMaterialOverridePathDesc(strPopupId, strLabel);
			result.bCommitRequested = DrawInspectorAssetPathField(
				refUi,
				refMaterialOverride.material_path,
				desc,
				widgetState,
				refDeps.pAssetDatabaseService,
				refDeps.pDragDropTransferService) || result.bCommitRequested;

			std::string strValidationMessage{};
			const bool bOverrideBlocksCommit = TryGetMaterialOverrideValidationMessage(
				refMaterialOverride,
				refDeps.pAssetDatabaseService,
				strValidationMessage);
			if (!strValidationMessage.empty())
			{
				refUi.text_colored(GetEditorWarningTextColor(refUi), "%s", strValidationMessage.c_str());
			}
			result.bBlocksCommit = result.bBlocksCommit || bOverrideBlocksCommit;
			refUi.pop_id();
		}

		if (uRemoveIndex < refMesh.material_overrides.size())
		{
			refMesh.material_overrides.erase(
				refMesh.material_overrides.begin() + static_cast<ptrdiff_t>(uRemoveIndex));
			result.bCommitRequested = true;
		}
		return result;
	}
}
