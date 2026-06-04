#pragma once

#include <memory>
#include <vector>

namespace AshEngine
{
	class Entity;
}

namespace AshEditor
{
	class InspectorComponentEditor;
	class IInspectorComponentHost;

	class InspectorComponentEditorRegistry final
	{
	public:
		InspectorComponentEditorRegistry();
		~InspectorComponentEditorRegistry();

	public:
		void Register(std::unique_ptr<InspectorComponentEditor> upEditor);
		std::vector<InspectorComponentEditor*> CollectVisibleEditors(
			IInspectorComponentHost& refHost,
			const AshEngine::Entity& refEntity) const;
		std::vector<InspectorComponentEditor*> CollectAddableEditors(
			IInspectorComponentHost& refHost,
			const AshEngine::Entity& refEntity) const;

	private:
		std::vector<std::unique_ptr<InspectorComponentEditor>> _vecEditors{};
	};

	void RegisterDefaultInspectorComponentEditors(InspectorComponentEditorRegistry& refRegistry);
}
