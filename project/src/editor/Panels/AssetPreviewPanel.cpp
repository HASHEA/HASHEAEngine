#include "Panels/AssetPreviewPanel.h"

#include "Base/hlog.h"
#include "Core/AssetPresentationUtils.h"
#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UIContext.h"
#include "Services/AssetDatabaseService.h"
#include "Services/AssetPreviewService.h"
#include "Widgets/EditorThemeColors.h"

#include <memory>
#include <string>
#include <string_view>

namespace AshEditor
{
	struct AssetPreviewPanelState
	{
		uint64_t uPreviewAssetId = 0;
		std::string strPreviewText{};
		std::string strPreviewStatus{};
	};

	namespace
	{
		constexpr AshEngine::UIColor kAssetPreviewWarningColor{ 0.95f, 0.80f, 0.48f, 1.0f };
		constexpr float kAssetPreviewSummaryLabelWidth = 112.0f;
		constexpr AshEngine::UITableFlags kAssetPreviewSummaryTableFlags =
			AshEngine::UITableFlagBits::SizingStretchProp |
			AshEngine::UITableFlagBits::BordersInner;

		bool BeginSummaryTable(AshEngine::UIContext& refUi, const char* pTableId)
		{
			if (!refUi.begin_table(pTableId, 2, kAssetPreviewSummaryTableFlags))
			{
				return false;
			}

			refUi.table_setup_column("Label", AshEngine::UITableColumnFlagBits::WidthFixed, kAssetPreviewSummaryLabelWidth);
			refUi.table_setup_column("Value", AshEngine::UITableColumnFlagBits::WidthStretch);
			return true;
		}

		void DrawSummaryRow(AshEngine::UIContext& refUi, const char* pLabel, std::string_view svValue)
		{
			refUi.table_next_row();
			refUi.table_next_column();
			refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pLabel);
			refUi.table_next_column();
			const std::string strValue(svValue.empty() ? "-" : svValue);
			refUi.text_wrapped("%s", strValue.c_str());
		}

		void DrawPanelIntro(AshEngine::UIContext& refUi, const char* pTitle, const char* pDescription)
		{
			refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", pTitle);
			refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pDescription);
			refUi.separator();
		}

		void DrawEmptyState(AshEngine::UIContext& refUi)
		{
			DrawPanelIntro(refUi, "Asset Preview", "Open a preview from Asset Browser when you want to inspect resource contents.");
			refUi.bullet_text("Hover tooltip keeps basic metadata lightweight.");
			refUi.bullet_text("Right-click an asset and choose Preview to open detailed content here.");
		}
	}

	AssetPreviewPanel::AssetPreviewPanel(AssetPreviewPanelDeps deps)
		: EditorPanel(EditorPanelIds::AssetPreview, EditorWindowTitles::AssetPreview)
		, _deps(deps)
		, _upState(std::make_unique<AssetPreviewPanelState>())
	{
		SetOpen(false);
	}

	AssetPreviewPanel::~AssetPreviewPanel() = default;

	void AssetPreviewPanel::OnAttach()
	{
		HLogInfo("AssetPreviewPanel attached.");
	}

	void AssetPreviewPanel::OnDetach()
	{
		ClearDeps();
	}

	void AssetPreviewPanel::OnGui(const EditorFrameContext& refFrameContext)
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

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!_deps.pAssetPreviewService || !_deps.pAssetDatabaseService)
		{
			refUi.text_unformatted("Asset preview dependencies are unavailable.");
			EndPanelWindow(refFrameContext);
			return;
		}

		if (!_deps.pAssetPreviewService->HasPreviewAsset())
		{
			DrawEmptyState(refUi);
			EndPanelWindow(refFrameContext);
			return;
		}

		const EditorSelection& refSelection = _deps.pAssetPreviewService->GetPreviewAsset();
		const AshEngine::AssetInfo* pAsset = _deps.pAssetDatabaseService->FindById(refSelection.uId);
		if (!pAsset)
		{
			refUi.text_unformatted("Selected asset no longer exists.");
			EndPanelWindow(refFrameContext);
			return;
		}

		AssetPreviewPanelState& state = GetState();
		if (state.uPreviewAssetId != pAsset->id)
		{
			state.uPreviewAssetId = pAsset->id;
			state.strPreviewText.clear();
			state.strPreviewStatus.clear();

			if (SupportsTextAssetPreview(*pAsset))
			{
				std::string strSourceText{};
				if (_deps.pAssetDatabaseService->LoadTextById(pAsset->id, strSourceText))
				{
					state.strPreviewText = BuildTextAssetPreview(strSourceText);
				}
				else
				{
					state.strPreviewStatus = "The editor could not load text content for this asset.";
				}
			}
		}

		const std::string strTitle = GetAssetDisplayLabel(*pAsset);
		const std::string strPath = pAsset->relative_path.generic_string();
		DrawPanelIntro(refUi, strTitle.c_str(), strPath.c_str());
		if (BeginSummaryTable(refUi, "AssetPreviewSummary"))
		{
			DrawSummaryRow(refUi, "Type", AssetDatabaseService::GetTypeLabel(pAsset->type));
			DrawSummaryRow(refUi, "Size", pAsset->is_directory ? std::string("-") : FormatAssetFileSize(pAsset->file_size));
			DrawSummaryRow(refUi, "Modified", FormatAssetLastWriteTime(*_deps.pAssetDatabaseService, *pAsset));
			DrawSummaryRow(refUi, "State", AssetDatabaseService::GetLoadStateLabel(_deps.pAssetDatabaseService->GetLoadState(pAsset->id)));
			DrawSummaryRow(refUi, "Path", strPath);
			refUi.end_table();
		}

		refUi.separator();
		refUi.text_colored(GetEditorHeadingTextColor(refUi), "Content Preview");
		refUi.text_colored(GetEditorMutedTextColor(refUi), "This panel is reserved for richer asset inspection.");

		if (pAsset->is_directory)
		{
			refUi.text_colored(kAssetPreviewWarningColor, "Folders do not have inline content preview.");
		}
		else if (SupportsTextAssetPreview(*pAsset))
		{
			if (!state.strPreviewStatus.empty())
			{
				refUi.text_colored(kAssetPreviewWarningColor, "%s", state.strPreviewStatus.c_str());
			}
			else
			{
				const AshEngine::UIVec2 vecAvail = refUi.get_content_region_avail();
				const float fPreviewHeight = vecAvail.y > 160.0f ? vecAvail.y : 240.0f;
				refUi.input_text_multiline(
					"##AssetPreviewText",
					state.strPreviewText,
					{ 0.0f, fPreviewHeight },
					AshEngine::UIInputTextFlagBits::ReadOnly |
					AshEngine::UIInputTextFlagBits::AllowTabInput);
			}
		}
		else
		{
			refUi.text_colored(
				kAssetPreviewWarningColor,
				"Inline preview is not implemented for this asset type yet. Texture/model/material graph preview can be added later.");
		}

		EndPanelWindow(refFrameContext);
	}

	void AssetPreviewPanel::ClearDeps()
	{
		_deps = {};
	}

	AssetPreviewPanelState& AssetPreviewPanel::GetState()
	{
		return *_upState;
	}

	const AssetPreviewPanelState& AssetPreviewPanel::GetState() const
	{
		return *_upState;
	}
}
