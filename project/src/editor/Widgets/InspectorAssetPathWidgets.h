#pragma once

#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UICommon.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <string>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class AssetDatabaseService;
	class DragDropTransferService;

	struct InspectorAssetPathWidgetState
	{
		std::vector<std::string>* pVecRecentPaths = nullptr;
		std::string* pStrSearch = nullptr;
	};

	struct InspectorAssetPathFieldDesc
	{
		const char* pLabel = nullptr;
		const char* pPopupId = nullptr;
		const char* pPickerTitle = nullptr;
		const char* pBrowseLabel = "Browse";
		const char* pDropLabelEmpty = nullptr;
		const char* pDropLabelReplace = nullptr;
		InspectorFieldSpec fieldSpec{};
		InspectorFieldSpec browseSpec{};
		InspectorFieldSpec dropZoneSpec{};
		std::vector<AshEngine::AssetType> vecAllowedAssetTypes{};
		AshEngine::UIVec2 pickerSize{ 300.0f, 250.0f };
		float fBrowseButtonWidth = 60.0f;
		bool bDrawDropZone = false;
	};

	bool DrawInspectorAssetPathField(
		AshEngine::UIContext& refUi,
		std::string& strPath,
		const InspectorAssetPathFieldDesc& refDesc,
		InspectorAssetPathWidgetState& refState,
		const AssetDatabaseService* pAssetDatabaseService,
		const DragDropTransferService* pDragDropTransferService);
}
