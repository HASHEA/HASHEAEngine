#pragma once

#include "Core/TerrainEditorSessionCore.h"
#include "Function/Render/ScenePresentationHandles.h"

#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>

namespace AshEngine
{
	struct TerrainAssetSnapshot;
}

namespace AshEditor
{
	class TerrainBrushOverlayRenderer final
	{
	public:
		using SubmitOverlayFunction = bool(*)(
			AshEngine::SceneViewBindingHandle,
			const AshEngine::SceneOverlayBatchDesc&);

		static std::vector<AshEngine::SceneOverlayLine> BuildLines(
			const TerrainEditorPreviewState& refPreview,
			const AshEngine::TerrainAssetSnapshot& refSnapshot,
			const glm::mat4& matTerrainLocalToWorld);

		static bool Submit(
			const TerrainEditorPreviewState& refPreview,
			const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
			const glm::mat4& matTerrainLocalToWorld,
			AshEngine::SceneViewBindingHandle binding);

		static bool Submit(
			const TerrainEditorPreviewState& refPreview,
			const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& refSnapshot,
			const glm::mat4& matTerrainLocalToWorld,
			AshEngine::SceneViewBindingHandle binding,
			SubmitOverlayFunction pSubmitOverlay);
	};
}
