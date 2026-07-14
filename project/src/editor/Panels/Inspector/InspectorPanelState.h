#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace AshEditor
{
	// Shared draft/state bag kept by InspectorPanel and reused by split component editors.
	struct InspectorPanelState
	{
		struct IdentityDraft
		{
			SceneEntityId uEntityId = 0;
			std::string strOriginalName{};
			std::string strCurrentName{};
		};

		struct TransformDraft
		{
			SceneEntityId uEntityId = 0;
			AshEngine::TransformComponent originalValue{};
			AshEngine::TransformComponent currentValue{};
		};

		struct CameraDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::CameraComponent> optOriginalValue{};
			std::optional<AshEngine::CameraComponent> optCurrentValue{};
		};

		struct LightDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::LightComponent> optOriginalValue{};
			std::optional<AshEngine::LightComponent> optCurrentValue{};
		};

		struct MeshDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::MeshComponent> optOriginalValue{};
			std::optional<AshEngine::MeshComponent> optCurrentValue{};
		};

		struct EnvironmentDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::EnvironmentComponent> optOriginalValue{};
			std::optional<AshEngine::EnvironmentComponent> optCurrentValue{};
		};

		struct ParticleDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::ParticleComponent> optOriginalValue{};
			std::optional<AshEngine::ParticleComponent> optCurrentValue{};
		};

		struct TerrainDraft
		{
			SceneEntityId uEntityId = 0;
			std::optional<AshEngine::TerrainComponent> optOriginalValue{};
			std::optional<AshEngine::TerrainComponent> optCurrentValue{};
		};

		IdentityDraft draftIdentity{};
		TransformDraft draftTransform{};
		CameraDraft draftCamera{};
		LightDraft draftLight{};
		MeshDraft draftMesh{};
		EnvironmentDraft draftEnvironment{};
		ParticleDraft draftParticle{};
		TerrainDraft draftTerrain{};

		std::vector<std::string> vecRecentMeshPaths{};
		std::vector<std::string> vecRecentMaterialPaths{};
		std::vector<std::string> vecRecentEnvironmentIblPaths{};
		std::vector<std::string> vecRecentEnvironmentTexturePaths{};
		std::vector<std::string> vecRecentTerrainPaths{};
		std::string strAssetPickerSearch{};
		std::string strMaterialAssetPickerSearch{};
		std::string strEnvironmentIblAssetPickerSearch{};
		std::string strEnvironmentTextureAssetPickerSearch{};
		std::string strTerrainAssetPickerSearch{};

		void PushRecentMeshPath(const std::string& strPath)
		{
			if (strPath.empty())
			{
				return;
			}

			vecRecentMeshPaths.erase(
				std::remove(vecRecentMeshPaths.begin(), vecRecentMeshPaths.end(), strPath),
				vecRecentMeshPaths.end());
			vecRecentMeshPaths.insert(vecRecentMeshPaths.begin(), strPath);
			if (vecRecentMeshPaths.size() > 10)
			{
				vecRecentMeshPaths.resize(10);
			}
		}
	};
}
