#include "Panels/AssetBrowser/AssetBrowserContentView.h"

#include "Core/AssetPresentationUtils.h"
#include "Core/EditorIds.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/AssetBrowserContextMenus.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Services/IEditorIconService.h"
#include "Widgets/EditorTooltipWidgets.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace AshEditor
{
	namespace
	{
		constexpr float kAssetBrowserGridIconSize = 36.0f;
		constexpr float kAssetBrowserGridCellWidth = 112.0f;
		constexpr float kAssetBrowserGridCellHeight = 96.0f;
		constexpr float kAssetBrowserGridSpacing = 10.0f;
		constexpr AshEngine::UITooltipConfig kAssetItemTooltipConfig{
			{ 460.0f, 0.0f },
			{ 300.0f, 0.0f },
			{ 620.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr AshEngine::UITooltipConfig kAssetItemCompactTooltipConfig{
			{ 340.0f, 0.0f },
			{ 220.0f, 0.0f },
			{ 460.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};

		void BeginAssetDragSource(
			const AssetBrowserViewContext& refViewContext,
			AshEngine::UIContext& refUi,
			const AshEngine::AssetInfo& refAsset,
			std::string_view svDisplayLabel)
		{
			if (!refViewContext.refDeps.pDragDropTransferService || !refUi.begin_drag_drop_source())
			{
				return;
			}

			DragDropTransferData dragData{};
			dragData.strPayloadType = EditorDragPayloadTypes::Asset;
			dragData.vecEntityIds = { refAsset.id };
			dragData.extraData = refAsset.relative_path.generic_string();
			const DragDropTransferId uTransferId =
				refViewContext.refDeps.pDragDropTransferService->Register(std::move(dragData));
			refUi.set_drag_drop_payload(EditorDragPayloadTypes::Asset, &uTransferId, sizeof(DragDropTransferId));
			refUi.text_unformatted(std::string(svDisplayLabel).c_str());
			refUi.end_drag_drop_source();
		}

		void HandleAssetItemInteraction(
			const AssetBrowserViewContext& refViewContext,
			AshEngine::UIContext& refUi,
			const AshEngine::AssetInfo& refAsset,
			bool bPrimaryActivated,
			bool bDoubleClicked)
		{
			if (bPrimaryActivated)
			{
				refViewContext.refHost.SelectAsset(refAsset);
			}
			if (bDoubleClicked)
			{
				refViewContext.refHost.OpenAssetItem(refAsset);
			}
			if (refUi.is_item_clicked(AshEngine::UIMouseButton::Right))
			{
				refViewContext.refHost.SelectAsset(refAsset);
			}
		}

		void DrawAssetItemTooltip(
			const AssetBrowserViewContext& refViewContext,
			const AshEngine::AssetInfo& refAsset,
			std::string_view svDisplayLabel)
		{
			if (!refViewContext.refDeps.pAssetDatabaseService)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
			if (!refUi.is_item_hovered())
			{
				return;
			}

			const std::string strTitle =
				svDisplayLabel.empty() ? GetAssetDisplayLabel(refAsset) : std::string(svDisplayLabel);
			const std::string strSubtitle = refAsset.relative_path.generic_string();
			const std::string strTypeLabel = AssetDatabaseService::GetTypeLabel(refAsset.type);
			const std::string strParent =
				refAsset.parent_path.empty()
				? std::string("<Root>")
				: refAsset.parent_path.generic_string();
			const std::string strSizeLabel =
				refAsset.is_directory ? std::string("-") : FormatAssetFileSize(refAsset.file_size);
			const std::string strModifiedLabel =
				FormatAssetLastWriteTime(*refViewContext.refDeps.pAssetDatabaseService, refAsset);
			const std::string strLoadState =
				AssetDatabaseService::GetLoadStateLabel(
					refViewContext.refDeps.pAssetDatabaseService->GetLoadState(refAsset.id));
			const bool bUseCompactTooltip = AssetBrowserSupport::ShouldUseCompactAssetTooltip(refAsset.type);

			refUi.begin_tooltip(bUseCompactTooltip ? kAssetItemCompactTooltipConfig : kAssetItemTooltipConfig);
			if (bUseCompactTooltip)
			{
				DrawEditorTooltipCompactTitle(refUi, strTitle, strTypeLabel);
				if (!strSubtitle.empty())
				{
					DrawEditorTooltipCaption(refUi, strSubtitle);
				}
				refUi.separator();
				DrawEditorTooltipCompactRow(refUi, "Modified", strModifiedLabel);
				DrawEditorTooltipCompactRow(refUi, "Size", strSizeLabel);
				if (refAsset.type != AshEngine::AssetType::Directory)
				{
					DrawEditorTooltipCompactRow(refUi, "State", strLoadState);
				}
			}
			else
			{
				DrawEditorTooltipTitle(refUi, strTitle, strTypeLabel);
				if (BeginEditorTooltipTable(refUi, "AssetBrowserItemTooltip", 88.0f))
				{
					DrawEditorTooltipRow(refUi, "Modified", strModifiedLabel);
					DrawEditorTooltipRow(refUi, "Size", strSizeLabel);
					DrawEditorTooltipRow(refUi, "Parent", strParent);
					DrawEditorTooltipRow(refUi, "State", strLoadState);
					DrawEditorTooltipRow(refUi, "Path", strSubtitle);
					refUi.end_table();
				}
			}

			refUi.end_tooltip();
		}

		void DrawAssetListView(
			const AssetBrowserViewContext& refViewContext,
			const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
			const AshEngine::AssetInfo* pSelectedAsset,
			const AssetBrowserContextMenus& refContextMenus)
		{
			AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
			if (!refUi.begin_table(
				"AssetBrowserTable",
				3,
				AshEngine::UITableFlagBits::RowBg |
					AshEngine::UITableFlagBits::BordersInner |
					AshEngine::UITableFlagBits::Resizable |
					AshEngine::UITableFlagBits::SizingStretchProp |
					AshEngine::UITableFlagBits::ScrollY,
				{}))
			{
				return;
			}

			refUi.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch);
			refUi.table_setup_column("Type", AshEngine::UITableColumnFlagBits::WidthFixed, 110.0f);
			refUi.table_setup_column("State", AshEngine::UITableColumnFlagBits::WidthFixed, 90.0f);
			refUi.table_headers_row();
			for (const AssetBrowserVisibleItem& refVisibleItem : vecVisibleItems)
			{
				const AshEngine::AssetInfo& refAsset = *refVisibleItem.pAsset;
				const bool bSelected = pSelectedAsset && pSelectedAsset->id == refAsset.id;
				AshEngine::UITextureHandle iconHandle = nullptr;
				if (refViewContext.refDeps.pIconService)
				{
					iconHandle = refViewContext.refDeps.pIconService->GetIcon(
						AssetBrowserSupport::GetAssetIconId(refAsset),
						refUi);
				}

				refUi.table_next_row();
				refUi.table_next_column();
				const std::string strItemId = std::to_string(refAsset.id);
				refUi.push_id(strItemId.c_str());
				const bool bPrimaryActivated = refUi.selectable(
					"##AssetListItem",
					bSelected,
					AshEngine::UISelectableFlagBits::SpanAllColumns);
				const bool bDoubleClicked =
					refUi.is_item_hovered() &&
					refUi.is_mouse_double_clicked(AshEngine::UIMouseButton::Left);

				BeginAssetDragSource(refViewContext, refUi, refAsset, refVisibleItem.strDisplayLabel);
				AssetBrowserSupport::DrawItemFeedback(refUi, bSelected);
				AssetBrowserSupport::DrawListItemIcon(refUi, iconHandle);
				AssetBrowserSupport::DrawListItemLabel(refUi, iconHandle, refVisibleItem.strDisplayLabel);
				DrawAssetItemTooltip(refViewContext, refAsset, refVisibleItem.strDisplayLabel);
				HandleAssetItemInteraction(refViewContext, refUi, refAsset, bPrimaryActivated, bDoubleClicked);
				refContextMenus.DrawAssetItemMenu(refViewContext, refAsset);
				refUi.pop_id();

				refUi.table_next_column();
				refUi.text_unformatted(AssetDatabaseService::GetTypeLabel(refAsset.type));
				refUi.table_next_column();
				refUi.text_unformatted(
					AssetDatabaseService::GetLoadStateLabel(
						refViewContext.refDeps.pAssetDatabaseService->GetLoadState(refAsset.id)));
			}

			refUi.end_table();
		}

		void DrawAssetIconView(
			const AssetBrowserViewContext& refViewContext,
			const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
			const AshEngine::AssetInfo* pSelectedAsset,
			const AssetBrowserContextMenus& refContextMenus)
		{
			AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
			if (!refUi.begin_child("AssetBrowserIconView", { 0.0f, 0.0f }, AshEngine::UIChildFlagBits::Border))
			{
				refUi.end_child();
				return;
			}

			const float fAvailableWidth = std::max(1.0f, refUi.get_content_region_avail().x);
			const float fCellSpan = kAssetBrowserGridCellWidth + kAssetBrowserGridSpacing;
			const int32_t iColumnCount =
				std::max(1, static_cast<int32_t>((fAvailableWidth + kAssetBrowserGridSpacing) / fCellSpan));
			int32_t iColumnIndex = 0;
			for (size_t uAssetIndex = 0; uAssetIndex < vecVisibleItems.size(); ++uAssetIndex)
			{
				const AssetBrowserVisibleItem& refVisibleItem = vecVisibleItems[uAssetIndex];
				const AshEngine::AssetInfo& refAsset = *refVisibleItem.pAsset;
				const bool bSelected = pSelectedAsset && pSelectedAsset->id == refAsset.id;
				AshEngine::UITextureHandle iconHandle = nullptr;
				if (refViewContext.refDeps.pIconService)
				{
					iconHandle = refViewContext.refDeps.pIconService->GetIcon(
						AssetBrowserSupport::GetAssetIconId(refAsset),
						refUi);
				}

				const std::string strItemId = std::to_string(refAsset.id);
				refUi.push_id(strItemId.c_str());
				const bool bPrimaryActivated = refUi.selectable(
					"##AssetIconItem",
					bSelected,
					AshEngine::UISelectableFlagBits::None,
					{ kAssetBrowserGridCellWidth, kAssetBrowserGridCellHeight });
				const bool bDoubleClicked =
					refUi.is_item_hovered() &&
					refUi.is_mouse_double_clicked(AshEngine::UIMouseButton::Left);

				BeginAssetDragSource(refViewContext, refUi, refAsset, refVisibleItem.strDisplayLabel);
				AssetBrowserSupport::DrawItemFeedback(refUi, bSelected, 6.0f);

				const AshEngine::UIRect rectItem = refUi.get_item_rect();
				const float fIconX = rectItem.x + (kAssetBrowserGridCellWidth - kAssetBrowserGridIconSize) * 0.5f;
				const float fIconY = rectItem.y + 10.0f;
				if (iconHandle)
				{
					refUi.draw_window_image(
						iconHandle,
						{ fIconX, fIconY, kAssetBrowserGridIconSize, kAssetBrowserGridIconSize });
				}

				const float fTextX = rectItem.x + 8.0f;
				const float fTextY = fIconY + kAssetBrowserGridIconSize + 8.0f;
				const float fTextWidth = std::max(1.0f, rectItem.width - 16.0f);
				const float fTextHeight = std::max(1.0f, rectItem.y + rectItem.height - 8.0f - fTextY);
				refUi.push_window_clip_rect({ fTextX, fTextY, fTextWidth, fTextHeight });
				refUi.draw_window_text(
					{ fTextX, fTextY },
					refUi.get_style_color(AshEngine::UIStyleColorKind::Text),
					refVisibleItem.strDisplayLabel.c_str(),
					fTextWidth);
				refUi.pop_window_clip_rect();

				DrawAssetItemTooltip(refViewContext, refAsset, refVisibleItem.strDisplayLabel);
				HandleAssetItemInteraction(refViewContext, refUi, refAsset, bPrimaryActivated, bDoubleClicked);
				refContextMenus.DrawAssetItemMenu(refViewContext, refAsset);
				refUi.pop_id();

				++iColumnIndex;
				const bool bEndOfRow = iColumnIndex >= iColumnCount;
				const bool bHasMoreItems = uAssetIndex + 1 < vecVisibleItems.size();
				if (!bEndOfRow && bHasMoreItems)
				{
					refUi.same_line(0.0f, kAssetBrowserGridSpacing);
				}
				else
				{
					iColumnIndex = 0;
				}
			}

			refUi.end_child();
		}
	}

	AssetBrowserContentDrawResult AssetBrowserContentView::Draw(
		const AssetBrowserViewContext& refViewContext,
		const AssetBrowserFrameData& refFrameData,
		const AssetBrowserContextMenus& refContextMenus) const
	{
		AssetBrowserContentDrawResult drawResult{};
		AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
		const AssetTypeFilterOption& refTypeFilter = *refFrameData.pTypeFilter;
		const AshEngine::AssetInfo* pSelectedAsset = refFrameData.pSelectedAsset;

		if (!refFrameData.bActiveDirectoryExists)
		{
			refUi.text_unformatted("The saved directory is no longer available.");
			refUi.text_unformatted("Reset the directory filter to return to the asset root.");
			if (refUi.button("Reset Directory"))
			{
				refViewContext.refHost.NavigateToDirectory({});
			}
		}
		else if (refFrameData.directoryTreeData.vecEntries.empty())
		{
			if (!refFrameData.strLastError.empty())
			{
				refUi.text_unformatted("No assets are available because the last asset scan reported an error.");
				refUi.text_unformatted("Review the error above, then refresh the asset database.");
			}
			else
			{
				refUi.text_unformatted("No assets are indexed yet.");
				refUi.text_unformatted("Refresh the asset database or confirm the asset root contains importable files.");
			}
		}
		else if (refFrameData.uFilteredCount == 0)
		{
			refUi.text_unformatted("No assets match the current search, type filter, or directory.");
			if (!refViewContext.refState.strSearchText.empty())
			{
				refUi.text("Search: %s", refViewContext.refState.strSearchText.c_str());
			}
			refUi.text("Type Filter: %s", refTypeFilter.pLabel);
			refUi.text("Directory: %s", refFrameData.strScopeLabel.c_str());
			refUi.begin_disabled(!refFrameData.bFiltersActive);
			if (refUi.button("Clear Search And Filters"))
			{
				refViewContext.refHost.ResetFilters();
			}
			refUi.end_disabled();
		}

		if (pSelectedAsset && !refFrameData.bSelectedAssetVisible)
		{
			refUi.separator();
			refUi.text_unformatted("The current asset selection is outside the visible browser scope.");
			if (refUi.button("Reveal Selection"))
			{
				refViewContext.refState.strSearchText.clear();
				refViewContext.refState.iTypeFilterIndex = 0;
				refViewContext.refHost.BrowseToAssetLocation(*pSelectedAsset);
			}
			refUi.same_line();
			if (refUi.button("Clear Selection"))
			{
				refViewContext.refHost.ClearAssetSelection();
				pSelectedAsset = nullptr;
			}
		}

		if (refFrameData.bActiveDirectoryExists && refFrameData.uFilteredCount > 0)
		{
			if (refViewContext.refState.eViewMode == AssetBrowserViewMode::Icons)
			{
				DrawAssetIconView(
					refViewContext,
					refFrameData.vecVisibleItems,
					pSelectedAsset,
					refContextMenus);
			}
			else
			{
				DrawAssetListView(
					refViewContext,
					refFrameData.vecVisibleItems,
					pSelectedAsset,
					refContextMenus);
			}
		}

		drawResult.bOpenContentMenu =
			refUi.is_window_hovered_with_children() &&
			!refUi.is_any_item_hovered() &&
			!refUi.is_any_item_active() &&
			refUi.is_mouse_released(AshEngine::UIMouseButton::Right);
		drawResult.bClearContentSelection =
			refUi.is_window_hovered_with_children() &&
			!refUi.is_any_item_hovered() &&
			!refUi.is_any_item_active() &&
			refUi.is_mouse_released(AshEngine::UIMouseButton::Left);
		drawResult.bContentFocused =
			refUi.is_window_focused_with_children() &&
			!refUi.wants_text_input();
		return drawResult;
	}
}
