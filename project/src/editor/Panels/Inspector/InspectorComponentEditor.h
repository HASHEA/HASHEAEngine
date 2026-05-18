#pragma once

namespace AshEngine
{
	class Entity;
	class UIContext;
}

namespace AshEditor
{
	class IInspectorComponentHost;

	// Per-component Inspector renderer. Owns only UI flow for one component type.
	class InspectorComponentEditor
	{
	public:
		virtual ~InspectorComponentEditor() = default;

	public:
		virtual bool ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) = 0;
		virtual void Draw(IInspectorComponentHost& refHost, AshEngine::UIContext& refUi, AshEngine::Entity entity) = 0;
	};
}
