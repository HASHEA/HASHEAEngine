#pragma once

#include "Core/EditorViewportInputState.h"
#include "Core/EditorGizmoTypes.h"
#include "Function/Render/ScenePresentationHandles.h"
#include "Services/EditorGizmoServiceTypes.h"

#include <memory>

namespace AshEditor::EditorGizmoInternal
{
	struct GizmoBasis;
}

namespace AshEditor
{
	class AssetDatabaseService;
	class IEditorCommandExecutor;
	class MoveScaleGizmoTool;
	class RotateGizmoTool;
	class SceneService;
	class SelectionService;
}

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class EditorGizmoService final
	{
	public:
		using ViewportContext = EditorGizmoViewportContext;
		using InteractionResult = EditorGizmoInteractionResult;

	public:
		EditorGizmoService();
		~EditorGizmoService();

		EditorGizmoService(const EditorGizmoService&) = delete;
		EditorGizmoService& operator=(const EditorGizmoService&) = delete;

		InteractionResult UpdateSceneViewportInteraction(
			AshEngine::UIContext& refUi,
			const EditorViewportInputState& refInput,
			bool bViewportHovered,
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			IEditorCommandExecutor& refCommandExecutor,
			const EditorGizmoState& refGizmoState,
			const ViewportContext& refViewportContext);

		void DrawSceneViewportGizmo(
			AshEngine::UIContext& refUi,
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			const EditorGizmoState& refGizmoState,
			bool bDrawSelectionHelpers,
			AshEngine::SceneViewBindingHandle sceneViewBinding,
			const ViewportContext& refViewportContext);

	private:
		using GizmoBasis = EditorGizmoInternal::GizmoBasis;

	private:
		static bool TryBuildGizmoBasis(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			const EditorGizmoState& refGizmoState,
			GizmoBasis& outBasis);
		void ResetInteraction();
		void CancelInteraction(IEditorCommandExecutor& refCommandExecutor);

	private:
		std::unique_ptr<MoveScaleGizmoTool> _upMoveScaleTool{};
		std::unique_ptr<RotateGizmoTool> _upRotateTool{};
	};
}
