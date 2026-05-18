#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/ISceneHierarchyActionTarget.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Panels/SceneHierarchy/SceneHierarchyDeleteModal.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"
#include "Panels/SceneHierarchy/SceneHierarchyRenameModal.h"
#include "Panels/SceneHierarchy/SceneHierarchyReparentModal.h"
#include "Panels/SceneHierarchy/SceneHierarchySearchResultsView.h"
#include "Panels/SceneHierarchy/SceneHierarchyToolbarView.h"
#include "Panels/SceneHierarchy/SceneHierarchyTreeView.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class EditorEventBus;

	class SceneHierarchyPanel final
		: public EditorPanel
		, public ISceneHierarchyActionTarget
	{
	public:
		explicit SceneHierarchyPanel(SceneHierarchyPanelDeps deps = {});

	public:
		void OnAttach() override;
		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		void BindEventBus(EditorEventBus* pEventBus);
		void ExecuteCreateRoot() override;
		void ExecuteCreateChildFromSelection() override;
		void ExecuteCopySelection() override;
		void ExecutePasteSelection() override;
		void ExecuteDuplicateSelection() override;
		bool CanPasteSelection() const override;
		void RequestRenameSelected(AshEngine::UIContext* pUiContext) override;
		void RequestReparentSelected(AshEngine::UIContext* pUiContext) override;
		void RequestDeleteSelected(AshEngine::UIContext* pUiContext) override;

	private:
		void ClearDeps();
		void UnsubscribeEvents();

	private:
		SceneHierarchyPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		SceneHierarchyPanelState _state{};
		SceneHierarchyToolbarView _toolbarView{};
		SceneHierarchyTreeView _treeView{};
		SceneHierarchySearchResultsView _searchResultsView{};
		SceneHierarchyRenameModal _renameModal{};
		SceneHierarchyReparentModal _reparentModal{};
		SceneHierarchyDeleteModal _deleteModal{};
	};
}
