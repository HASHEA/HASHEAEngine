#pragma once

#include "Core/EditorSceneTypes.h"
#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/Scene.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;

	class SceneService
	{
	public:
		// Initializes the active scene. If pathStartupScene is empty or load fails, the service still provides a valid default scene.
		bool Initialize(const std::filesystem::path& pathStartupScene);
		void SetEventBus(EditorEventBus* pEventBus);

		AshEngine::Scene& GetActiveScene();
		const AshEngine::Scene& GetActiveScene() const;

		// Finds an entity by id in the active scene. Returns an invalid handle if not found.
		AshEngine::Entity FindEntity(SceneEntityId uEntityId) const;
		uint32_t GetEntitySiblingIndex(SceneEntityId uEntityId) const;

		// Creates an entity in the active scene.
		// - uParentId = 0 means root-level.
		// - uSiblingIndex uses engine conventions; k_scene_append_sibling_index appends to the end.
		AshEngine::Entity CreateEntity(const std::string& strName, SceneEntityId uParentId = 0);
		AshEngine::Entity CreateEntity(const std::string& strName, SceneEntityId uParentId, uint32_t uSiblingIndex);
		AshEngine::Entity CreateEntityWithId(SceneEntityId uEntityId, const std::string& strName, SceneEntityId uParentId = 0);
		AshEngine::Entity CreateEntityWithId(SceneEntityId uEntityId, const std::string& strName, SceneEntityId uParentId, uint32_t uSiblingIndex);

		// Modifies entity state. Returns false if the entity does not exist or the operation is not allowed.
		bool RenameEntity(SceneEntityId uEntityId, std::string_view svName);
		bool DestroyEntity(SceneEntityId uEntityId);
		bool ReparentEntity(SceneEntityId uEntityId, SceneEntityId uNewParentId);
		bool ReparentEntity(SceneEntityId uEntityId, SceneEntityId uNewParentId, uint32_t uSiblingIndex);

		// Validates whether reparenting would create cycles or otherwise violates hierarchy rules.
		bool CanReparentEntity(SceneEntityId uEntityId, SceneEntityId uNewParentId) const;
		bool IsDescendantOf(SceneEntityId uEntityId, SceneEntityId uPotentialAncestorId) const;
		// Filters candidate ids to existing entities and drops descendants whose ancestor is also included.
		std::vector<SceneEntityId> BuildTopLevelEntityIds(const std::vector<uint64_t>& vecCandidateEntityIds) const;

		// Captures/restores a self-contained snapshot of an entity subtree for undo/redo.
		std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(SceneEntityId uEntityId) const;
		AshEngine::Entity RestoreEntitySnapshot(const SceneEntitySnapshot& refSnapshot, SceneEntityId uParentId = 0);

		// Resets the active scene to a new default scene. The active scene path is cleared.
		void NewScene(const std::string& strName);

		// Loads/saves the active scene from/to disk. On load success, active_scene_path is updated.
		bool LoadScene(const std::filesystem::path& pathScene);
		bool SaveScene(const std::filesystem::path& pathScene);

		// Returns the currently loaded scene path, or empty if the scene is unsaved/new.
		const std::filesystem::path& GetActiveScenePath() const;

	private:
		void CreateDefaultEntities();
		void SubscribeActiveSceneChanges();
		void UnsubscribeActiveSceneChanges();
		void PublishSceneChanged(const AshEngine::SceneChangeEvent& refEvent) const;
		void PublishDirtyStateChanged(bool bDirty) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		AshEngine::Scene _activeScene{};
		std::filesystem::path _pathActiveScene{};
		uint32_t _uSceneChangeSubscriptionId = 0;
	};
}
