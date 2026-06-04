#include "App/PanelBootstrapper.h"

#include "Core/EditorEventBus.h"
#include "Core/EditorIds.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/AssetPreviewPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ViewportPanel.h"
#include "Services/EditorViewportCameraService.h"
#include "Shell/PanelManager.h"

namespace AshEditor
{
	namespace
	{
		ViewportPanelDeps MakeViewportPanelDeps(const PanelBootstrapContext& refContext)
		{
			ViewportPanelDeps deps{};
			deps.pAssetDatabaseService = refContext.pAssetDatabaseService;
			deps.pViewportService = refContext.pViewportService;
			deps.pViewportCameraService = refContext.pViewportCameraService;
			deps.pSettingsService = refContext.pSettingsService;
			deps.pSceneService = refContext.pSceneService;
			deps.pGizmoState = refContext.pGizmoState;
			deps.pGizmoService = refContext.pGizmoService;
			deps.pSelectionService = refContext.pSelectionService;
			deps.pDragDropTransferService = refContext.pDragDropTransferService;
			deps.pCommandExecutor = refContext.pCommandExecutor;
			return deps;
		}

		ConsolePanelDeps MakeConsolePanelDeps(const PanelBootstrapContext& refContext)
		{
			ConsolePanelDeps deps{};
			deps.pSettingsService = refContext.pSettingsService;
			return deps;
		}

		AssetBrowserPanelDeps MakeAssetBrowserPanelDeps(const PanelBootstrapContext& refContext, PanelManager& refPanelManager)
		{
			AssetBrowserPanelDeps deps{};
			deps.pAssetDatabaseService = refContext.pAssetDatabaseService;
			deps.pAssetPreviewService = refContext.pAssetPreviewService;
			deps.pCommandService = refContext.pCommandService;
			deps.pIconService = refContext.pIconService;
			deps.pSelectionService = refContext.pSelectionService;
			deps.pSettingsService = refContext.pSettingsService;
			deps.pShortcutService = refContext.pShortcutService;
			deps.pDragDropTransferService = refContext.pDragDropTransferService;
			deps.pPanelManager = &refPanelManager;
			deps.pCommandExecutor = refContext.pCommandExecutor;
			return deps;
		}

		AssetPreviewPanelDeps MakeAssetPreviewPanelDeps(const PanelBootstrapContext& refContext)
		{
			AssetPreviewPanelDeps deps{};
			deps.pAssetDatabaseService = refContext.pAssetDatabaseService;
			deps.pAssetPreviewService = refContext.pAssetPreviewService;
			return deps;
		}

		SceneHierarchyPanelDeps MakeSceneHierarchyPanelDeps(const PanelBootstrapContext& refContext)
		{
			SceneHierarchyPanelDeps deps{};
			deps.pSelectionService = refContext.pSelectionService;
			deps.pSceneService = refContext.pSceneService;
			deps.pCommandService = refContext.pCommandService;
			deps.pCommandExecutor = refContext.pCommandExecutor;
			deps.pIconService = refContext.pIconService;
			deps.pDragDropTransferService = refContext.pDragDropTransferService;
			return deps;
		}

		InspectorPanelDeps MakeInspectorPanelDeps(const PanelBootstrapContext& refContext)
		{
			InspectorPanelDeps deps{};
			deps.pSelectionService = refContext.pSelectionService;
			deps.pSceneService = refContext.pSceneService;
			deps.pAssetDatabaseService = refContext.pAssetDatabaseService;
			deps.pCommandExecutor = refContext.pCommandExecutor;
			deps.pDragDropTransferService = refContext.pDragDropTransferService;
			return deps;
		}
	}

	PanelBootstrapResult PanelBootstrapper::CreateDefaultPanels(
		PanelManager& refPanelManager,
		const PanelBootstrapContext& refContext,
		EditorEventBus& refEventBus)
	{
		PanelBootstrapResult result{};
		refPanelManager.ClearRuntimeState();
		ViewportPanel* pViewportPanel = refPanelManager.CreatePanel<ViewportPanel>(
			EditorViewportIds::Scene,
			EditorPanelIds::SceneViewport,
			EditorWindowTitles::Scene,
			MakeViewportPanelDeps(refContext));
		ViewportPanel* pGameViewportPanel = refPanelManager.CreatePanel<ViewportPanel>(
			EditorViewportIds::Game,
			EditorPanelIds::GameViewport,
			EditorWindowTitles::Game,
			MakeViewportPanelDeps(refContext));
		SceneHierarchyPanel* pSceneHierarchyPanel = refPanelManager.CreatePanel<SceneHierarchyPanel>(
			MakeSceneHierarchyPanelDeps(refContext));
		InspectorPanel* pInspectorPanel = refPanelManager.CreatePanel<InspectorPanel>(
			MakeInspectorPanelDeps(refContext));
		AssetPreviewPanel* pAssetPreviewPanel = refPanelManager.CreatePanel<AssetPreviewPanel>(
			MakeAssetPreviewPanelDeps(refContext));
		ConsolePanel* pConsolePanel = refPanelManager.CreatePanel<ConsolePanel>(
			MakeConsolePanelDeps(refContext));
		AssetBrowserPanel* pAssetBrowserPanel = refPanelManager.CreatePanel<AssetBrowserPanel>(
			MakeAssetBrowserPanelDeps(refContext, refPanelManager));
		result.pSceneHierarchyActionTarget = pSceneHierarchyPanel;
		result.pAssetBrowserActionTarget = pAssetBrowserPanel;
		if (pViewportPanel)
		{
			pViewportPanel->BindEventBus(&refEventBus);
		}
		if (pGameViewportPanel)
		{
			pGameViewportPanel->SetOpen(false);
			pGameViewportPanel->BindEventBus(&refEventBus);
		}
		if (pSceneHierarchyPanel)
		{
			pSceneHierarchyPanel->BindEventBus(&refEventBus);
		}
		if (pInspectorPanel)
		{
			pInspectorPanel->BindEventBus(&refEventBus);
		}
		(void)pAssetPreviewPanel;
		if (pConsolePanel)
		{
			pConsolePanel->BindEventBus(&refEventBus);
		}
		if (pAssetBrowserPanel)
		{
			pAssetBrowserPanel->BindEventBus(&refEventBus);
		}
		return result;
	}
}
