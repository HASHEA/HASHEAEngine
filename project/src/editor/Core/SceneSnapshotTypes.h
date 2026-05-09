#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <string>
#include <vector>

namespace AshEditor
{
	struct SceneComponentSnapshot
	{
		AshEngine::SceneComponentType eType = AshEngine::SceneComponentType::Name;
		std::string strSerializedValue{};
	};

	struct SceneEntitySnapshot
	{
		SceneEntityId uEntityId = 0;
		uint32_t uSiblingIndex = kSceneAppendSiblingIndex;
		std::vector<SceneComponentSnapshot> vecComponents{};
		std::vector<SceneEntitySnapshot> vecChildren{};
	};
}
