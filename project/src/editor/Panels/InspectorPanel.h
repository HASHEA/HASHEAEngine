#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/InspectorPanelDeps.h"
#include "Core/EditorPanel.h"

#include <memory>

namespace AshEngine
{
	class Entity;
	struct MeshComponent;
	class UIContext;
}

namespace AshEditor
{
	class EditorEventBus;
	struct InspectorPanelState;

	class InspectorPanel final : public EditorPanel
	{
	public:
		explicit InspectorPanel(InspectorPanelDeps deps = {});
		~InspectorPanel() override;

	public:
		void OnAttach() override;
		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;
		void BindEventBus(EditorEventBus* pEventBus);

	private:
		void ClearDeps();
		void UnsubscribeEvents();
		void DrawEntityInspector(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawAddComponentMenu(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawComponentSections(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawIdentitySection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawTransformSection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		bool DrawCameraSection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		bool DrawLightSection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		bool DrawMeshSection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		bool DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& meshComponent);
		bool HasPendingIdentityChanges() const;
		bool HasPendingTransformChanges() const;
		bool HasPendingCameraChanges() const;
		bool HasPendingLightChanges() const;
		bool HasPendingMeshChanges() const;
		void ResetEntityDrafts();
		void SyncEntityDrafts(const AshEngine::Entity& entity);
		void SyncCameraDraft(const AshEngine::Entity& entity);
		void SyncLightDraft(const AshEngine::Entity& entity);
		void SyncMeshDraft(const AshEngine::Entity& entity);
		bool CommitIdentityDraft(AshEngine::Entity entity);
		bool CommitTransformDraft(AshEngine::Entity entity);
		bool CommitCameraDraft(AshEngine::Entity entity);
		bool CommitLightDraft(AshEngine::Entity entity);
		bool CommitMeshDraft(AshEngine::Entity entity);
		InspectorPanelState& GetState();
		const InspectorPanelState& GetState() const;

	private:
		InspectorPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::unique_ptr<InspectorPanelState> _upState{};
	};
}
