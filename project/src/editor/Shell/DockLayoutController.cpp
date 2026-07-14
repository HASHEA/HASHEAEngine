#include "Shell/DockLayoutController.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"

#include <algorithm>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kWorkspaceHostWindowName = "Editor Workspace";
		constexpr const char* kWorkspaceDockspaceName = "EditorWorkspaceDockspace";
	}

	void DockLayoutController::RequestLayoutReset()
	{
		_bResetLayoutRequested = true;
	}

	void DockLayoutController::ClearRuntimeState()
	{
		_bResetLayoutRequested = false;
		_bDefaultDockLayoutBuilt = false;
	}

	void DockLayoutController::DrawWorkspaceHost(AshEngine::UIContext& refUi, const float fBottomInset)
	{
		const AshEngine::UIRect rectMainViewport = refUi.get_main_viewport_rect();
		const AshEngine::UIVec2 vecWorkspaceSize{
			rectMainViewport.width,
			std::max(0.0f, rectMainViewport.height - std::max(0.0f, fBottomInset))
		};
		refUi.set_next_window_viewport(refUi.get_main_viewport_id());
		refUi.set_next_window_position({ rectMainViewport.x, rectMainViewport.y }, AshEngine::UIConditionFlagBits::Always);
		refUi.set_next_window_size(vecWorkspaceSize, AshEngine::UIConditionFlagBits::Always);
		refUi.push_style_var(AshEngine::UIStyleVarKind::WindowPadding, { 0.0f, 0.0f });
		const bool bHostOpen = refUi.begin_dockspace_host_window(
			kWorkspaceHostWindowName,
			nullptr,
			AshEngine::UIWindowFlagBits::NoSavedSettings);
		refUi.pop_style_var();

		if (!bHostOpen)
		{
			refUi.end_window();
			return;
		}

		const AshEngine::UIDockNodeId uDockspaceId = refUi.dock_space(
			kWorkspaceDockspaceName,
			{},
			AshEngine::UIDockNodeFlagBits::PassthruCentralNode);
		if (uDockspaceId != 0u && (!_bDefaultDockLayoutBuilt || _bResetLayoutRequested))
		{
			BuildDefaultDockLayout(refUi, uDockspaceId, vecWorkspaceSize);
			_bDefaultDockLayoutBuilt = true;
			_bResetLayoutRequested = false;
		}
		if (uDockspaceId != 0u)
		{
			refUi.ensure_dock_node_tab_bar_visible(uDockspaceId, true);
		}

		refUi.end_window();
	}

	void DockLayoutController::BuildDefaultDockLayout(
		AshEngine::UIContext& refUi,
		const AshEngine::UIDockNodeId uDockspaceId,
		const AshEngine::UIVec2& refSize) const
	{
		if (uDockspaceId == 0u)
		{
			return;
		}

		refUi.dock_builder_remove_node(uDockspaceId);
		refUi.dock_builder_add_node(uDockspaceId, AshEngine::UIDockNodeFlagBits::DockSpace);
		refUi.dock_builder_set_node_size(uDockspaceId, refSize);

		AshEngine::UIDockNodeId uBottomNode = 0u;
		AshEngine::UIDockNodeId uTopNode = 0u;
		refUi.dock_builder_split_node(uDockspaceId, AshEngine::UIDirection::Down, 0.30f, &uBottomNode, &uTopNode);

		AshEngine::UIDockNodeId uLeftNode = 0u;
		AshEngine::UIDockNodeId uCenterAndRightNode = 0u;
		refUi.dock_builder_split_node(uTopNode, AshEngine::UIDirection::Left, 0.16f, &uLeftNode, &uCenterAndRightNode);

		AshEngine::UIDockNodeId uInspectorNode = 0u;
		AshEngine::UIDockNodeId uViewportNode = 0u;
		refUi.dock_builder_split_node(
			uCenterAndRightNode,
			AshEngine::UIDirection::Right,
			0.20f,
			&uInspectorNode,
			&uViewportNode);

		refUi.dock_builder_dock_window(EditorWindowTitles::SceneHierarchy, uLeftNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::Scene, uViewportNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::Game, uViewportNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::Console, uBottomNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::AssetBrowser, uBottomNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::Inspector, uInspectorNode);
		refUi.dock_builder_dock_window(EditorWindowTitles::TerrainMode, uInspectorNode);
		refUi.dock_builder_finish(uDockspaceId);
	}
}
