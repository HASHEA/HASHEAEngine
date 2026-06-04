#pragma once

#include <cstdint>

namespace AshEngine
{
	class Entity;
	class UIContext;
	enum class SceneComponentType : uint8_t;
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
		virtual AshEngine::SceneComponentType GetComponentType() const = 0;
		virtual const char* GetDisplayName() const = 0;
		virtual bool CanAdd(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) const = 0;
		virtual bool AddDefault(
			IInspectorComponentHost& refHost,
			AshEngine::UIContext& refUi,
			AshEngine::Entity entity) = 0;
		virtual bool ShouldDraw(IInspectorComponentHost& refHost, const AshEngine::Entity& entity) = 0;
		virtual void Draw(IInspectorComponentHost& refHost, AshEngine::UIContext& refUi, AshEngine::Entity entity) = 0;
	};
}
