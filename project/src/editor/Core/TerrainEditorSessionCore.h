#pragma once

#include "Function/Asset/TerrainData.h"
#include "Function/Scene/TerrainQuery.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace AshEditor
{
	struct TerrainEditorPreviewState
	{
		AshEngine::TerrainQueryStatus query_status = AshEngine::TerrainQueryStatus::Outside;
		glm::vec3 center_ws{ 0.0f };
		glm::vec3 normal_ws{ 0.0f, 1.0f, 0.0f };
		float radius = 1.0f;
		bool layer_locked = false;
		bool stroke_active = false;
	};

	class TerrainEditorSessionCore final
	{
	public:
		AshEngine::TerrainAssetId GetAssetId() const;
		const TerrainEditorPreviewState& GetPreviewState() const;
		bool HasActiveStroke() const;

	private:
		AshEngine::TerrainAssetId _assetId = 0;
		uint64_t _activeSequence = 0;
		TerrainEditorPreviewState _preview{};
	};
}
