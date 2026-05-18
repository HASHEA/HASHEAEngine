#pragma once

#include "Panels/Inspector/InspectorComponentEditor.h"

namespace AshEditor
{
	class CameraComponentEditor final : public InspectorComponentEditor
	{
	public:
		bool ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) override;
		void Draw(IInspectorComponentHost& refHost, AshEngine::UIContext& refUi, AshEngine::Entity entity) override;
	};
}
