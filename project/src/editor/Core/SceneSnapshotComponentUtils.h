#pragma once

#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/Scene.h"

#include <optional>
#include <vector>

namespace AshEditor
{
	namespace SceneSnapshotComponentUtils
	{
		std::optional<SceneComponentSnapshot> CaptureComponentSnapshot(
			const AshEngine::Entity& refEntity,
			AshEngine::SceneComponentType eType);
		std::vector<SceneComponentSnapshot> CaptureComponentSnapshots(const AshEngine::Entity& refEntity);
		// Re-applies the snapshot payload to an existing entity and removes optional components that are absent in the snapshot.
		bool ApplyEntitySnapshot(AshEngine::Entity entity, const SceneEntitySnapshot& refSnapshot);
	}
}
