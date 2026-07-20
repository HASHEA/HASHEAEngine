#include "Core/SceneSnapshotUtils.h"
#include "Core/SceneSnapshotComponentUtils.h"

#include <algorithm>
#include <exception>
#include <numeric>
#include <tuple>
#include <unordered_set>

namespace AshEditor::SceneSnapshotUtils
{
	namespace
	{
		SceneEntitySnapshot CaptureSnapshotRecursive(
			const AshEngine::Scene& refScene,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refScene.get_entity_sibling_index(refEntity.get_id());

			snapshot.vecComponents = SceneSnapshotComponentUtils::CaptureComponentSnapshots(refEntity);

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refScene, refChild));
			}
			return snapshot;
		}

		enum class RestoreIdentityMode : uint8_t
		{
			PreserveIds,
			AllocateIds,
		};

		using RestoreRootRequest = SceneSnapshotRestoreRequest;

		struct PendingSnapshotApplication
		{
			const SceneEntitySnapshot* pSnapshot = nullptr;
			SceneEntityId uTargetEntityId = 0;
		};

		bool PreflightSnapshotRecursive(
			const AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const RestoreIdentityMode eMode,
			std::unordered_set<SceneEntityId>& refSourceIds,
			size_t& refEntityCount)
		{
			if (refSnapshot.uEntityId == 0 || !refSourceIds.insert(refSnapshot.uEntityId).second)
			{
				return false;
			}
			if (eMode == RestoreIdentityMode::PreserveIds &&
				refScene.find_entity(refSnapshot.uEntityId).is_valid())
			{
				return false;
			}

			std::unordered_set<uint8_t> componentTypes{};
			for (const SceneComponentSnapshot& refComponent : refSnapshot.vecComponents)
			{
				const uint8_t type = static_cast<uint8_t>(refComponent.eType);
				if (!AshEngine::get_scene_component_descriptor(refComponent.eType) ||
					!componentTypes.insert(type).second)
				{
					return false;
				}
			}

			++refEntityCount;
			for (const SceneEntitySnapshot& refChild : refSnapshot.vecChildren)
			{
				if (!PreflightSnapshotRecursive(refScene, refChild, eMode, refSourceIds, refEntityCount))
				{
					return false;
				}
			}
			return true;
		}

		AshEngine::Entity CreateRestoredEntity(
			AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex,
			const RestoreIdentityMode eMode)
		{
			const AshEngine::Entity parent =
				uParentId != 0 ? refScene.find_entity(uParentId) : AshEngine::Entity{};
			if (uParentId != 0 && !parent.is_valid())
			{
				return {};
			}

			if (eMode == RestoreIdentityMode::PreserveIds)
			{
				return refScene.create_entity_with_id(
					refSnapshot.uEntityId,
					"Entity",
					parent,
					uSiblingIndex);
			}
			return refScene.create_entity("Entity", parent, uSiblingIndex);
		}

		bool CreateSnapshotHierarchy(
			AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex,
			const RestoreIdentityMode eMode,
			SceneSnapshotComponentUtils::SceneEntityIdRemap& refEntityIdRemap,
			std::vector<PendingSnapshotApplication>& refPendingApplications,
			std::vector<SceneEntityId>& refCreatedEntityIds)
		{
			AshEngine::Entity entity =
				CreateRestoredEntity(refScene, refSnapshot, uParentId, uSiblingIndex, eMode);
			if (!entity.is_valid())
			{
				return false;
			}

			const SceneEntityId targetEntityId = entity.get_id();
			if (!refEntityIdRemap.emplace(refSnapshot.uEntityId, targetEntityId).second)
			{
				return false;
			}
			refPendingApplications.push_back({ &refSnapshot, targetEntityId });
			refCreatedEntityIds.push_back(targetEntityId);

			for (const SceneEntitySnapshot& refChild : refSnapshot.vecChildren)
			{
				if (!CreateSnapshotHierarchy(
					refScene,
					refChild,
					targetEntityId,
					refChild.uSiblingIndex,
					eMode,
					refEntityIdRemap,
					refPendingApplications,
					refCreatedEntityIds))
				{
					return false;
				}
			}
			return true;
		}

		bool RestoreSnapshotForest(
			AshEngine::Scene& refScene,
			const std::vector<RestoreRootRequest>& refRequests,
			const RestoreIdentityMode eMode,
			std::vector<SceneEntityId>& outRootEntityIds,
			std::vector<SceneEntityId>* pCreatedEntityIds)
		{
			if (!refScene.is_valid() || refRequests.empty())
			{
				return false;
			}

			std::unordered_set<SceneEntityId> sourceIds{};
			size_t entityCount = 0;
			for (const RestoreRootRequest& refRequest : refRequests)
			{
				if (!refRequest.pSnapshot ||
					(refRequest.uParentId != 0 && !refScene.find_entity(refRequest.uParentId).is_valid()) ||
					!PreflightSnapshotRecursive(
						refScene,
						*refRequest.pSnapshot,
						eMode,
						sourceIds,
						entityCount))
				{
					return false;
				}
			}

			SceneSnapshotComponentUtils::SceneEntityIdRemap entityIdRemap{};
			std::vector<PendingSnapshotApplication> pendingApplications{};
			std::vector<SceneEntityId> createdEntityIds{};
			std::vector<SceneEntityId> createdRootIds(refRequests.size(), 0);
			std::vector<SceneEntityId> publishedCreatedEntityIds{};
			std::vector<size_t> creationOrder(refRequests.size());
			std::iota(creationOrder.begin(), creationOrder.end(), size_t{ 0 });
			std::sort(
				creationOrder.begin(),
				creationOrder.end(),
				[&refRequests](const size_t left, const size_t right)
				{
					const RestoreRootRequest& leftRequest = refRequests[left];
					const RestoreRootRequest& rightRequest = refRequests[right];
					return std::tie(leftRequest.uParentId, leftRequest.uSiblingIndex, left) <
						std::tie(rightRequest.uParentId, rightRequest.uSiblingIndex, right);
				});
			entityIdRemap.reserve(entityCount);
			pendingApplications.reserve(entityCount);
			createdEntityIds.reserve(entityCount);
			AshEngine::Scene::CreationTransaction transaction = refScene.begin_creation_transaction();
			if (!transaction.is_active())
			{
				return false;
			}

			bool success = false;
			try
			{
				for (const size_t requestIndex : creationOrder)
				{
					const RestoreRootRequest& refRequest = refRequests[requestIndex];
					const size_t beforeCreate = createdEntityIds.size();
					const bool created = CreateSnapshotHierarchy(
						refScene,
						*refRequest.pSnapshot,
						refRequest.uParentId,
						refRequest.uSiblingIndex,
						eMode,
						entityIdRemap,
						pendingApplications,
						createdEntityIds);
					if (createdEntityIds.size() > beforeCreate)
					{
						createdRootIds[requestIndex] = createdEntityIds[beforeCreate];
					}
					if (!created || createdEntityIds.size() == beforeCreate)
					{
						break;
					}
				}

				if (std::all_of(
					createdRootIds.begin(),
					createdRootIds.end(),
					[](const SceneEntityId entityId) { return entityId != 0; }))
				{
					success = true;
					for (const PendingSnapshotApplication& refPending : pendingApplications)
					{
						AshEngine::Entity entity = refScene.find_entity(refPending.uTargetEntityId);
						if (!entity.is_valid() ||
							!SceneSnapshotComponentUtils::ApplyEntitySnapshot(
								entity,
								*refPending.pSnapshot,
								&entityIdRemap))
						{
							success = false;
							break;
						}
					}
				}

				if (success)
				{
					for (size_t index = 0; index < refRequests.size(); ++index)
					{
						const char* pSuffix = refRequests[index].pRootNameSuffix;
						if (pSuffix && pSuffix[0] != '\0')
						{
							AshEngine::Entity root = refScene.find_entity(createdRootIds[index]);
							if (!root.is_valid() || !root.set_name(root.get_name() + pSuffix))
							{
								success = false;
								break;
							}
						}
					}
				}

				if (success && pCreatedEntityIds)
				{
					publishedCreatedEntityIds = *pCreatedEntityIds;
					publishedCreatedEntityIds.reserve(
						publishedCreatedEntityIds.size() + createdEntityIds.size());
					publishedCreatedEntityIds.insert(
						publishedCreatedEntityIds.end(),
						createdEntityIds.begin(),
						createdEntityIds.end());
				}
			}
			catch (const std::exception&)
			{
				success = false;
			}

			if (!success)
			{
				return false;
			}
			if (!transaction.commit())
			{
				return false;
			}

			outRootEntityIds.swap(createdRootIds);
			if (pCreatedEntityIds)
			{
				pCreatedEntityIds->swap(publishedCreatedEntityIds);
			}
			return true;
		}
	}

	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		const SceneEntityId uEntityId)
	{
		const AshEngine::Entity entity = refScene.find_entity(uEntityId);
		if (!entity.is_valid())
		{
			return std::nullopt;
		}

		return CaptureSnapshotRecursive(refScene, entity);
	}

	bool RestoreEntitySnapshots(
		AshEngine::Scene& refScene,
		const std::vector<SceneSnapshotRestoreRequest>& refRequests,
		std::vector<SceneEntityId>& outRootEntityIds)
	{
		return RestoreSnapshotForest(
			refScene,
			refRequests,
			RestoreIdentityMode::PreserveIds,
			outRootEntityIds,
			nullptr);
	}

	bool RestoreEntitySnapshotsAsCopies(
		AshEngine::Scene& refScene,
		const std::vector<SceneSnapshotRestoreRequest>& refRequests,
		std::vector<SceneEntityId>& outRootEntityIds,
		std::vector<SceneEntityId>* pCreatedEntityIds)
	{
		return RestoreSnapshotForest(
			refScene,
			refRequests,
			RestoreIdentityMode::AllocateIds,
			outRootEntityIds,
			pCreatedEntityIds);
	}

	AshEngine::Entity RestoreEntitySnapshot(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		const SceneEntityId uParentId)
	{
		std::vector<SceneEntityId> rootEntityIds{};
		const RestoreRootRequest request{ &refSnapshot, uParentId, refSnapshot.uSiblingIndex, nullptr };
		if (!RestoreSnapshotForest(
			refScene,
			{ request },
			RestoreIdentityMode::PreserveIds,
			rootEntityIds,
			nullptr) || rootEntityIds.size() != 1u)
		{
			return {};
		}
		return refScene.find_entity(rootEntityIds.front());
	}

	AshEngine::Entity RestoreEntitySnapshotAsCopy(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		const SceneEntityId uParentId,
		const uint32_t uSiblingIndex,
		std::vector<SceneEntityId>* pCreatedEntityIds,
		const char* pRootNameSuffix)
	{
		std::vector<SceneEntityId> rootEntityIds{};
		const RestoreRootRequest request{ &refSnapshot, uParentId, uSiblingIndex, pRootNameSuffix };
		if (!RestoreSnapshotForest(
			refScene,
			{ request },
			RestoreIdentityMode::AllocateIds,
			rootEntityIds,
			pCreatedEntityIds) || rootEntityIds.size() != 1u)
		{
			return {};
		}
		return refScene.find_entity(rootEntityIds.front());
	}

	AshEngine::Scene CloneScene(const AshEngine::Scene& refSourceScene)
	{
		if (!refSourceScene.is_valid())
		{
			return {};
		}

		AshEngine::Scene clonedScene = AshEngine::Scene::create(refSourceScene.get_name());
		if (!clonedScene.is_valid())
		{
			return {};
		}

		const std::vector<AshEngine::Entity> sourceRoots = refSourceScene.get_root_entities();
		std::vector<SceneEntitySnapshot> snapshots{};
		snapshots.reserve(sourceRoots.size());
		for (const AshEngine::Entity& refRootEntity : sourceRoots)
		{
			const std::optional<SceneEntitySnapshot> optSnapshot =
				CaptureEntitySnapshot(refSourceScene, refRootEntity.get_id());
			if (!optSnapshot.has_value())
			{
				return {};
			}
			snapshots.push_back(*optSnapshot);
		}
		if (snapshots.empty())
		{
			clonedScene.mark_clean();
			return clonedScene;
		}

		std::vector<RestoreRootRequest> requests{};
		requests.reserve(snapshots.size());
		for (const SceneEntitySnapshot& refSnapshot : snapshots)
		{
			requests.push_back({ &refSnapshot, 0, refSnapshot.uSiblingIndex, nullptr });
		}
		std::vector<SceneEntityId> clonedRootIds{};
		if (!RestoreSnapshotForest(
			clonedScene,
			requests,
			RestoreIdentityMode::PreserveIds,
			clonedRootIds,
			nullptr))
		{
			return {};
		}

		clonedScene.mark_clean();
		return clonedScene;
	}
}
