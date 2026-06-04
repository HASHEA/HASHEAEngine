#include "Panels/Inspector/InspectorComponentEditorRegistry.h"

#include "Panels/Inspector/CameraComponentEditor.h"
#include "Panels/Inspector/EnvironmentComponentEditor.h"
#include "Panels/Inspector/InspectorComponentEditor.h"
#include "Panels/Inspector/IInspectorComponentHost.h"
#include "Panels/Inspector/LightComponentEditor.h"
#include "Panels/Inspector/MeshComponentEditor.h"

#include <memory>

namespace AshEditor
{
	InspectorComponentEditorRegistry::InspectorComponentEditorRegistry() = default;

	InspectorComponentEditorRegistry::~InspectorComponentEditorRegistry() = default;

	void InspectorComponentEditorRegistry::Register(std::unique_ptr<InspectorComponentEditor> upEditor)
	{
		if (!upEditor)
		{
			return;
		}

		_vecEditors.push_back(std::move(upEditor));
	}

	std::vector<InspectorComponentEditor*> InspectorComponentEditorRegistry::CollectVisibleEditors(
		IInspectorComponentHost& refHost,
		const AshEngine::Entity& refEntity) const
	{
		std::vector<InspectorComponentEditor*> vecEditors{};
		vecEditors.reserve(_vecEditors.size());
		for (const std::unique_ptr<InspectorComponentEditor>& upEditor : _vecEditors)
		{
			if (upEditor && upEditor->ShouldDraw(refHost, refEntity))
			{
				vecEditors.push_back(upEditor.get());
			}
		}
		return vecEditors;
	}

	std::vector<InspectorComponentEditor*> InspectorComponentEditorRegistry::CollectAddableEditors(
		IInspectorComponentHost& refHost,
		const AshEngine::Entity& refEntity) const
	{
		std::vector<InspectorComponentEditor*> vecEditors{};
		vecEditors.reserve(_vecEditors.size());
		for (const std::unique_ptr<InspectorComponentEditor>& upEditor : _vecEditors)
		{
			if (upEditor && upEditor->CanAdd(refHost, refEntity))
			{
				vecEditors.push_back(upEditor.get());
			}
		}
		return vecEditors;
	}

	void RegisterDefaultInspectorComponentEditors(InspectorComponentEditorRegistry& refRegistry)
	{
		refRegistry.Register(std::make_unique<CameraComponentEditor>());
		refRegistry.Register(std::make_unique<LightComponentEditor>());
		refRegistry.Register(std::make_unique<MeshComponentEditor>());
		refRegistry.Register(std::make_unique<EnvironmentComponentEditor>());
	}
}
