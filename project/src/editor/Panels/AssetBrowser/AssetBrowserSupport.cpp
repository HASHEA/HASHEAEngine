#include "Panels/AssetBrowser/AssetBrowserSupport.h"

#include "Core/AssetPresentationUtils.h"
#include "Core/EditorPathUtils.h"
#include "Core/EditorSelection.h"
#include "Core/EditorStringUtils.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/EditorSettingsService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/EditorTooltipWidgets.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace AshEditor
{
	namespace
	{
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

		constexpr float kAssetBrowserIconSize = 16.0f;
		constexpr float kAssetBrowserListIconTextSpacing = 6.0f;
		constexpr const char* kAssetItemContextPopupId = "AssetBrowserItemContextMenu";
		constexpr const char* kAssetContentContextPopupId = "AssetBrowserContentContextMenu";
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

		bool IsAssetInDirectory(
			const AshEngine::AssetInfo& refAsset,
			const std::filesystem::path& pathDirectory,
			bool bIncludeDescendants = false)
		{
			if (bIncludeDescendants)
			{
				return refAsset.is_directory
					? AssetBrowserSupport::IsSameOrAncestorPath(pathDirectory, refAsset.relative_path)
					: AssetBrowserSupport::IsSameOrAncestorPath(pathDirectory, refAsset.parent_path);
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
	}

	namespace AssetBrowserSupport
	{
		const std::array<AssetTypeFilterOption, 11>& GetAssetTypeFilters()
		{
			return kAssetTypeFilters;
		}

		void NormalizeSelection(
			const AssetDatabaseService& refService,
			AssetBrowserPanelState& refState)
		{
			std::vector<uint64_t> vecValidAssetIds{};
			vecValidAssetIds.reserve(refState.vecSelectedAssetIds.size());
			for (const uint64_t uAssetId : refState.vecSelectedAssetIds)
			{
				if (uAssetId == 0 || refService.FindById(uAssetId) == nullptr)
				{
					continue;
				}

				if (std::find(vecValidAssetIds.begin(), vecValidAssetIds.end(), uAssetId) == vecValidAssetIds.end())
				{
					vecValidAssetIds.push_back(uAssetId);
				}
			}
			refState.vecSelectedAssetIds = std::move(vecValidAssetIds);

			if (refState.uSelectedAssetId != 0 && !IsAssetSelected(refState, refState.uSelectedAssetId))
			{
				refState.uSelectedAssetId =
					refState.vecSelectedAssetIds.empty()
					? 0
					: refState.vecSelectedAssetIds.back();
			}
			if (refState.uSelectionAnchorAssetId != 0 && !IsAssetSelected(refState, refState.uSelectionAnchorAssetId))
			{
				refState.uSelectionAnchorAssetId =
					refState.uSelectedAssetId != 0
					? refState.uSelectedAssetId
					: (refState.vecSelectedAssetIds.empty() ? 0 : refState.vecSelectedAssetIds.front());
			}
		}

		AssetBrowserFrameData BuildFrameData(
			const AssetBrowserPanelDeps& refDeps,
			AssetBrowserPanelState& refState)
		{
			AssetBrowserFrameData frameData{};
			if (!refDeps.pAssetDatabaseService)
			{
				return frameData;
			}

			const std::vector<AshEngine::AssetInfo>& vecAssets = refDeps.pAssetDatabaseService->GetItems();
			NormalizeSelection(*refDeps.pAssetDatabaseService, refState);
			refState.iTypeFilterIndex = std::clamp(
				refState.iTypeFilterIndex,
				0,
				static_cast<int32_t>(kAssetTypeFilters.size() - 1));
			frameData.pTypeFilter = &kAssetTypeFilters[refState.iTypeFilterIndex];
			frameData.directoryTreeData = BuildDirectoryTreeData(vecAssets);
			frameData.pathActiveDirectory =
				refState.strActiveDirectoryPath.empty()
				? std::filesystem::path{}
				: std::filesystem::path(refState.strActiveDirectoryPath);
			frameData.bActiveDirectoryExists = DirectoryExists(frameData.directoryTreeData, frameData.pathActiveDirectory);
			frameData.pSelectedAsset = GetSelectedAsset(*refDeps.pAssetDatabaseService, refState.uSelectedAssetId);
			if (!frameData.pSelectedAsset &&
				refDeps.pSelectionService &&
				refDeps.pSelectionService->GetSelection().eKind == EditorSelectionKind::Asset)
			{
				frameData.pSelectedAsset =
					refDeps.pAssetDatabaseService->FindById(refDeps.pSelectionService->GetSelection().uId);
				refState.uSelectedAssetId = frameData.pSelectedAsset ? frameData.pSelectedAsset->id : 0u;
				refState.uSelectionAnchorAssetId = refState.uSelectedAssetId;
				refState.vecSelectedAssetIds.clear();
				if (refState.uSelectedAssetId != 0)
				{
					refState.vecSelectedAssetIds.push_back(refState.uSelectedAssetId);
				}
			}

			const std::string strLoweredSearchText = ToLowerCopy(refState.strSearchText);
			frameData.strScopeLabel = GetAssetScopeLabel(frameData.pathActiveDirectory);
			frameData.strFilterSummary = BuildFilterSummary(
				refState.strSearchText,
				*frameData.pTypeFilter,
				refState.eViewMode);
			frameData.vecVisibleItems = BuildVisibleItems(
				vecAssets,
				frameData.pathActiveDirectory,
				strLoweredSearchText,
				*frameData.pTypeFilter);
			SortVisibleAssets(frameData.vecVisibleItems);
			frameData.uFilteredCount = static_cast<uint32_t>(frameData.vecVisibleItems.size());
			frameData.uSelectedCount = static_cast<uint32_t>(refState.vecSelectedAssetIds.size());
			frameData.bFiltersActive = HasActiveFilters(refState);
			frameData.bSelectedAssetVisible = IsSelectedAssetVisible(
				frameData.pSelectedAsset,
				frameData.bActiveDirectoryExists,
				frameData.pathActiveDirectory,
				refState.strSearchText,
				*frameData.pTypeFilter);
			frameData.strLastError = refDeps.pAssetDatabaseService->GetLastError();
			return frameData;
		}

		const AshEngine::AssetInfo* GetSelectedAsset(const AssetDatabaseService& refService, uint64_t uSelectedId)
		{
			return uSelectedId == 0 ? nullptr : refService.FindById(uSelectedId);
		}

		bool IsAssetSelected(const AssetBrowserPanelState& refState, uint64_t uSelectedId)
		{
			return
				uSelectedId != 0 &&
				std::find(
					refState.vecSelectedAssetIds.begin(),
					refState.vecSelectedAssetIds.end(),
					uSelectedId) != refState.vecSelectedAssetIds.end();
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

		bool HasActiveFilters(const AssetBrowserPanelState& refState)
		{
			return
				!refState.strSearchText.empty() ||
				refState.iTypeFilterIndex != 0 ||
				!refState.strActiveDirectoryPath.empty();
		}

		void SyncSettings(const AssetBrowserPanelDeps& refDeps, const AssetBrowserPanelState& refState)
		{
			if (!refDeps.pSettingsService)
			{
				return;
			}

			EditorSettings& settings = refDeps.pSettingsService->GetSettings();
			settings.strAssetBrowserSearchText = refState.strSearchText;
			settings.strAssetBrowserActiveDirectory = refState.strActiveDirectoryPath;
			settings.iAssetBrowserTypeFilter = refState.iTypeFilterIndex;
			settings.iAssetBrowserViewMode = static_cast<int32_t>(refState.eViewMode);
		}

		AssetBrowserViewMode ToAssetBrowserViewMode(int32_t iValue)
		{
			return iValue == static_cast<int32_t>(AssetBrowserViewMode::Icons)
				? AssetBrowserViewMode::Icons
				: AssetBrowserViewMode::List;
		}

		const char* GetAssetItemContextPopupId()
		{
			return kAssetItemContextPopupId;
		}

		const char* GetAssetContentContextPopupId()
		{
			return kAssetContentContextPopupId;
		}

		void PushSelectedToolbarButtonStyle(AshEngine::UIContext& refUi)
		{
			PushEditorSelectedButtonStyle(refUi);
		}

		void PopSelectedToolbarButtonStyle(AshEngine::UIContext& refUi)
		{
			PopEditorSelectedButtonStyle(refUi);
		}

		void DrawToolbarSeparator(AshEngine::UIContext& refUi)
		{
			refUi.same_line(0.0f, 10.0f);
			refUi.text_colored(GetEditorMutedTextColor(refUi), "|");
			refUi.same_line(0.0f, 10.0f);
		}

		void DrawToolbarLabel(AshEngine::UIContext& refUi, const char* pLabel, const AshEngine::UIColor& color)
		{
			refUi.text_colored(color, "%s", pLabel);
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

		void DrawListItemLabel(
			AshEngine::UIContext& refUi,
			AshEngine::UITextureHandle iconHandle,
			std::string_view svLabel)
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
				GetEditorTextColor(refUi),
				strText.c_str());
			refUi.pop_window_clip_rect();
		}

		void DrawItemFeedback(AshEngine::UIContext& refUi, bool bSelected, float fRounding)
		{
			if (!bSelected && !refUi.is_item_hovered())
			{
				return;
			}

			const AshEngine::UIRect rectItem = refUi.get_item_rect();
			refUi.draw_window_rect_filled(
				rectItem,
				bSelected ? GetEditorRowSelectedFillColor(refUi) : GetEditorRowHoverFillColor(refUi),
				fRounding);
			refUi.draw_window_rect(
				rectItem,
				bSelected ? GetEditorRowSelectedOutlineColor(refUi) : GetEditorRowHoverOutlineColor(refUi),
				fRounding,
				1.0f);
		}

		EditorIconId GetAssetIconId(const AshEngine::AssetInfo& refAsset)
		{
			return refAsset.is_directory ? EditorIconId::FolderClosed : EditorIconId::File;
		}

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

		EditorTreeWidgetStyle MakeTreeStyle(AshEngine::UIContext& refUi)
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
			style.colorGuideLine = GetEditorGuideLineColor(refUi);
			style.colorDropAccent = GetEditorDropAccentColor(refUi);
			style.colorRowHoverFill = GetEditorRowHoverFillColor(refUi);
			style.colorRowHoverOutline = GetEditorRowHoverOutlineColor(refUi);
			style.colorRowSelectedFill = GetEditorRowSelectedFillColor(refUi);
			style.colorRowSelectedOutline = GetEditorRowSelectedOutlineColor(refUi);
			return style;
		}

		bool IsSameOrAncestorPath(
			const std::filesystem::path& pathAncestor,
			const std::filesystem::path& pathDescendant)
		{
			return EditorPathUtils::IsSameOrAncestorPath(pathAncestor, pathDescendant);
		}
	}
}
