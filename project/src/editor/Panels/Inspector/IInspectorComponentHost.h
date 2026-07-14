#pragma once

#include "Core/PanelDeps/InspectorPanelDeps.h"
#include "Panels/Inspector/InspectorPanelState.h"

namespace AshEngine
{
	class Entity;
	struct MeshComponent;
	class UIContext;
}

namespace AshEditor
{
	// Narrow host contract exposed to split component editors.
	// InspectorPanel stays the owner and wiring point; editors call back through this surface.
	class IInspectorComponentHost
	{
	public:
		virtual ~IInspectorComponentHost() = default;

	public:
		virtual InspectorPanelState& AccessInspectorState() = 0;
		virtual const InspectorPanelDeps& AccessInspectorDeps() const = 0;
		virtual bool DrawComponentHeaderContextMenu(AshEngine::UIContext& refUi, const char* pPopupId) = 0;
		virtual bool DrawComponentRemoveAction(AshEngine::UIContext& refUi, const char* pIdSuffix) = 0;
		virtual bool DrawMeshAssetPathEditor(AshEngine::UIContext& refUi, AshEngine::MeshComponent& refMeshComponent) = 0;

		virtual void SyncCameraDraft(const AshEngine::Entity& entity) = 0;
		virtual void SyncLightDraft(const AshEngine::Entity& entity) = 0;
		virtual void SyncMeshDraft(const AshEngine::Entity& entity) = 0;
		virtual void SyncEnvironmentDraft(const AshEngine::Entity& entity) = 0;
		virtual void SyncParticleDraft(const AshEngine::Entity& entity) = 0;
		virtual void SyncTerrainDraft(const AshEngine::Entity& entity) = 0;

		virtual void ResetCameraDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetCameraDraftToDefaults(const AshEngine::Entity& entity) = 0;
		virtual void ResetLightDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetLightDraftToDefaults(const AshEngine::Entity& entity) = 0;
		virtual void ResetMeshDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetMeshDraftToDefaults(const AshEngine::Entity& entity) = 0;
		virtual void ResetEnvironmentDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetEnvironmentDraftToDefaults(const AshEngine::Entity& entity) = 0;
		virtual void ResetParticleDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetParticleDraftToDefaults(const AshEngine::Entity& entity) = 0;
		virtual void ResetTerrainDraftToLive(const AshEngine::Entity& entity) = 0;
		virtual void ResetTerrainDraftToDefaults(const AshEngine::Entity& entity) = 0;

		virtual bool CommitCameraDraft(AshEngine::Entity entity) = 0;
		virtual bool CommitLightDraft(AshEngine::Entity entity) = 0;
		virtual bool CommitMeshDraft(AshEngine::Entity entity) = 0;
		virtual bool CommitEnvironmentDraft(AshEngine::Entity entity) = 0;
		virtual bool CommitParticleDraft(AshEngine::Entity entity) = 0;
		virtual bool CommitTerrainDraft(AshEngine::Entity entity) = 0;
	};
}
