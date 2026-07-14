#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/InspectorPanelDeps.h"
#include "Core/EditorPanel.h"
#include "Panels/Inspector/IInspectorComponentHost.h"

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
	class InspectorComponentEditorRegistry;
	struct EditorSceneChangedEvent;
	struct InspectorPanelState;

	class InspectorPanel final : public EditorPanel, private IInspectorComponentHost
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
		void RequestEntityDraftRefreshFromSceneChange(const EditorSceneChangedEvent& refEvent);
		void DrawEntityInspector(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawAddComponentMenu(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawComponentSections(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawIdentitySection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		void DrawTransformSection(AshEngine::UIContext& refUi, AshEngine::Entity entity);
		bool HasPendingIdentityChanges() const;
		bool HasPendingTransformChanges() const;
		bool HasPendingCameraChanges() const;
		bool HasPendingLightChanges() const;
		bool HasPendingMeshChanges() const;
		bool HasPendingEnvironmentChanges() const;
		bool HasPendingParticleChanges() const;
		bool HasPendingTerrainChanges() const;
		void ResetEntityDrafts();
		void ResetIdentityDraftToLive(const AshEngine::Entity& entity);
		void ResetTransformDraftToLive(const AshEngine::Entity& entity);
		void ResetTransformDraftToDefaults(const AshEngine::Entity& entity);
		void ResetCameraDraftToLive(const AshEngine::Entity& entity);
		void ResetCameraDraftToDefaults(const AshEngine::Entity& entity);
		void ResetLightDraftToLive(const AshEngine::Entity& entity);
		void ResetLightDraftToDefaults(const AshEngine::Entity& entity);
		void ResetMeshDraftToLive(const AshEngine::Entity& entity);
		void ResetMeshDraftToDefaults(const AshEngine::Entity& entity);
		void ResetEnvironmentDraftToLive(const AshEngine::Entity& entity);
		void ResetEnvironmentDraftToDefaults(const AshEngine::Entity& entity);
		void ResetParticleDraftToLive(const AshEngine::Entity& entity);
		void ResetParticleDraftToDefaults(const AshEngine::Entity& entity);
		void ResetTerrainDraftToLive(const AshEngine::Entity& entity) override;
		void ResetTerrainDraftToDefaults(const AshEngine::Entity& entity) override;
		void SyncEntityDrafts(const AshEngine::Entity& entity);
		bool CommitIdentityDraft(AshEngine::Entity entity);
		bool CommitTransformDraft(AshEngine::Entity entity);
		InspectorPanelState& GetState();
		const InspectorPanelState& GetState() const;

	private:
		InspectorPanelState& AccessInspectorState() override;
		const InspectorPanelDeps& AccessInspectorDeps() const override;
		bool DrawComponentHeaderContextMenu(AshEngine::UIContext& refUi, const char* pPopupId) override;
		bool DrawComponentRemoveAction(AshEngine::UIContext& refUi, const char* pIdSuffix) override;
		bool DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& meshComponent) override;
		void SyncCameraDraft(const AshEngine::Entity& entity) override;
		void SyncLightDraft(const AshEngine::Entity& entity) override;
		void SyncMeshDraft(const AshEngine::Entity& entity) override;
		void SyncEnvironmentDraft(const AshEngine::Entity& entity) override;
		void SyncParticleDraft(const AshEngine::Entity& entity) override;
		void SyncTerrainDraft(const AshEngine::Entity& entity) override;
		bool CommitCameraDraft(AshEngine::Entity entity) override;
		bool CommitLightDraft(AshEngine::Entity entity) override;
		bool CommitMeshDraft(AshEngine::Entity entity) override;
		bool CommitEnvironmentDraft(AshEngine::Entity entity) override;
		bool CommitParticleDraft(AshEngine::Entity entity) override;
		bool CommitTerrainDraft(AshEngine::Entity entity) override;

	private:
		InspectorPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::unique_ptr<InspectorPanelState> _upState{};
		std::unique_ptr<InspectorComponentEditorRegistry> _upComponentEditorRegistry{};
		bool _bRefreshEntityDraftsOnNextGui = false;
	};
}
