#pragma once

#include "Panels/Inspector/InspectorComponentEditor.h"

namespace AshEditor
{
	class ParticleComponentEditor final : public InspectorComponentEditor
	{
	public:
		AshEngine::SceneComponentType GetComponentType() const override;
		const char* GetDisplayName() const override;
		bool CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const override;
		bool AddDefault(
			IInspectorComponentHost& refHost,
			AshEngine::UIContext& refUi,
			AshEngine::Entity entity) override;
		bool ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) override;
		void Draw(IInspectorComponentHost& refHost, AshEngine::UIContext& refUi, AshEngine::Entity entity) override;
	};
}
