#include "Widgets/InspectorAssetPathWidgets.h"

#include "Base/hlog.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace AshEditor
{
	namespace
	{
		constexpr size_t kInspectorAssetPathRecentLimit = 10;

		bool IsAllowedAssetType(
			const AshEngine::AssetType eAssetType,
			const std::vector<AshEngine::AssetType>& refAllowedAssetTypes)
		{
			if (refAllowedAssetTypes.empty())
			{
				return true;
			}

			return std::find(refAllowedAssetTypes.begin(), refAllowedAssetTypes.end(), eAssetType) !=
				refAllowedAssetTypes.end();
		}

		float CalcInspectorVisibleLabelWidth(AshEngine::UIContext& refUi, const char* pLabel)
		{
			if (!pLabel || pLabel[0] == '\0' || std::string_view(pLabel).rfind("##", 0) == 0)
			{
				return 0.0f;
			}

			const std::string_view svLabel{ pLabel };
			const size_t uHiddenIdOffset = svLabel.find("##");
			const std::string strVisibleLabel = uHiddenIdOffset == std::string_view::npos
				? std::string(svLabel)
				: std::string(svLabel.substr(0, uHiddenIdOffset));
			return strVisibleLabel.empty() ? 0.0f : refUi.calc_text_size(strVisibleLabel.c_str()).x;
		}

		bool IsAssetPathAllowed(
			const std::string& strPath,
			const InspectorAssetPathFieldDesc& refDesc,
			const AssetDatabaseService* pAssetDatabaseService)
		{
			if (strPath.empty() || refDesc.vecAllowedAssetTypes.empty() || !pAssetDatabaseService)
			{
				return true;
			}

			const AshEngine::AssetInfo* pAssetInfo = pAssetDatabaseService->FindByPath(strPath);
			return !pAssetInfo || IsAllowedAssetType(pAssetInfo->type, refDesc.vecAllowedAssetTypes);
		}

		void PushRecentInspectorAssetPath(std::vector<std::string>& refRecentPaths, const std::string& strPath)
		{
			if (strPath.empty())
			{
				return;
			}

			refRecentPaths.erase(
				std::remove(refRecentPaths.begin(), refRecentPaths.end(), strPath),
				refRecentPaths.end());
			refRecentPaths.insert(refRecentPaths.begin(), strPath);
			if (refRecentPaths.size() > kInspectorAssetPathRecentLimit)
			{
				refRecentPaths.resize(kInspectorAssetPathRecentLimit);
			}
		}

		void ClearInspectorAssetSearch(InspectorAssetPathWidgetState& refState)
		{
			if (refState.pStrSearch)
			{
				refState.pStrSearch->clear();
			}
		}

		bool ApplyInspectorAssetPath(
			std::string& strPath,
			const std::string& strNewPath,
			const InspectorAssetPathFieldDesc& refDesc,
			InspectorAssetPathWidgetState& refState,
			const AssetDatabaseService* pAssetDatabaseService)
		{
			if (!IsAssetPathAllowed(strNewPath, refDesc, pAssetDatabaseService))
			{
				HLogWarning("Inspector rejected asset path '{}' because its asset type is not allowed here.", strNewPath);
				return false;
			}

			strPath = strNewPath;
			if (refState.pVecRecentPaths)
			{
				PushRecentInspectorAssetPath(*refState.pVecRecentPaths, strPath);
			}
			ClearInspectorAssetSearch(refState);
			return true;
		}

		bool AcceptInspectorAssetPathDrop(
			AshEngine::UIContext& refUi,
			std::string& strPath,
			const InspectorAssetPathFieldDesc& refDesc,
			InspectorAssetPathWidgetState& refState,
			const AssetDatabaseService* pAssetDatabaseService,
			const DragDropTransferService* pDragDropTransferService)
		{
			if (!pDragDropTransferService || !refUi.begin_drag_drop_target())
			{
				return false;
			}

			bool bChanged = false;
			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::Asset);
			if (payload.is_delivery && payload.data && payload.data_size == sizeof(DragDropTransferId))
			{
				DragDropTransferId uTransferId = 0;
				std::memcpy(&uTransferId, payload.data, sizeof(DragDropTransferId));
				const DragDropTransferData* pData = pDragDropTransferService->Resolve(uTransferId);
				if (pData && pData->extraData.has_value())
				{
					try
					{
						const std::string strDroppedPath = std::any_cast<std::string>(pData->extraData);
						bChanged = ApplyInspectorAssetPath(
							strPath,
							strDroppedPath,
							refDesc,
							refState,
							pAssetDatabaseService);
					}
					catch (const std::bad_any_cast&)
					{
						HLogWarning("Inspector rejected asset drag-drop payload because the asset path type was invalid.");
					}
				}
			}
			refUi.end_drag_drop_target();
			return bChanged;
		}

		bool DrawInspectorAssetPathDropZone(
			AshEngine::UIContext& refUi,
			std::string& strPath,
			const InspectorAssetPathFieldDesc& refDesc,
			InspectorAssetPathWidgetState& refState,
			const AssetDatabaseService* pAssetDatabaseService,
			const DragDropTransferService* pDragDropTransferService)
		{
			if (!refDesc.bDrawDropZone)
			{
				return false;
			}

			refUi.push_style_color(AshEngine::UIStyleColorKind::Button, GetEditorDropZoneFillColor(refUi));
			refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, GetEditorDropZoneHoverColor(refUi));
			refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, GetEditorDropZoneActiveColor(refUi));
			const char* pLabel = strPath.empty() ? refDesc.pDropLabelEmpty : refDesc.pDropLabelReplace;
			if (!pLabel)
			{
				pLabel = "Drop asset here";
			}
			if (refUi.button(pLabel, { refUi.get_content_region_avail().x, 24.0f }) && refDesc.pPopupId)
			{
				refUi.open_popup(refDesc.pPopupId);
			}
			DrawInspectorFieldTooltip(refUi, refDesc.dropZoneSpec);
			refUi.pop_style_color(3);

			const AshEngine::UIRect rectDropHint = refUi.get_item_rect();
			refUi.draw_window_rect(rectDropHint, GetEditorDropZoneBorderColor(refUi), 4.0f, 1.0f);
			return AcceptInspectorAssetPathDrop(
				refUi,
				strPath,
				refDesc,
				refState,
				pAssetDatabaseService,
				pDragDropTransferService);
		}

		bool DrawInspectorAssetPathPicker(
			AshEngine::UIContext& refUi,
			std::string& strPath,
			const InspectorAssetPathFieldDesc& refDesc,
			InspectorAssetPathWidgetState& refState,
			const AssetDatabaseService* pAssetDatabaseService)
		{
			if (!refDesc.pPopupId || !refUi.begin_popup(refDesc.pPopupId))
			{
				return false;
			}

			bool bChanged = false;
			refUi.text_unformatted(refDesc.pPickerTitle ? refDesc.pPickerTitle : "Select Asset");
			refUi.separator();
			if (refState.pStrSearch)
			{
				refUi.set_next_item_width(280.0f);
				refUi.input_text("##PickerSearch", *refState.pStrSearch);
			}

			if (refState.pVecRecentPaths && !refState.pVecRecentPaths->empty())
			{
				refUi.text_colored(GetEditorMutedTextColor(refUi), "Recent");
				for (const std::string& strRecent : *refState.pVecRecentPaths)
				{
					if (refUi.selectable(strRecent.c_str()))
					{
						bChanged = ApplyInspectorAssetPath(strPath, strRecent, refDesc, refState, pAssetDatabaseService);
						refUi.close_current_popup();
					}
				}
				refUi.separator();
			}

			refUi.text_colored(GetEditorMutedTextColor(refUi), "Assets");
			if (pAssetDatabaseService)
			{
				if (refUi.begin_child("AssetPickerList", refDesc.pickerSize))
				{
					const std::string strEmptySearch{};
					const std::string& refSearch = refState.pStrSearch ? *refState.pStrSearch : strEmptySearch;
					const std::vector<AshEngine::AssetInfo>& vecAssets = pAssetDatabaseService->GetItems();
					for (const AshEngine::AssetInfo& refAsset : vecAssets)
					{
						if (!IsAllowedAssetType(refAsset.type, refDesc.vecAllowedAssetTypes))
						{
							continue;
						}

						const std::string strRelPath = refAsset.relative_path.generic_string();
						if (!refSearch.empty() &&
							strRelPath.find(refSearch) == std::string::npos &&
							refAsset.name.find(refSearch) == std::string::npos)
						{
							continue;
						}
						if (refUi.selectable(strRelPath.c_str()))
						{
							bChanged = ApplyInspectorAssetPath(
								strPath,
								strRelPath,
								refDesc,
								refState,
								pAssetDatabaseService);
							refUi.close_current_popup();
						}
					}
				}
				refUi.end_child();
			}
			else
			{
				refUi.text_colored(GetEditorWarningTextColor(refUi), "Asset database not available.");
			}

			refUi.end_popup();
			return bChanged;
		}
	}

	bool DrawInspectorAssetPathField(
		AshEngine::UIContext& refUi,
		std::string& strPath,
		const InspectorAssetPathFieldDesc& refDesc,
		InspectorAssetPathWidgetState& refState,
		const AssetDatabaseService* pAssetDatabaseService,
		const DragDropTransferService* pDragDropTransferService)
	{
		bool bChanged = false;
		const float fAvail = refUi.get_content_region_avail().x;
		const float fSpacing = 4.0f;
		constexpr float kMinimumInspectorAssetPathInputWidth = 60.0f;
		const float fLabelWidth = CalcInspectorVisibleLabelWidth(refUi, refDesc.pLabel);
		const float fLabelSpacing = fLabelWidth > 0.0f ? refUi.get_style_item_spacing().x : 0.0f;
		const bool bDrawBrowseInline =
			fAvail >= kMinimumInspectorAssetPathInputWidth +
				refDesc.fBrowseButtonWidth +
				fSpacing +
				fLabelWidth +
				fLabelSpacing;
		const float fInlineReservedWidth = bDrawBrowseInline ? refDesc.fBrowseButtonWidth + fSpacing : 0.0f;
		const float fInputWidth = std::max(
			kMinimumInspectorAssetPathInputWidth,
			fAvail - fInlineReservedWidth - fLabelWidth - fLabelSpacing);
		refUi.set_next_item_width(fInputWidth);
		bChanged = DrawInspectorTextField(
			refUi,
			refDesc.pLabel ? refDesc.pLabel : "##AssetPath",
			strPath,
			refDesc.fieldSpec) || bChanged;
		bChanged = AcceptInspectorAssetPathDrop(
			refUi,
			strPath,
			refDesc,
			refState,
			pAssetDatabaseService,
			pDragDropTransferService) || bChanged;
		if (bDrawBrowseInline)
		{
			refUi.same_line(0.0f, fSpacing);
		}
		if (DrawInspectorActionButton(
			refUi,
			refDesc.pBrowseLabel ? refDesc.pBrowseLabel : "Browse",
			refDesc.browseSpec,
			{ refDesc.fBrowseButtonWidth, 0.0f }))
		{
			if (refDesc.pPopupId)
			{
				refUi.open_popup(refDesc.pPopupId);
			}
		}

		bChanged = DrawInspectorAssetPathDropZone(
			refUi,
			strPath,
			refDesc,
			refState,
			pAssetDatabaseService,
			pDragDropTransferService) || bChanged;
		bChanged = DrawInspectorAssetPathPicker(
			refUi,
			strPath,
			refDesc,
			refState,
			pAssetDatabaseService) || bChanged;
		return bChanged;
	}
}
