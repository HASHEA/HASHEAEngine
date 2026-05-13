#include "Panels/AssetBrowserPanel.h"

#include "Base/hlog.h"
#include "Core/AssetPresentationUtils.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorStringUtils.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/AssetPreviewService.h"
#include "Services/CommandService.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorShortcutService.h"
#include "Services/IEditorIconService.h"
#include "Services/SelectionService.h"
#include "Shell/PanelManager.h"
#include "Widgets/EditorActionWidgets.h"
#include "Widgets/EditorTooltipWidgets.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEditor
{
	namespace
	{
		struct AssetTypeFilterOption
		{
			const char* pLabel = "";
			AshEngine::AssetType eType = AshEngine::AssetType::Unknown;
			bool bMatchAll = false;
		};

		constexpr std::array<AssetTypeFilterOption, 11> kAssetTypeFilters{ {
			{ "All", AshEngine::AssetType::Unknown, true },
			{ "Folder", AshEngine::AssetType::Directory, false },
			{ "Scene", AshEngine::AssetType::Scene, false },
			{ "Shader", AshEngine::AssetType::Shader, false },
			{ "Texture", AshEngine::AssetType::Texture, false },
			{ "Mesh", AshEngine::AssetType::Mesh, false },
			{ "Model", AshEngine::AssetType::Model, false },
			{ "Prefab", AshEngine::AssetType::Prefab, false },
			{ "Material", AshEngine::AssetType::Material, false },
			{ "Text", AshEngine::AssetType::Text, false },
			{ "Binary", AshEngine::AssetType::Binary, false },
		} };

		struct AssetDirectoryEntry
		{
			std::filesystem::path pathRelative{};
			std::filesystem::path pathParent{};
			std::string strPathKey{};
			std::string strParentKey{};
			std::string strLabel{};
			uint32_t uChildCount = 0;
		};

		struct AssetDirectoryTreeData
		{
			std::vector<AssetDirectoryEntry> vecEntries{};
			std::unordered_set<std::string> setDirectoryKeys{};
			std::unordered_map<std::string, std::vector<size_t>> mapChildrenByParent{};
		};

		constexpr float kAssetBrowserIconSize = 16.0f;
		constexpr float kAssetBrowserGridIconSize = 36.0f;
		constexpr float kAssetBrowserGridCellWidth = 112.0f;
		constexpr float kAssetBrowserGridCellHeight = 96.0f;
		constexpr float kAssetBrowserGridSpacing = 10.0f;
		constexpr float kAssetBrowserListIconTextSpacing = 6.0f;
		constexpr const char* kAssetItemContextPopupId = "AssetBrowserItemContextMenu";
		constexpr const char* kAssetContentContextPopupId = "AssetBrowserContentContextMenu";
		constexpr AshEngine::UIColor kAssetItemSelectedFillColor{ 0.32f, 0.47f, 0.60f, 0.30f };
		constexpr AshEngine::UIColor kAssetItemHoverFillColor{ 0.28f, 0.39f, 0.49f, 0.18f };
		constexpr AshEngine::UIColor kAssetItemHoverOutlineColor{ 0.38f, 0.56f, 0.74f, 0.40f };
		constexpr AshEngine::UIColor kAssetItemSelectedOutlineColor{ 0.43f, 0.64f, 0.85f, 0.92f };
		constexpr AshEngine::UIColor kAssetToolbarSelectedButtonColor{ 0.34f, 0.42f, 0.50f, 1.0f };
		constexpr AshEngine::UIColor kAssetToolbarSelectedButtonHoveredColor{ 0.38f, 0.46f, 0.55f, 1.0f };
		constexpr AshEngine::UIColor kAssetToolbarSelectedButtonActiveColor{ 0.31f, 0.39f, 0.47f, 1.0f };
		constexpr AshEngine::UIColor kAssetToolbarMutedTextColor{ 0.67f, 0.70f, 0.76f, 1.0f };
		constexpr AshEngine::UIColor kAssetToolbarWarningTextColor{ 0.93f, 0.78f, 0.45f, 1.0f };
		constexpr AshEngine::UITooltipConfig kAssetBrowserInfoTooltipConfig{
			{ 560.0f, 0.0f },
			{ 420.0f, 0.0f },
			{ 760.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr AshEngine::UITooltipConfig kAssetBrowserWarningTooltipConfig{
			{ 420.0f, 0.0f },
			{ 320.0f, 0.0f },
			{ 640.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
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

		bool ShouldUseCompactAssetTooltip(AshEngine::AssetType eType)
		{
			switch (eType)
			{
			case AshEngine::AssetType::Directory:
			case AshEngine::AssetType::Text:
			case AshEngine::AssetType::Binary:
			case AshEngine::AssetType::Shader:
				return true;
			default:
				return false;
			}
		}

		void PushSelectedToolbarButtonStyle(AshEngine::UIContext& refUi)
		{
			refUi.push_style_color(AshEngine::UIStyleColorKind::Button, kAssetToolbarSelectedButtonColor);
			refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, kAssetToolbarSelectedButtonHoveredColor);
			refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, kAssetToolbarSelectedButtonActiveColor);
		}

		void PopSelectedToolbarButtonStyle(AshEngine::UIContext& refUi)
		{
			refUi.pop_style_color(3);
		}

		void DrawToolbarSeparator(AshEngine::UIContext& refUi)
		{
			refUi.same_line(0.0f, 10.0f);
			refUi.text_colored(kAssetToolbarMutedTextColor, "|");
			refUi.same_line(0.0f, 10.0f);
		}

		void DrawToolbarLabel(AshEngine::UIContext& refUi, const char* pLabel, const AshEngine::UIColor& color)
		{
			refUi.text_colored(color, "%s", pLabel);
		}

		void DrawListItemIcon(AshEngine::UIContext& refUi, AshEngine::UITextureHandle iconHandle)
		{
			if (!iconHandle)
			{
				return;
			}

			const AshEngine::UIRect rectItem = refUi.get_item_rect();
			const AshEngine::UIVec2 vecFramePadding = refUi.get_style_frame_padding();
			const float fX = rectItem.x + vecFramePadding.x + 2.0f;
			const float fY = rectItem.y + (rectItem.height - kAssetBrowserIconSize) * 0.5f;
			refUi.draw_window_image(iconHandle, { fX, fY, kAssetBrowserIconSize, kAssetBrowserIconSize });
		}

		void DrawListItemLabel(AshEngine::UIContext& refUi, AshEngine::UITextureHandle iconHandle, std::string_view svLabel)
		{
			if (svLabel.empty())
			{
				return;
			}

			const AshEngine::UIRect rectItem = refUi.get_item_rect();
			const AshEngine::UIVec2 vecFramePadding = refUi.get_style_frame_padding();
			float fTextX = rectItem.x + vecFramePadding.x;
			if (iconHandle)
			{
				fTextX += kAssetBrowserIconSize + kAssetBrowserListIconTextSpacing;
			}

			const std::string strText(svLabel);
			const AshEngine::UIVec2 vecTextSize = refUi.calc_text_size(strText.c_str());
			const float fTextY = rectItem.y + (rectItem.height - vecTextSize.y) * 0.5f;
			const float fClipWidth = std::max(0.0f, rectItem.x + rectItem.width - fTextX);
			refUi.push_window_clip_rect({ fTextX, rectItem.y, fClipWidth, rectItem.height });
			refUi.draw_window_text(
				{ fTextX, fTextY },
				refUi.get_style_color(AshEngine::UIStyleColorKind::Text),
				strText.c_str());
			refUi.pop_window_clip_rect();
		}

		void DrawItemFeedback(AshEngine::UIContext& refUi, bool bSelected, float fRounding = 4.0f)
		{
			if (!bSelected && !refUi.is_item_hovered())
			{
				return;
			}

			const AshEngine::UIRect rectItem = refUi.get_item_rect();
			refUi.draw_window_rect_filled(
				rectItem,
				bSelected ? kAssetItemSelectedFillColor : kAssetItemHoverFillColor,
				fRounding);
			refUi.draw_window_rect(
				rectItem,
				bSelected ? kAssetItemSelectedOutlineColor : kAssetItemHoverOutlineColor,
				fRounding,
				1.0f);
		}

		EditorIconId GetAssetIconId(const AshEngine::AssetInfo& refAsset)
		{
			return refAsset.is_directory ? EditorIconId::FolderClosed : EditorIconId::File;
		}

		bool AssetMatchesFilter(
			const AshEngine::AssetInfo& refAsset,
			const std::string& strLoweredSearchText,
			const std::string& strLoweredName,
			const std::string& strLoweredPath,
			const AssetTypeFilterOption& refTypeFilter)
		{
			if (!refTypeFilter.bMatchAll && refAsset.type != refTypeFilter.eType)
			{
				return false;
			}

			if (strLoweredSearchText.empty())
			{
				return true;
			}

			return
				strLoweredName.find(strLoweredSearchText) != std::string::npos ||
				strLoweredPath.find(strLoweredSearchText) != std::string::npos;
		}

		bool IsSameOrAncestorPath(
			const std::filesystem::path& pathAncestor,
			const std::filesystem::path& pathDescendant)
		{
			if (pathAncestor.empty())
			{
				return true;
			}

			std::filesystem::path::const_iterator itAncestor = pathAncestor.begin();
			std::filesystem::path::const_iterator itDescendant = pathDescendant.begin();
			for (; itAncestor != pathAncestor.end(); ++itAncestor, ++itDescendant)
			{
				if (itDescendant == pathDescendant.end() || *itAncestor != *itDescendant)
				{
					return false;
				}
			}

			return true;
		}

		bool IsAssetInDirectory(
			const AshEngine::AssetInfo& refAsset,
			const std::filesystem::path& pathDirectory,
			bool bIncludeDescendants = false)
		{
			if (bIncludeDescendants)
			{
				return refAsset.is_directory
					? IsSameOrAncestorPath(pathDirectory, refAsset.relative_path)
					: IsSameOrAncestorPath(pathDirectory, refAsset.parent_path);
			}

			return refAsset.parent_path == pathDirectory;
		}

		AssetDirectoryTreeData BuildDirectoryTreeData(const std::vector<AshEngine::AssetInfo>& vecAssets)
		{
			std::unordered_map<std::string, uint32_t> mapChildCounts{};
			mapChildCounts.reserve(vecAssets.size());
			for (const AshEngine::AssetInfo& refAsset : vecAssets)
			{
				++mapChildCounts[refAsset.parent_path.generic_string()];
			}

			AssetDirectoryTreeData data{};
			data.vecEntries.reserve(vecAssets.size() + 1);
			data.vecEntries.push_back({ {}, {}, "", "", "All Assets", static_cast<uint32_t>(vecAssets.size()) });
			for (const AshEngine::AssetInfo& refAsset : vecAssets)
			{
				if (!refAsset.is_directory)
				{
					continue;
				}

				const std::string strPathKey = refAsset.relative_path.generic_string();
				const std::string strParentKey = refAsset.parent_path.generic_string();
				const std::unordered_map<std::string, uint32_t>::const_iterator itChildCount = mapChildCounts.find(strPathKey);
				const uint32_t uChildCount = itChildCount == mapChildCounts.end() ? 0u : itChildCount->second;

				data.vecEntries.push_back({
					refAsset.relative_path,
					refAsset.parent_path,
					strPathKey,
					strParentKey,
					refAsset.name.empty() ? refAsset.relative_path.generic_string() : refAsset.name,
					uChildCount });
			}

			std::sort(
				data.vecEntries.begin() + 1,
				data.vecEntries.end(),
				[](const AssetDirectoryEntry& lhs, const AssetDirectoryEntry& rhs)
				{
					return lhs.strPathKey < rhs.strPathKey;
				});

			data.setDirectoryKeys.reserve(data.vecEntries.size());
			data.setDirectoryKeys.insert("");
			for (size_t uIndex = 1; uIndex < data.vecEntries.size(); ++uIndex)
			{
				const AssetDirectoryEntry& refEntry = data.vecEntries[uIndex];
				data.setDirectoryKeys.insert(refEntry.strPathKey);
				data.mapChildrenByParent[refEntry.strParentKey].push_back(uIndex);
			}

			return data;
		}

		const AshEngine::AssetInfo* GetSelectedAsset(const AssetDatabaseService& refService, uint64_t uSelectedId)
		{
			return uSelectedId == 0 ? nullptr : refService.FindById(uSelectedId);
		}

		bool DirectoryExists(const AssetDirectoryTreeData& refDirectoryTreeData, const std::filesystem::path& pathDirectory)
		{
			return refDirectoryTreeData.setDirectoryKeys.find(pathDirectory.generic_string()) != refDirectoryTreeData.setDirectoryKeys.end();
		}

		std::vector<AssetBrowserVisibleItem> BuildVisibleItems(
			const std::vector<AshEngine::AssetInfo>& vecAssets,
			const std::filesystem::path& pathActiveDirectory,
			const std::string& strLoweredSearchText,
			const AssetTypeFilterOption& refTypeFilter)
		{
			const bool bSearchActive = !strLoweredSearchText.empty();
			std::vector<AssetBrowserVisibleItem> vecVisibleItems{};
			vecVisibleItems.reserve(vecAssets.size());
			for (const AshEngine::AssetInfo& refAsset : vecAssets)
			{
				std::string strPathKey = refAsset.relative_path.generic_string();
				std::string strLoweredName{};
				std::string strLoweredPath{};
				if (bSearchActive)
				{
					strLoweredName = ToLowerCopy(refAsset.name);
					strLoweredPath = ToLowerCopy(strPathKey);
				}

				if (!AssetMatchesFilter(refAsset, strLoweredSearchText, strLoweredName, strLoweredPath, refTypeFilter))
				{
					continue;
				}

				if (!IsAssetInDirectory(refAsset, pathActiveDirectory, bSearchActive))
				{
					continue;
				}

				AssetBrowserVisibleItem item{};
				item.pAsset = &refAsset;
				item.strDisplayLabel = GetAssetDisplayLabel(refAsset);
				item.strLoweredLabel = ToLowerCopy(item.strDisplayLabel);
				item.strPathKey = std::move(strPathKey);
				vecVisibleItems.push_back(std::move(item));
			}

			return vecVisibleItems;
		}

		void SortVisibleAssets(std::vector<AssetBrowserVisibleItem>& vecVisibleItems)
		{
			std::stable_sort(
				vecVisibleItems.begin(),
				vecVisibleItems.end(),
				[](const AssetBrowserVisibleItem& lhs, const AssetBrowserVisibleItem& rhs)
				{
					if (lhs.pAsset == rhs.pAsset)
					{
						return false;
					}
					if (!lhs.pAsset)
					{
						return false;
					}
					if (!rhs.pAsset)
					{
						return true;
					}
					if (lhs.pAsset->is_directory != rhs.pAsset->is_directory)
					{
						return lhs.pAsset->is_directory && !rhs.pAsset->is_directory;
					}

					if (lhs.strLoweredLabel != rhs.strLoweredLabel)
					{
						return lhs.strLoweredLabel < rhs.strLoweredLabel;
					}

					return lhs.strPathKey < rhs.strPathKey;
				});
		}

		std::string GetAssetScopeLabel(const std::filesystem::path& pathDirectory)
		{
			return pathDirectory.empty() ? std::string("All Assets") : pathDirectory.generic_string();
		}

		std::string BuildFilterSummary(
			const std::string& strSearchText,
			const AssetTypeFilterOption& refTypeFilter,
			AssetBrowserViewMode eViewMode)
		{
			std::string strSummary =
				strSearchText.empty()
					? std::string("Search: Any")
					: std::string("Search: \"") + strSearchText + "\"";
			strSummary += " | Type: ";
			strSummary += refTypeFilter.pLabel;
			strSummary += eViewMode == AssetBrowserViewMode::Icons ? " | View: Icons" : " | View: List";
			return strSummary;
		}

		void DrawAssetBrowserInfoTooltip(
			AshEngine::UIContext& refUi,
			const std::filesystem::path& pathAssetRoot,
			const std::string& strScopeLabel,
			const std::string& strFilterSummary)
		{
			if (!refUi.is_item_hovered())
			{
				return;
			}

			refUi.begin_tooltip(kAssetBrowserInfoTooltipConfig);
			DrawEditorTooltipTitle(refUi, "Asset Browser Scope", "Current browser context");
			if (BeginEditorTooltipTable(refUi, "AssetBrowserInfoTooltip", 84.0f))
			{
				DrawEditorTooltipRow(refUi, "Root", pathAssetRoot.string());
				DrawEditorTooltipRow(refUi, "Scope", strScopeLabel);
				DrawEditorTooltipRow(refUi, "Filter", strFilterSummary);
				refUi.end_table();
			}
			refUi.end_tooltip();
		}

		void DrawAssetBrowserWarningTooltip(AshEngine::UIContext& refUi, const std::string& strWarning)
		{
			if (!refUi.is_item_hovered())
			{
				return;
			}

			refUi.begin_tooltip(kAssetBrowserWarningTooltipConfig);
			DrawEditorTooltipCompactTitle(refUi, "Asset Scan Warning");
			DrawEditorTooltipCaption(refUi, strWarning);
			refUi.end_tooltip();
		}
		
		void ClearIfAssetSelected(SelectionService* pSelectionService)
		{
			if (pSelectionService && pSelectionService->GetSelection().eKind == EditorSelectionKind::Asset)
			{
				pSelectionService->Clear();
			}
		}

		EditorTreeWidgetStyle MakeAssetBrowserTreeStyle()
		{
			EditorTreeWidgetStyle style{};
			style.fRowHeight = 26.0f;
			style.fIndentSpacing = 12.0f;
			style.fIconSize = kAssetBrowserIconSize;
			style.fIconTextSpacing = 4.0f;
			style.fRowPaddingY = 6.0f;
			style.fRowSpacingY = 3.0f;
			style.fConnectorHorizontalPadding = 3.0f;
			style.fGuideLinePaddingY = 0.0f;
			style.colorGuideLine = { 0.46f, 0.49f, 0.54f, 0.55f };
			style.colorRowHoverFill = { 0.28f, 0.39f, 0.49f, 0.16f };
			style.colorRowHoverOutline = { 0.38f, 0.56f, 0.74f, 0.36f };
			style.colorRowSelectedFill = { 0.32f, 0.47f, 0.60f, 0.28f };
			style.colorRowSelectedOutline = { 0.43f, 0.64f, 0.85f, 0.82f };
			return style;
		}

		void DrawDirectoryTree(
			EditorTreeWidget& refTreeWidget,
			AshEngine::UIContext& refUi,
			IEditorIconService* pIconService,
			const AssetDirectoryTreeData& refDirectoryTreeData,
			const AssetDirectoryEntry& refEntry,
			const std::filesystem::path& pathActiveDirectory,
			std::string& strActiveDirectoryPath,
			bool bIsLastSibling)
		{
			const bool bSelected = refEntry.pathRelative == pathActiveDirectory;
			const std::unordered_map<std::string, std::vector<size_t>>::const_iterator itChildren =
				refDirectoryTreeData.mapChildrenByParent.find(refEntry.strPathKey);
			const bool bHasChildDirectories =
				itChildren != refDirectoryTreeData.mapChildrenByParent.end() &&
				!itChildren->second.empty();
			const std::string strBaseLabel = refEntry.strLabel + " (" + std::to_string(refEntry.uChildCount) + ")";
			const std::string strStableId = refEntry.pathRelative.empty()
				? std::string("__asset_browser_root__")
				: refEntry.strPathKey;
			EditorTreeItemDesc descItem{};
			descItem.svUniqueId = strStableId;
			descItem.svLabel = strBaseLabel;
			descItem.pIcon = pIconService ? pIconService->GetIcon(EditorIconId::FolderClosed, refUi) : nullptr;
			descItem.pIconWhenOpen = pIconService ? pIconService->GetIcon(EditorIconId::FolderOpen, refUi) : nullptr;
			descItem.bIsSelected = bSelected;
			descItem.bHasChildren = bHasChildDirectories;
			descItem.bIsDefaultOpen = refEntry.pathRelative.empty() || IsSameOrAncestorPath(refEntry.pathRelative, pathActiveDirectory);
			descItem.bIsLastSibling = bIsLastSibling;
			const EditorTreeItemResult itemResult = refTreeWidget.DrawItem(descItem);
			if (itemResult.bClicked)
			{
				strActiveDirectoryPath = refEntry.pathRelative.generic_string();
			}

			if (!itemResult.bOpened)
			{
				return;
			}

			if (bHasChildDirectories)
			{
				refTreeWidget.PushLevel(!bIsLastSibling);
				const std::vector<size_t>& vecChildIndices = itChildren->second;
				for (size_t uChildIndex = 0; uChildIndex < vecChildIndices.size(); ++uChildIndex)
				{
					DrawDirectoryTree(
						refTreeWidget,
						refUi,
						pIconService,
						refDirectoryTreeData,
						refDirectoryTreeData.vecEntries[vecChildIndices[uChildIndex]],
						pathActiveDirectory,
						strActiveDirectoryPath,
						uChildIndex + 1 == vecChildIndices.size());
				}
				refTreeWidget.PopLevel();
			}
			refUi.tree_pop();
		}

		bool IsSelectedAssetVisible(
			const AshEngine::AssetInfo* pSelectedAsset,
			bool bActiveDirectoryExists,
			const std::filesystem::path& pathActiveDirectory,
			const std::string& strSearchText,
			const AssetTypeFilterOption& refTypeFilter)
		{
			if (!pSelectedAsset || !bActiveDirectoryExists)
			{
				return false;
			}

			const bool bSearchActive = !strSearchText.empty();
			std::string strLoweredName{};
			std::string strLoweredPath{};
			if (bSearchActive)
			{
				strLoweredName = ToLowerCopy(pSelectedAsset->name);
				strLoweredPath = ToLowerCopy(pSelectedAsset->relative_path.generic_string());
			}

			return
				AssetMatchesFilter(
					*pSelectedAsset,
					ToLowerCopy(strSearchText),
					strLoweredName,
					strLoweredPath,
					refTypeFilter) &&
				IsAssetInDirectory(*pSelectedAsset, pathActiveDirectory, bSearchActive);
		}

		AssetBrowserViewMode ToAssetBrowserViewMode(int32_t iValue)
		{
			return iValue == static_cast<int32_t>(AssetBrowserViewMode::Icons)
				? AssetBrowserViewMode::Icons
				: AssetBrowserViewMode::List;
		}
	}

	AssetBrowserPanel::AssetBrowserPanel(AssetBrowserPanelDeps deps)
		: EditorPanel(EditorPanelIds::AssetBrowser, EditorWindowTitles::AssetBrowser)
		, _deps(deps)
	{
	}

	void AssetBrowserPanel::BindEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
		if (_eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}

		_eventBindings.Bind(pEventBus);
		if (!pEventBus)
		{
			return;
		}

		_eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				if (refEvent.currentSelection.eKind == EditorSelectionKind::Asset)
				{
					_uSelectedAssetId = refEvent.currentSelection.uId;
				}
				else if (refEvent.currentSelection.IsEmpty() && refEvent.previousSelection.eKind == EditorSelectionKind::Asset)
				{
					_uSelectedAssetId = 0;
				}
			});
	}

	void AssetBrowserPanel::OnAttach()
	{
		if (_deps.pSettingsService)
		{
			const EditorSettings& settings = _deps.pSettingsService->GetSettings();
			_strSearchText = settings.strAssetBrowserSearchText;
			_strActiveDirectoryPath = settings.strAssetBrowserActiveDirectory;
			_iTypeFilterIndex = settings.iAssetBrowserTypeFilter;
			_eViewMode = ToAssetBrowserViewMode(settings.iAssetBrowserViewMode);
		}
		ResetDirectoryHistory();
		HLogInfo("AssetBrowserPanel attached.");
	}

	void AssetBrowserPanel::OnDetach()
	{
		PublishContentShortcutScope(false);
		UnsubscribeEvents();
		_pEventBus = nullptr;
		ClearDeps();
	}

	void AssetBrowserPanel::ClearDeps()
	{
		_deps = {};
	}

	void AssetBrowserPanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	bool AssetBrowserPanel::CanExecuteOpenSelected() const
	{
		return
			_deps.pAssetDatabaseService &&
			GetSelectedAsset(*_deps.pAssetDatabaseService, _uSelectedAssetId) &&
			_bSelectedAssetVisibleThisFrame;
	}

	void AssetBrowserPanel::ExecuteOpenSelected()
	{
		if (!_deps.pAssetDatabaseService)
		{
			return;
		}

		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset(*_deps.pAssetDatabaseService, _uSelectedAssetId);
		if (!pSelectedAsset)
		{
			return;
		}

		OpenAssetItem(*pSelectedAsset);
	}

	bool AssetBrowserPanel::CanExecuteNavigateUp() const
	{
		return _bActiveDirectoryExistsThisFrame && !_strActiveDirectoryPath.empty();
	}

	void AssetBrowserPanel::ExecuteNavigateUp()
	{
		if (!CanExecuteNavigateUp())
		{
			return;
		}

		NavigateToDirectory(std::filesystem::path(_strActiveDirectoryPath).parent_path());
	}

	void AssetBrowserPanel::DispatchContentShortcuts(const EditorFrameContext& refFrameContext, bool bContentFocused)
	{
		if (!bContentFocused || !_deps.pCommandService || !_deps.pShortcutService || !refFrameContext.pUiContext)
		{
			return;
		}

		// AssetBrowser content shortcuts must flow through the shared shortcut service so text-input suppression,
		// logging source, and future scope priority rules stay on one path.
		_deps.pShortcutService->DispatchScope(
			*_deps.pCommandService,
			EditorActionScope::AssetBrowserContent,
			*refFrameContext.pUiContext);
	}

	void AssetBrowserPanel::PublishContentShortcutScope(bool bContentFocused)
	{
		if (_bContentShortcutScopeActive == bContentFocused)
		{
			return;
		}

		_bContentShortcutScopeActive = bContentFocused;
		if (!_pEventBus)
		{
			return;
		}

		// The panel only publishes focus/scope ownership here. It does not invoke actions directly.
		EditorShortcutScopeChangedEvent event{};
		event.eScope = bContentFocused ? EditorShortcutScope::AssetBrowserContent : EditorShortcutScope::Global;
		event.strOwnerPanelId = EditorPanelIds::AssetBrowser;
		_pEventBus->Publish(event);
	}

	bool AssetBrowserPanel::HasActiveFilters() const
	{
		return !_strSearchText.empty() || _iTypeFilterIndex != 0 || !_strActiveDirectoryPath.empty();
	}

	void AssetBrowserPanel::ResetFilters()
	{
		_strSearchText.clear();
		_strActiveDirectoryPath.clear();
		_iTypeFilterIndex = 0;
	}

	void AssetBrowserPanel::SyncSettings() const
	{
		if (!_deps.pSettingsService)
		{
			return;
		}

		EditorSettings& settings = _deps.pSettingsService->GetSettings();
		settings.strAssetBrowserSearchText = _strSearchText;
		settings.strAssetBrowserActiveDirectory = _strActiveDirectoryPath;
		settings.iAssetBrowserTypeFilter = _iTypeFilterIndex;
		settings.iAssetBrowserViewMode = static_cast<int32_t>(_eViewMode);
	}

	void AssetBrowserPanel::SelectAsset(const AshEngine::AssetInfo& refItem)
	{
		_uSelectedAssetId = refItem.id;
		if (_deps.pSelectionService)
		{
			_deps.pSelectionService->Select({
				EditorSelectionKind::Asset,
				refItem.id,
				GetAssetDisplayLabel(refItem),
				refItem.relative_path.generic_string() });
		}
	}

	void AssetBrowserPanel::ClearAssetSelection()
	{
		_uSelectedAssetId = 0u;
		ClearIfAssetSelected(_deps.pSelectionService);
	}

	void AssetBrowserPanel::ActivateAsset(const AshEngine::AssetInfo& refItem)
	{
		SelectAsset(refItem);
	}

	void AssetBrowserPanel::OpenAssetItem(const AshEngine::AssetInfo& refItem)
	{
		if (refItem.is_directory)
		{
			NavigateToDirectory(refItem.relative_path);
			return;
		}

		ActivateAsset(refItem);
	}

	void AssetBrowserPanel::OpenAssetPreview(const AshEngine::AssetInfo& refItem)
	{
		SelectAsset(refItem);
		if (_deps.pAssetPreviewService)
		{
			_deps.pAssetPreviewService->SetPreviewAsset({
				EditorSelectionKind::Asset,
				refItem.id,
				GetAssetDisplayLabel(refItem),
				refItem.relative_path.generic_string() });
		}

		if (!_deps.pPanelManager)
		{
			HLogWarning("AssetBrowserPanel cannot open Asset Preview because PanelManager is unavailable. Asset={}.", refItem.relative_path.generic_string());
			return;
		}

		_deps.pPanelManager->SetPanelOpen(EditorPanelIds::AssetPreview, true);
	}

	void AssetBrowserPanel::NavigateToDirectory(const std::filesystem::path& refDirectoryPath)
	{
		NavigateToDirectoryInternal(refDirectoryPath, true);
	}

	void AssetBrowserPanel::NavigateToDirectoryInternal(const std::filesystem::path& refDirectoryPath, bool bRecordHistory)
	{
		const std::string strNewPath = refDirectoryPath.generic_string();
		if (strNewPath == _strActiveDirectoryPath)
		{
			return;
		}

		if (bRecordHistory)
		{
			if (_iDirectoryHistoryIndex >= 0 &&
				_iDirectoryHistoryIndex + 1 < static_cast<int32_t>(_vecDirectoryHistory.size()))
			{
				_vecDirectoryHistory.erase(
					_vecDirectoryHistory.begin() + (_iDirectoryHistoryIndex + 1),
					_vecDirectoryHistory.end());
			}

			if (_vecDirectoryHistory.empty() || _vecDirectoryHistory.back() != strNewPath)
			{
				_vecDirectoryHistory.push_back(strNewPath);
				_iDirectoryHistoryIndex = static_cast<int32_t>(_vecDirectoryHistory.size() - 1);
			}
		}

		_strActiveDirectoryPath = strNewPath;
	}

	bool AssetBrowserPanel::CanNavigateDirectoryBack() const
	{
		return _iDirectoryHistoryIndex > 0 && !_vecDirectoryHistory.empty();
	}

	bool AssetBrowserPanel::CanNavigateDirectoryForward() const
	{
		return
			_iDirectoryHistoryIndex >= 0 &&
			_iDirectoryHistoryIndex + 1 < static_cast<int32_t>(_vecDirectoryHistory.size());
	}

	void AssetBrowserPanel::NavigateDirectoryBack()
	{
		if (!CanNavigateDirectoryBack())
		{
			return;
		}

		--_iDirectoryHistoryIndex;
		NavigateToDirectoryInternal(std::filesystem::path(_vecDirectoryHistory[_iDirectoryHistoryIndex]), false);
	}

	void AssetBrowserPanel::NavigateDirectoryForward()
	{
		if (!CanNavigateDirectoryForward())
		{
			return;
		}

		++_iDirectoryHistoryIndex;
		NavigateToDirectoryInternal(std::filesystem::path(_vecDirectoryHistory[_iDirectoryHistoryIndex]), false);
	}

	void AssetBrowserPanel::ResetDirectoryHistory()
	{
		_vecDirectoryHistory.clear();
		_iDirectoryHistoryIndex = 0;
		_vecDirectoryHistory.push_back(_strActiveDirectoryPath);
	}

	void AssetBrowserPanel::BrowseToAssetLocation(const AshEngine::AssetInfo& refItem)
	{
		NavigateToDirectory(refItem.is_directory ? refItem.relative_path : refItem.parent_path);
	}

	void AssetBrowserPanel::HandleAssetItemInteraction(
		const EditorFrameContext& refFrameContext,
		const AshEngine::AssetInfo& refAsset,
		bool bPrimaryActivated,
		bool bDoubleClicked)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (bPrimaryActivated)
		{
			SelectAsset(refAsset);
		}
		if (bDoubleClicked)
		{
			OpenAssetItem(refAsset);
		}

		if (refUi.is_item_clicked(AshEngine::UIMouseButton::Right))
		{
			SelectAsset(refAsset);
		}
	}

	void AssetBrowserPanel::DrawViewModeToggle(AshEngine::UIContext& refUi, const char* pLabel, AssetBrowserViewMode eMode)
	{
		const bool bSelected = _eViewMode == eMode;
		if (bSelected)
		{
			PushSelectedToolbarButtonStyle(refUi);
		}

		if (refUi.small_button(pLabel))
		{
			_eViewMode = eMode;
		}

		if (bSelected)
		{
			PopSelectedToolbarButtonStyle(refUi);
		}
	}

	void AssetBrowserPanel::DrawBreadcrumbs(AshEngine::UIContext& refUi)
	{
		refUi.text_colored(kAssetToolbarMutedTextColor, "Location");
		refUi.same_line();
		const bool bAllAssetsSelected = _strActiveDirectoryPath.empty();
		if (bAllAssetsSelected)
		{
			PushSelectedToolbarButtonStyle(refUi);
		}
		if (refUi.small_button("All Assets"))
		{
			NavigateToDirectory({});
		}
		if (bAllAssetsSelected)
		{
			PopSelectedToolbarButtonStyle(refUi);
		}

		if (_strActiveDirectoryPath.empty())
		{
			return;
		}

		std::filesystem::path pathCurrent(_strActiveDirectoryPath);
		std::filesystem::path pathBreadcrumb{};
		for (const std::filesystem::path& refPart : pathCurrent)
		{
			pathBreadcrumb /= refPart;
			refUi.same_line();
			refUi.text_unformatted("/");
			refUi.same_line();
			const std::string strLabel = refPart.generic_string();
			const std::string strPathId = pathBreadcrumb.generic_string();
			const bool bIsCurrent = strPathId == _strActiveDirectoryPath;
			refUi.push_id(strPathId.c_str());
			if (bIsCurrent)
			{
				PushSelectedToolbarButtonStyle(refUi);
			}
			if (refUi.small_button(strLabel.c_str()))
			{
				NavigateToDirectory(pathBreadcrumb);
			}
			if (bIsCurrent)
			{
				PopSelectedToolbarButtonStyle(refUi);
			}
			refUi.pop_id();
		}
	}

	void AssetBrowserPanel::DrawAssetListView(
		const EditorFrameContext& refFrameContext,
		const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
		const AshEngine::AssetInfo* pSelectedAsset)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		const AshEngine::UIVec2 vecTableSize{};

		if (!refUi.begin_table(
			"AssetBrowserTable",
			3,
			AshEngine::UITableFlagBits::RowBg |
				AshEngine::UITableFlagBits::BordersInner |
				AshEngine::UITableFlagBits::Resizable |
				AshEngine::UITableFlagBits::SizingStretchProp |
				AshEngine::UITableFlagBits::ScrollY,
			vecTableSize))
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
			if (_deps.pIconService)
			{
				iconHandle = _deps.pIconService->GetIcon(GetAssetIconId(refAsset), refUi);
			}

			refUi.table_next_row();
			refUi.table_next_column();
			const std::string strItemId = std::to_string(refAsset.id);
			refUi.push_id(strItemId.c_str());
			const bool bPrimaryActivated = refUi.selectable(
				"##AssetListItem",
				bSelected,
				AshEngine::UISelectableFlagBits::SpanAllColumns);
			const bool bDoubleClicked = refUi.is_item_hovered() && refUi.is_mouse_double_clicked(AshEngine::UIMouseButton::Left);

			// Asset drag source.
			if (_deps.pDragDropTransferService && refUi.begin_drag_drop_source())
			{
				DragDropTransferData dragData{};
				dragData.strPayloadType = EditorDragPayloadTypes::Asset;
				dragData.vecEntityIds = { refAsset.id };
				dragData.extraData = refAsset.relative_path.generic_string();
				const DragDropTransferId uTransferId = _deps.pDragDropTransferService->Register(std::move(dragData));
				refUi.set_drag_drop_payload(EditorDragPayloadTypes::Asset, &uTransferId, sizeof(DragDropTransferId));
				refUi.text_unformatted(refVisibleItem.strDisplayLabel.c_str());
				refUi.end_drag_drop_source();
			}

			DrawItemFeedback(refUi, bSelected);
			DrawListItemIcon(refUi, iconHandle);
			DrawListItemLabel(refUi, iconHandle, refVisibleItem.strDisplayLabel);
			DrawAssetItemTooltip(refFrameContext, refAsset, refVisibleItem.strDisplayLabel);
			HandleAssetItemInteraction(refFrameContext, refAsset, bPrimaryActivated, bDoubleClicked);
			DrawAssetItemContextMenu(refFrameContext, refAsset);
			refUi.pop_id();

			refUi.table_next_column();
			refUi.text_unformatted(AssetDatabaseService::GetTypeLabel(refAsset.type));
			refUi.table_next_column();
			refUi.text_unformatted(AssetDatabaseService::GetLoadStateLabel(_deps.pAssetDatabaseService->GetLoadState(refAsset.id)));
		}

		refUi.end_table();
	}

	void AssetBrowserPanel::DrawAssetIconView(
		const EditorFrameContext& refFrameContext,
		const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
		const AshEngine::AssetInfo* pSelectedAsset)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		const float fGridHeight = 0.0f;

		if (!refUi.begin_child("AssetBrowserIconView", { 0.0f, fGridHeight }, AshEngine::UIChildFlagBits::Border))
		{
			refUi.end_child();
			return;
		}

		const float fAvailableWidth = std::max(1.0f, refUi.get_content_region_avail().x);
		const float fCellSpan = kAssetBrowserGridCellWidth + kAssetBrowserGridSpacing;
		const int32_t iColumnCount = std::max(1, static_cast<int32_t>((fAvailableWidth + kAssetBrowserGridSpacing) / fCellSpan));
		int32_t iColumnIndex = 0;
		for (size_t uAssetIndex = 0; uAssetIndex < vecVisibleItems.size(); ++uAssetIndex)
		{
			const AssetBrowserVisibleItem& refVisibleItem = vecVisibleItems[uAssetIndex];
			const AshEngine::AssetInfo& refAsset = *refVisibleItem.pAsset;
			const bool bSelected = pSelectedAsset && pSelectedAsset->id == refAsset.id;
			AshEngine::UITextureHandle iconHandle = nullptr;
			if (_deps.pIconService)
			{
				iconHandle = _deps.pIconService->GetIcon(GetAssetIconId(refAsset), refUi);
			}

			const std::string strItemId = std::to_string(refAsset.id);
			refUi.push_id(strItemId.c_str());
			const bool bPrimaryActivated = refUi.selectable(
				"##AssetIconItem",
				bSelected,
				AshEngine::UISelectableFlagBits::None,
				{ kAssetBrowserGridCellWidth, kAssetBrowserGridCellHeight });
			const bool bDoubleClicked = refUi.is_item_hovered() && refUi.is_mouse_double_clicked(AshEngine::UIMouseButton::Left);

			// Asset drag source.
			if (_deps.pDragDropTransferService && refUi.begin_drag_drop_source())
			{
				DragDropTransferData dragData{};
				dragData.strPayloadType = EditorDragPayloadTypes::Asset;
				dragData.vecEntityIds = { refAsset.id };
				dragData.extraData = refAsset.relative_path.generic_string();
				const DragDropTransferId uTransferId = _deps.pDragDropTransferService->Register(std::move(dragData));
				refUi.set_drag_drop_payload(EditorDragPayloadTypes::Asset, &uTransferId, sizeof(DragDropTransferId));
				refUi.text_unformatted(refVisibleItem.strDisplayLabel.c_str());
				refUi.end_drag_drop_source();
			}

			DrawItemFeedback(refUi, bSelected, 6.0f);

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

			DrawAssetItemTooltip(refFrameContext, refAsset, refVisibleItem.strDisplayLabel);
			HandleAssetItemInteraction(refFrameContext, refAsset, bPrimaryActivated, bDoubleClicked);
			DrawAssetItemContextMenu(refFrameContext, refAsset);
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

	void AssetBrowserPanel::DrawAssetItemTooltip(
		const EditorFrameContext& refFrameContext,
		const AshEngine::AssetInfo& refAsset,
		std::string_view svDisplayLabel)
	{
		if (!refFrameContext.pUiContext || !_deps.pAssetDatabaseService)
		{
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.is_item_hovered())
		{
			return;
		}

		const std::string strTitle = svDisplayLabel.empty() ? GetAssetDisplayLabel(refAsset) : std::string(svDisplayLabel);
		const std::string strSubtitle = refAsset.relative_path.generic_string();
		const std::string strTypeLabel = AssetDatabaseService::GetTypeLabel(refAsset.type);
		const std::string strParent =
			refAsset.parent_path.empty()
			? std::string("<Root>")
			: refAsset.parent_path.generic_string();
		const std::string strSizeLabel =
			refAsset.is_directory ? std::string("-") : FormatAssetFileSize(refAsset.file_size);
		const std::string strModifiedLabel =
			FormatAssetLastWriteTime(*_deps.pAssetDatabaseService, refAsset);
		const std::string strLoadState =
			AssetDatabaseService::GetLoadStateLabel(_deps.pAssetDatabaseService->GetLoadState(refAsset.id));
		const bool bUseCompactTooltip = ShouldUseCompactAssetTooltip(refAsset.type);

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

	void AssetBrowserPanel::DrawAssetItemContextMenu(const EditorFrameContext& refFrameContext, const AshEngine::AssetInfo& refAsset)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup_context_item(kAssetItemContextPopupId))
		{
			return;
		}

		SelectAsset(refAsset);
		const bool bCanBrowse = refAsset.is_directory || !refAsset.parent_path.empty();

		if (refUi.menu_item("Select"))
		{
			SelectAsset(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item(refAsset.is_directory ? "Open Folder" : "Open", nullptr, false, true))
		{
			OpenAssetItem(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Browse Location", nullptr, false, bCanBrowse))
		{
			BrowseToAssetLocation(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Preview", nullptr, false, !refAsset.is_directory))
		{
			OpenAssetPreview(refAsset);
			refUi.close_current_popup();
		}
		refUi.separator();
		if (refUi.menu_item("Clear Selection"))
		{
			ClearAssetSelection();
			refUi.close_current_popup();
		}
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::AssetsRefresh,
			"Refresh",
			"asset_browser.item_context",
			true))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void AssetBrowserPanel::DrawContentContextMenu(
		const EditorFrameContext& refFrameContext,
		bool bActiveDirectoryExists,
		bool bFiltersActive)
	{
		(void)bActiveDirectoryExists;
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup(kAssetContentContextPopupId))
		{
			return;
		}

		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::AssetsNavigateUp,
			"Up",
			"asset_browser.content_context",
			true))
		{
			refUi.close_current_popup();
		}
		refUi.separator();

		if (refUi.menu_item("List View", nullptr, _eViewMode == AssetBrowserViewMode::List))
		{
			_eViewMode = AssetBrowserViewMode::List;
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Icon View", nullptr, _eViewMode == AssetBrowserViewMode::Icons))
		{
			_eViewMode = AssetBrowserViewMode::Icons;
			refUi.close_current_popup();
		}

		refUi.separator();
		if (refUi.menu_item("Reset Filters", nullptr, false, bFiltersActive))
		{
			ResetFilters();
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Clear Selection"))
		{
			ClearAssetSelection();
			refUi.close_current_popup();
		}
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::AssetsRefresh,
			"Refresh",
			"asset_browser.content_context",
			true))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void AssetBrowserPanel::OnGui(const EditorFrameContext& frameContext)
	{
		if (!BeginPanelWindow(frameContext))
		{
			EndPanelWindow(frameContext);
			return;
		}

		_bActiveDirectoryExistsThisFrame = false;
		_bSelectedAssetVisibleThisFrame = false;
		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}
		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (!_deps.pAssetDatabaseService)
		{
			refUi.text_unformatted("Asset database unavailable.");
			EndPanelWindow(frameContext);
			return;
		}

		const std::vector<AshEngine::AssetInfo>& vecAssets = _deps.pAssetDatabaseService->GetItems();
		_iTypeFilterIndex = std::clamp(_iTypeFilterIndex, 0, static_cast<int32_t>(kAssetTypeFilters.size() - 1));
		const AssetTypeFilterOption& refTypeFilter = kAssetTypeFilters[_iTypeFilterIndex];
		const AssetDirectoryTreeData directoryTreeData = BuildDirectoryTreeData(vecAssets);
		const std::vector<AssetDirectoryEntry>& vecDirectories = directoryTreeData.vecEntries;
		const std::filesystem::path pathActiveDirectory =
			_strActiveDirectoryPath.empty() ? std::filesystem::path{} : std::filesystem::path(_strActiveDirectoryPath);
		const bool bActiveDirectoryExists = DirectoryExists(directoryTreeData, pathActiveDirectory);
		_bActiveDirectoryExistsThisFrame = bActiveDirectoryExists;
		const AshEngine::AssetInfo* pSelectedAsset = GetSelectedAsset(*_deps.pAssetDatabaseService, _uSelectedAssetId);
		if (!pSelectedAsset && _deps.pSelectionService && _deps.pSelectionService->GetSelection().eKind == EditorSelectionKind::Asset)
		{
			pSelectedAsset = _deps.pAssetDatabaseService->FindById(_deps.pSelectionService->GetSelection().uId);
			_uSelectedAssetId = pSelectedAsset ? pSelectedAsset->id : 0u;
		}

		const std::vector<const char*> vecTypeLabels{
			"All", "Folder", "Scene", "Shader", "Texture", "Mesh", "Model", "Prefab", "Material", "Text", "Binary"
		};
		const std::string strLoweredSearchText = ToLowerCopy(_strSearchText);
		const std::string strScopeLabel = GetAssetScopeLabel(pathActiveDirectory);
		const std::string strFilterSummary = BuildFilterSummary(_strSearchText, refTypeFilter, _eViewMode);
		std::vector<AssetBrowserVisibleItem> vecVisibleItems =
			BuildVisibleItems(vecAssets, pathActiveDirectory, strLoweredSearchText, refTypeFilter);
		SortVisibleAssets(vecVisibleItems);
		const uint32_t uFilteredCount = static_cast<uint32_t>(vecVisibleItems.size());
		const bool bFiltersActive = HasActiveFilters();

		bool bSelectedAssetVisible = IsSelectedAssetVisible(
			pSelectedAsset,
			bActiveDirectoryExists,
			pathActiveDirectory,
			_strSearchText,
			refTypeFilter);
		_bSelectedAssetVisibleThisFrame = bSelectedAssetVisible;

		const std::string strLastError = _deps.pAssetDatabaseService->GetLastError();
		refUi.set_next_item_width(220.0f);
		refUi.input_text("Search", _strSearchText);
		refUi.same_line();
		refUi.set_next_item_width(124.0f);
		refUi.combo("Type", _iTypeFilterIndex, vecTypeLabels);
		refUi.same_line();
		refUi.begin_disabled(!bFiltersActive);
		if (refUi.small_button("Reset"))
		{
			ResetFilters();
		}
		refUi.end_disabled();
		DrawToolbarSeparator(refUi);
		refUi.begin_disabled(!CanNavigateDirectoryBack());
		if (refUi.small_button("<"))
		{
			NavigateDirectoryBack();
		}
		refUi.end_disabled();
		refUi.same_line();
		refUi.begin_disabled(!CanNavigateDirectoryForward());
		if (refUi.small_button(">"))
		{
			NavigateDirectoryForward();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (_deps.pCommandService)
		{
			DrawEditorActionSmallButton(
				refUi,
				*_deps.pCommandService,
				EditorActionIds::AssetsNavigateUp,
				"Up",
				"asset_browser.toolbar");
		}
		else
		{
			refUi.begin_disabled(true);
			refUi.small_button("Up");
			refUi.end_disabled();
		}
		DrawToolbarSeparator(refUi);
		DrawViewModeToggle(refUi, "List", AssetBrowserViewMode::List);
		refUi.same_line();
		DrawViewModeToggle(refUi, "Icons", AssetBrowserViewMode::Icons);
		refUi.same_line();
		if (_deps.pCommandService)
		{
			DrawEditorActionButton(
				refUi,
				*_deps.pCommandService,
				EditorActionIds::AssetsRefresh,
				"Refresh",
				"asset_browser.toolbar");
		}
		else
		{
			refUi.begin_disabled(true);
			refUi.button("Refresh");
			refUi.end_disabled();
		}
		DrawToolbarSeparator(refUi);
		DrawToolbarLabel(refUi, "Info", kAssetToolbarMutedTextColor);
		DrawAssetBrowserInfoTooltip(refUi, _deps.pAssetDatabaseService->GetAssetRoot(), strScopeLabel, strFilterSummary);
		if (!strLastError.empty())
		{
			DrawToolbarSeparator(refUi);
			DrawToolbarLabel(refUi, "Warning", kAssetToolbarWarningTextColor);
			DrawAssetBrowserWarningTooltip(refUi, strLastError);
		}
		refUi.separator();

		const AshEngine::UIVec2 vecAvailRegion = refUi.get_content_region_avail();
		const float fLeftWidth = std::max(180.0f, vecAvailRegion.x * 0.28f);

		if (refUi.begin_child("AssetBrowserDirectories", { fLeftWidth, 0.0f }, AshEngine::UIChildFlagBits::Border))
		{
			refUi.text_unformatted("Directories");
			refUi.separator();
			if (!vecDirectories.empty())
			{
				EditorTreeWidget treeWidget(refUi, _treeStateDirectories, MakeAssetBrowserTreeStyle());
				treeWidget.ResetDragStateIfInactive();
				DrawDirectoryTree(
					treeWidget,
					refUi,
					_deps.pIconService,
					directoryTreeData,
					directoryTreeData.vecEntries.front(),
					pathActiveDirectory,
					_strActiveDirectoryPath,
					true);
			}
		}
		refUi.end_child();
		refUi.same_line();

		if (refUi.begin_child("AssetBrowserContent", {}, AshEngine::UIChildFlagBits::Border))
		{
			DrawBreadcrumbs(refUi);
			refUi.same_line();
			refUi.text_colored(
				kAssetToolbarMutedTextColor,
				"| %u / %u",
				uFilteredCount,
				static_cast<uint32_t>(vecAssets.size()));
			refUi.separator();

			if (!bActiveDirectoryExists)
			{
				refUi.text_unformatted("The saved directory is no longer available.");
				refUi.text_unformatted("Reset the directory filter to return to the asset root.");
				if (refUi.button("Reset Directory"))
				{
					_strActiveDirectoryPath.clear();
				}
			}
			else if (vecAssets.empty())
			{
				if (!strLastError.empty())
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
			else if (uFilteredCount == 0)
			{
				refUi.text_unformatted("No assets match the current search, type filter, or directory.");
				if (!_strSearchText.empty())
				{
					refUi.text("Search: %s", _strSearchText.c_str());
				}
				refUi.text("Type Filter: %s", refTypeFilter.pLabel);
				refUi.text("Directory: %s", strScopeLabel.c_str());
				refUi.begin_disabled(!bFiltersActive);
				if (refUi.button("Clear Search And Filters"))
				{
					ResetFilters();
				}
				refUi.end_disabled();
			}

			if (pSelectedAsset && !bSelectedAssetVisible)
			{
				refUi.separator();
				refUi.text_unformatted("The current asset selection is outside the visible browser scope.");
				if (refUi.button("Reveal Selection"))
				{
					_strSearchText.clear();
					_iTypeFilterIndex = 0;
					BrowseToAssetLocation(*pSelectedAsset);
				}
				refUi.same_line();
				if (refUi.button("Clear Selection"))
				{
					ClearAssetSelection();
					pSelectedAsset = nullptr;
				}
			}

			if (bActiveDirectoryExists && uFilteredCount > 0)
			{
				if (_eViewMode == AssetBrowserViewMode::Icons)
				{
					DrawAssetIconView(frameContext, vecVisibleItems, pSelectedAsset);
				}
				else
				{
					DrawAssetListView(frameContext, vecVisibleItems, pSelectedAsset);
				}
			}

			const bool bOpenContentMenu =
				refUi.is_window_hovered_with_children() &&
				!refUi.is_any_item_hovered() &&
				!refUi.is_any_item_active() &&
				refUi.is_mouse_released(AshEngine::UIMouseButton::Right);
			const bool bClearContentSelection =
				refUi.is_window_hovered_with_children() &&
				!refUi.is_any_item_hovered() &&
				!refUi.is_any_item_active() &&
				refUi.is_mouse_released(AshEngine::UIMouseButton::Left);
			if (bOpenContentMenu)
			{
				refUi.open_popup(kAssetContentContextPopupId);
			}
			if (bClearContentSelection)
			{
				ClearAssetSelection();
				pSelectedAsset = nullptr;
			}

			const bool bContentFocused =
				refUi.is_window_focused_with_children() &&
				!refUi.wants_text_input();
			PublishContentShortcutScope(bContentFocused);
			DispatchContentShortcuts(frameContext, bContentFocused);

			DrawContentContextMenu(frameContext, bActiveDirectoryExists, bFiltersActive);

			pSelectedAsset = GetSelectedAsset(*_deps.pAssetDatabaseService, _uSelectedAssetId);
			bSelectedAssetVisible = IsSelectedAssetVisible(
				pSelectedAsset,
				bActiveDirectoryExists,
				pathActiveDirectory,
				_strSearchText,
				refTypeFilter);
			_bSelectedAssetVisibleThisFrame = bSelectedAssetVisible;
		}
		refUi.end_child();

		SyncSettings();
		EndPanelWindow(frameContext);
	}
}
