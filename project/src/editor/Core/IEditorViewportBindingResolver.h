#pragma once

#include "Function/Render/ScenePresentationSubsystem.h"

#include <string>

namespace AshEngine
{
	class Scene;
}

namespace AshEditor
{
	struct EditorViewportBindingOverride
	{
		AshEngine::Scene* pScene = nullptr;
		AshEngine::SceneCameraSelector camera{};
	};

	class IEditorViewportBindingResolver
	{
	public:
		virtual ~IEditorViewportBindingResolver() = default;

		virtual bool TryResolveViewportBinding(
			const std::string& strViewportId,
			EditorViewportBindingOverride& outOverride) const = 0;
	};
}
