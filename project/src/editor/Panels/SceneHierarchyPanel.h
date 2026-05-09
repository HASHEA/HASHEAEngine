#pragma once
#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/EditorSceneTypes.h"
#include "Core/ISceneHierarchyActionTarget.h"
#include "Core/PanelDeps/SceneHierarchyPanelDeps.h"
#include "Widgets/EditorTreeWidget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	class Entity;
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
		void RequestRenameSelected(AshEngine::UIContext* pUiContext) override;
		void RequestReparentSelected(AshEngine::UIContext* pUiContext) override;
		void RequestDeleteSelected(AshEngine::UIContext* pUiContext) override;

	private:
		void ClearDeps();
		void UnsubscribeEvents();
		void ResetPendingRenameState();
		void ResetPendingReparentState();
		void ResetPendingDeleteState();
		void ResetTransientState();
		void BeginRenameSelectedEntity(AshEngine::UIContext* pUiContext);
		void BeginReparentSelectedEntity(AshEngine::UIContext* pUiContext);
		void BeginDeleteSelectedEntity(AshEngine::UIContext* pUiContext);
		void CreateEntity(SceneEntityId uParentId);
		void DestroyEntity(SceneEntityId uEntityId);
		void DrawToolbar(const EditorFrameContext& refFrameContext);
		void DrawEntityTree(
			EditorTreeWidget& refTreeWidget,
			const EditorFrameContext& refFrameContext,
			const AshEngine::Entity& refEntity,
			bool bIsLastSibling);
		void HandleRootAppendDropTarget(
			EditorTreeWidget& refTreeWidget,
			bool bDraggingSceneEntity);
		void DrawEntityContextMenu(const EditorFrameContext& refFrameContext, const AshEngine::Entity& refEntity);
		void DrawContentContextMenu(const EditorFrameContext& refFrameContext, SceneEntityId uSelectedEntityId);
		bool IsSceneEntityDragActive(const AshEngine::UIContext* pUiContext) const;
		void DrawRenameModal(const EditorFrameContext& refFrameContext);
		void DrawReparentModal(const EditorFrameContext& refFrameContext);
		void DrawDeleteModal(const EditorFrameContext& refFrameContext);

	private:
		SceneHierarchyPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		SceneEntityId _uPendingRenameEntityId = 0;
		std::string _strPendingRenameValue{};
		SceneEntityId _uPendingReparentEntityId = 0;
		int32_t _iPendingReparentIndex = 0;
		int32_t _iPendingReparentInsertIndex = 0;
		std::vector<SceneEntityId> _vecPendingReparentParentEntityIds{};
		std::vector<std::string> _vecPendingReparentParentLabels{};
		SceneEntityId _uPendingDeleteEntityId = 0;
		std::string _strPendingDeleteEntityName{};
		EditorTreeWidgetState _treeWidgetStateEntities{};
		bool _bOpenRenamePopup = false;
		bool _bOpenReparentPopup = false;
		bool _bOpenDeletePopup = false;
	};
}
