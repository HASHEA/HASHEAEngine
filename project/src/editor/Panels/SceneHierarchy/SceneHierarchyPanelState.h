#pragma once

#include "Core/EditorSceneTypes.h"
#include "Core/SceneSnapshotTypes.h"
#include "Widgets/EditorTreeWidget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEditor
{
	struct SceneHierarchyRenameModalState
	{
		SceneEntityId uEntityId = 0;
		std::string strValue{};
		bool bOpenPopup = false;

		void Reset()
		{
			uEntityId = 0;
			strValue.clear();
			bOpenPopup = false;
		}
	};

	struct SceneHierarchyReparentModalState
	{
		SceneEntityId uEntityId = 0;
		int32_t iParentIndex = 0;
		int32_t iInsertIndex = 0;
		std::vector<SceneEntityId> vecParentEntityIds{};
		std::vector<std::string> vecParentLabels{};
		bool bOpenPopup = false;

		void Reset()
		{
			uEntityId = 0;
			iParentIndex = 0;
			iInsertIndex = 0;
			vecParentEntityIds.clear();
			vecParentLabels.clear();
			bOpenPopup = false;
		}
	};

	struct SceneHierarchyDeleteModalState
	{
		SceneEntityId uEntityId = 0;
		std::vector<SceneEntityId> vecEntityIds{};
		std::string strDisplayName{};
		bool bOpenPopup = false;

		void Reset()
		{
			uEntityId = 0;
			vecEntityIds.clear();
			strDisplayName.clear();
			bOpenPopup = false;
		}
	};

	// Shared panel-local state bag reused by the split Scene Hierarchy subviews and modals.
	struct SceneHierarchyPanelState
	{
		SceneEntityId uCreateChildAnchorParentId = 0;
		bool bAwaitingCreateChildSelection = false;
		std::string strSearchText{};
		int32_t iEntityTypeFilterIndex = 0;
		SceneEntityId uRangeSelectionAnchorEntityId = 0;
		std::vector<SceneEntitySnapshot> vecClipboardEntitySnapshots{};
		std::vector<SceneEntityId> vecClipboardPreferredParentEntityIds{};
		EditorTreeWidgetState treeWidgetStateEntities{};
		SceneHierarchyRenameModalState renameModal{};
		SceneHierarchyReparentModalState reparentModal{};
		SceneHierarchyDeleteModalState deleteModal{};

		void ResetTransientState()
		{
			uCreateChildAnchorParentId = 0;
			bAwaitingCreateChildSelection = false;
			uRangeSelectionAnchorEntityId = 0;
			renameModal.Reset();
			reparentModal.Reset();
			deleteModal.Reset();
			treeWidgetStateEntities.ResetDragState();
		}
	};
}
