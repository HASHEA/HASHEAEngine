#pragma once

#include "Function/Scene/SceneComponents.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class IInspectorComponentHost;

	struct MeshMaterialOverridesEditResult
	{
		bool bCommitRequested = false;
		bool bBlocksCommit = false;
	};

	MeshMaterialOverridesEditResult DrawMeshMaterialOverridesEditor(
		IInspectorComponentHost& refHost,
		AshEngine::UIContext& refUi,
		AshEngine::MeshComponent& refMesh);
}
