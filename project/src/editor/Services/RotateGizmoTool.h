#pragma once

#include "Services/EditorGizmoTypesInternal.h"

#include <glm/fwd.hpp>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class IEditorCommandExecutor;
	class SceneService;
	class SelectionService;
}

namespace AshEditor
{
	class RotateGizmoTool final
	{
	public:
		static bool TryBuildVisual(
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const EditorGizmoInternal::GizmoBasis& refBasis,
			EditorGizmoInternal::RotateGizmoVisual& outVisual);
		static EditorGizmoInternal::HandleHit HitTestHandle(
			const EditorGizmoInternal::RotateGizmoVisual& refVisual,
			const glm::vec2& vecMousePosition);
		static void Draw(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const EditorGizmoInternal::RotateGizmoVisual& refVisual,
			const EditorGizmoInternal::HandleHit& refHoveredHandle,
			const EditorGizmoInternal::DragSession& refDragSession);

		bool TryBuildDragSession(
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const SceneService& refSceneService,
			const SelectionService& refSelectionService,
			const EditorGizmoInternal::GizmoBasis& refBasis,
			const EditorGizmoInternal::RotateGizmoVisual& refVisual,
			const EditorGizmoInternal::HandleHit& refHoveredHandle,
			const EditorGizmoState& refGizmoState,
			const glm::vec2& vecMousePosition,
			EditorGizmoInternal::DragSession& outDragSession) const;
		void BeginDragSession(
			const EditorGizmoInternal::DragSession& refDragSession,
			bool bTransactionOpened);
		bool TryUpdateDrag(
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const SceneService& refSceneService,
			const EditorGizmoState& refGizmoState,
			const glm::vec2& vecMousePosition,
			EditorGizmoInternal::GizmoDragUpdate& outUpdate) const;
		void SetHoveredHandle(const EditorGizmoInternal::HandleHit& refHoveredHandle);
		void ClearHoveredHandle();
		const EditorGizmoInternal::HandleHit& GetHoveredHandle() const;
		const EditorGizmoInternal::DragSession& GetDragSession() const;
		void ResetInteraction();
		void CancelInteraction(IEditorCommandExecutor& refCommandExecutor);

	private:
		EditorGizmoInternal::DragSession _dragSession{};
		EditorGizmoInternal::HandleHit _hoveredHandle{};
	};
}
