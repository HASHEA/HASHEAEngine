#pragma once

#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Function/Gui/UICommon.h"
#include "Services/EditorGizmoService.h"
#include "Services/EditorViewportService.h"

#include <string>
#include <vector>

namespace AshEngine
{
	class InputState;
	class UIContext;
}

namespace AshEditor
{
	struct DragDropTransferData;
}

namespace AshEditor::ViewportPanelSupport
{

	bool IsPointInRect(const AshEngine::UIRect& refRect, const AshEngine::UIVec2& refPoint);
	AshEngine::UIRect MakeRectFromPoints(
		const AshEngine::UIVec2& refFirstPoint,
		const AshEngine::UIVec2& refSecondPoint);
	bool RectsIntersect(const AshEngine::UIRect& refLeftRect, const AshEngine::UIRect& refRightRect);
	float DistanceSquared(const AshEngine::UIVec2& refFirstPoint, const AshEngine::UIVec2& refSecondPoint);
	AshEngine::UIVec2 GetMouseScreenPosition(const AshEngine::InputState& refInput);

	void DrawViewportDisplayOptionsMenu(
		AshEngine::UIContext& refUi,
		EditorViewportPresentation& refPresentation);
	void DrawSceneViewportHelperOptionsMenu(
		AshEngine::UIContext& refUi,
		EditorViewportPresentation& refPresentation);
	void DrawViewportInteractionOptionsMenu(
		const ViewportPanelDeps& refDeps,
		AshEngine::UIContext& refUi,
		const std::string& strViewportId,
		EditorViewportPresentation& refPresentation);

	std::vector<std::string> MakeOverlayLines(
		const EditorViewportInstance& refViewport,
		const EditorViewportPresentation& refPresentation,
		const EditorViewportRenderState* pRenderState,
		bool bIsPrimary);
	void DrawOperationHints(
		AshEngine::UIContext& refUi,
		float fContentOriginX,
		float fContentOriginY,
		float fContentWidth,
		float fContentHeight);

	bool HasSceneViewportOverlayHelpersEnabled(const EditorViewportPresentation& refPresentation);
	void DrawSceneViewportOverlayHelpers(
		const ViewportPanelDeps& refDeps,
		AshEngine::UIContext& refUi,
		const EditorViewportPresentation& refPresentation,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent);
	void DrawSceneGizmoOverlay(
		const ViewportPanelDeps& refDeps,
		AshEngine::UIContext& refUi,
		const EditorViewportPresentation* pPresentation,
		const AshEngine::UIRect& rectContent);
	EditorGizmoService::InteractionResult UpdateSceneGizmoInteraction(
		const ViewportPanelDeps& refDeps,
		AshEngine::UIContext& refUi,
		const AshEngine::InputState& refInput,
		bool bViewportHovered,
		const AshEngine::UIRect& rectContent);

	void ApplySceneViewportClickSelection(
		const ViewportPanelDeps& refDeps,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		const AshEngine::UIVec2& vecMousePosition,
		AshEngine::UIModifierFlags uModifiers);
	void ApplySceneViewportBoxSelection(
		const ViewportPanelDeps& refDeps,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		const AshEngine::UIRect& rectSelection,
		AshEngine::UIModifierFlags uModifiers);

	bool HandleAssetDropInstantiate(
		const ViewportPanelDeps& refDeps,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		const AshEngine::UIVec2& vecMousePosition,
		const DragDropTransferData& refData);
}
